#include "WebRenderer.h"
#include <cstring>
#include <unordered_map>
#include <utility>

namespace {
// ---- 快照 v2 常量（与 _agent/prompts/CONTRACT.md 第 2 节逐字节一致）----
constexpr uint8_t kMagic = 'W';
constexpr uint8_t kVersion = 2;
constexpr size_t kHeaderSize = 80;
constexpr size_t kBallRecSize = 6;
constexpr size_t kNucleusRecSize = 32;
constexpr size_t kCyanoNodeRecSize = 24;
constexpr size_t kCyanoLinkRecSize = 8;
constexpr size_t kFinishReasonLen = 28;

void writeU8(std::vector<uint8_t>& v, size_t off, uint8_t x) { v[off] = x; }
void writeU16(std::vector<uint8_t>& v, size_t off, uint16_t x) {
    v[off] = (uint8_t)(x & 0xFF);
    v[off + 1] = (uint8_t)(x >> 8);
}
void writeU32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off] = (uint8_t)(x & 0xFF);
    v[off + 1] = (uint8_t)((x >> 8) & 0xFF);
    v[off + 2] = (uint8_t)((x >> 16) & 0xFF);
    v[off + 3] = (uint8_t)((x >> 24) & 0xFF);
}
void writeF32(std::vector<uint8_t>& v, size_t off, float x) {
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(x), "float must be 32-bit");
    std::memcpy(&bits, &x, sizeof(bits));
    writeU32(v, off, bits);
}

// ---- 蓝细菌快照视图（字段与契约 §2 的 24 字节节点记录一一对应）----
// srcIdx：该节点在 Cyanobacteria::nodes() 数组中的原始下标（仅存活节点会进快照，
// 所以原始下标 ≠ 快照下标，连接记录必须经 mapLinkEndpoints 重映射）。
struct CyanoNodeView {
    float x = 0.0f;
    float y = 0.0f;
    float energy = 0.0f;
    uint32_t id = 0;        // 稳定谱系 ID
    uint16_t linkCount = 0;
    uint16_t colonyId = 0;
    uint16_t age = 0;
    uint16_t flags = 0;     // bit0 = 存活，bit1..2 = 类型
    uint32_t srcIdx = 0;    // nodes_ 数组下标（重映射用，不写入快照）
};

// 原始连接端点：语义为 Cyanobacteria::nodes_ 数组下标（见 src/Cyanobacteria.h
// 中 CyanoLink 注释「a < b（nodes_ 数组下标）」），**不是**稳定 id。
struct CyanoLinkRaw {
    uint32_t a = 0;
    uint32_t b = 0;
};
struct CyanoLinkView {
    uint32_t a = 0;  // 快照内 cyanoNodes 数组下标
    uint32_t b = 0;  // 同上，保证 a < b
};

// 由存活节点构建 nodes_ 原始下标 → 快照数组下标 的映射。
// 选择 unordered_map：一次构建 O(P)，每条连接两端 O(1) 查询，总计 O(P+L)；
// 节点数不大时也可线性查找，但 O(L·P) 在 P 上千时会明显变慢，故用哈希表。
std::unordered_map<uint32_t, uint32_t> buildSnapshotIndex(
    const std::vector<CyanoNodeView>& nodes) {
    std::unordered_map<uint32_t, uint32_t> map;
    map.reserve(nodes.size() * 2 + 1);
    for (size_t i = 0; i < nodes.size(); ++i) {
        map.emplace(nodes[i].srcIdx, (uint32_t)i);
    }
    return map;
}

// 把「nodes_ 数组下标语义」的原始连接映射为快照内数组下标：
//   - 任一端点不在存活节点集合中（节点已死/未进快照）→ 丢弃该连接，
//     保证头部 cyanoLinkCount 与实际记录条数、与 aliveLinkCount 语义一致；
//   - 两端映射到同一快照下标 → 丢弃（自环无效）；
//   - 输出统一交换为 a < b。
void mapLinkEndpoints(const std::vector<CyanoNodeView>& nodes,
                      const std::vector<CyanoLinkRaw>& raw,
                      std::vector<CyanoLinkView>& out) {
    const std::unordered_map<uint32_t, uint32_t> srcToSnap = buildSnapshotIndex(nodes);
    out.clear();
    out.reserve(raw.size());
    for (const CyanoLinkRaw& r : raw) {
        auto ia = srcToSnap.find(r.a);
        auto ib = srcToSnap.find(r.b);
        if (ia == srcToSnap.end() || ib == srcToSnap.end()) continue;
        if (ia->second == ib->second) continue;
        uint32_t a = ia->second;
        uint32_t b = ib->second;
        if (a > b) { uint32_t t = a; a = b; b = t; }
        out.push_back(CyanoLinkView{a, b});
    }
}

// 计算每个 nodes_ 下标的**存活度数**：只统计两端都存活的 links_ 条目。
// 为什么不能直接用 CyanoNode::linkCount 缓存：捕食发生在 World::step() 的
// absorbBalls() 阶段（把节点标为 alive=0），而「移除死节点 + 重映射 links_ +
// 重算 linkCount」发生在**下一次 step() 开头**（冻结契约 A5 红线 1：consumeNode
// 只标死、不立即移除，以免 World 手上的下标失效）。快照恰好取在这一帧内窗口里，
// 此时缓存仍计入「指向刚被吃掉节点」的连接，而 mapLinkEndpoints() 会把那些连接
// 丢掉 → 若直接用缓存，节点 linkCount 之和会 ≠ 2 × 头部 cyanoLinkCount。
// 因此这里现场统计，保证快照自洽：Σ linkCount == 2 × cyanoLinkCount。
std::vector<uint16_t> aliveLinkDegrees(const Cyanobacteria& cy) {
    const std::vector<CyanoNode>& ns = cy.nodes();
    std::vector<uint16_t> deg(ns.size(), 0);
    const std::vector<CyanoLink>& ls = cy.links();
    for (size_t i = 0; i < ls.size(); ++i) {
        const CyanoLink& l = ls[i];
        if (l.a >= ns.size() || l.b >= ns.size()) continue;
        if (ns[l.a].alive == 0 || ns[l.b].alive == 0) continue;  // 只计两端都存活
        if (deg[l.a] < 0xFFFFu) ++deg[l.a];
        if (deg[l.b] < 0xFFFFu) ++deg[l.b];
    }
    return deg;
}

// 取本帧蓝细菌快照数据（存活节点视图 / 原始连接 / 全局阳光）。
// 数据源：World::cyano()（WS4 已落盘）与 World::sunLevel()。
// 注：头部偏移 72 的平均能量不在此取，而由 buildSnapshot 对写入快照的
//     存活节点就地求均值，保证与节点记录逐条自洽（无节点 = 0）。
void fetchCyanoSnapshot(const World& world,
                        std::vector<CyanoNodeView>& nodes,
                        std::vector<CyanoLinkRaw>& rawLinks,
                        float& sun) {
    nodes.clear();
    rawLinks.clear();

    const Cyanobacteria& cy = world.cyano();
    // 现场统计存活度数（见上方 aliveLinkDegrees 注释），保证 ΣlinkCount == 2×cyanoLinkCount。
    const std::vector<uint16_t> deg = aliveLinkDegrees(cy);
    size_t idx = 0;
    for (const CyanoNode& n : cy.nodes()) {            // 只收存活节点
        if (n.alive) {
            CyanoNodeView v;
            v.x = (float)n.pos.x;  v.y = (float)n.pos.y;
            v.energy = (float)n.energy;
            v.id = n.id;
            v.linkCount = (idx < deg.size()) ? deg[idx] : 0;
            v.colonyId = n.colonyId;
            v.age = (uint16_t)n.age;
            v.flags = (uint16_t)((n.alive ? 1u : 0u) | ((uint32_t)n.type << 1));
            v.srcIdx = (uint32_t)idx;   // nodes_ 原始下标，供连接重映射
            nodes.push_back(v);
        }
        ++idx;
    }
    for (const CyanoLink& l : cy.links()) rawLinks.push_back({l.a, l.b});
    // sunLevel 语义：**世界中心处**的完整阳光解（昼夜 + 垂直梯度 + 局部扰动），
    // 由 World::sunLevel() 定义（World.cpp），非全局昼夜系数。
    sun = (float)world.sunLevel();

    // ⚠️ CyanoLink.a/b 语义已核实（src/Cyanobacteria.h）：存的是 nodes_ **数组下标**
    //    （注释「a < b（nodes_ 数组下标）」），不是稳定 id —— 所以上面直接把 l.a/l.b
    //    放进 rawLinks，由 mapLinkEndpoints() 把「nodes_ 下标」重映射为「快照下标」
    //    （存活过滤后两者不同）；不要按稳定 id 查表。
    //
    // 关于 cyanoLinkCount 的语义一致性：
    //   rawLinks 含「指向已死节点」的连接（捕食在本帧标死、移除推迟到下一次 step
    //   开头，见 Cyanobacteria.cpp 的帧内瞬态），这些会被 mapLinkEndpoints() 丢弃
    //   （端点不在存活节点集合中）→ 最终快照里的连接数**恰好等于 aliveLinkCount()**，
    //   与头部 68 字节语义一致。
}
}  // namespace

WebRenderer::WebRenderer(const World& world) {
    buildSnapshot(world);
}

void WebRenderer::render(const World& world) {
    buildSnapshot(world);
}

std::vector<uint8_t> WebRenderer::snapshotBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
}

void WebRenderer::buildSnapshot(const World& world) {
    const auto& balls = world.balls();
    const auto& nuclei = world.nuclei();
    const WorldConfig& cfg = world.config();

    // 蓝细菌数据先在锁外取好（WS4 未落盘时为关闭态）。
    std::vector<CyanoNodeView> cyanoNodes;
    std::vector<CyanoLinkRaw> cyanoRawLinks;
    float cyanoAvgEnergy = 0.0f;
    float sunLevel = 1.0f;
    fetchCyanoSnapshot(world, cyanoNodes, cyanoRawLinks, sunLevel);

    // 原始连接（nodes_ 数组下标语义）→ 快照内数组下标（a < b，两端必须存活）。
    std::vector<CyanoLinkView> cyanoLinks;
    mapLinkEndpoints(cyanoNodes, cyanoRawLinks, cyanoLinks);

    // 平均能量直接由写入快照的存活节点计算，保证与记录逐条自洽（无节点 = 0）。
    if (!cyanoNodes.empty()) {
        double sum = 0.0;
        for (const CyanoNodeView& n : cyanoNodes) sum += (double)n.energy;
        cyanoAvgEnergy = (float)(sum / (double)cyanoNodes.size());
    } else {
        cyanoAvgEnergy = 0.0f;
    }

    // 局部缓冲区构建（不持锁），最后一次 swap 发布，供 /state 线程读取。
    std::vector<uint8_t> out;
    out.resize(kHeaderSize + balls.size() * kBallRecSize + nuclei.size() * kNucleusRecSize +
               cyanoNodes.size() * kCyanoNodeRecSize + cyanoLinks.size() * kCyanoLinkRecSize);

    // ---- 头部 80 字节 ----
    writeU8(out, 0, kMagic);
    writeU8(out, 1, kVersion);
    writeU8(out, 2, world.finished() ? 1 : 0);
    writeU8(out, 3, 0);
    writeU32(out, 4, (uint32_t)world.frame());
    writeF32(out, 8, (float)cfg.width);
    writeF32(out, 12, (float)cfg.height);
    writeU32(out, 16, (uint32_t)balls.size());
    writeU32(out, 20, (uint32_t)world.aliveNucleusCount());

    int typeCount[3] = {0, 0, 0};
    for (const Ball& b : balls) {
        ++typeCount[(size_t)b.type];
    }
    writeU32(out, 24, (uint32_t)typeCount[0]);
    writeU32(out, 28, (uint32_t)typeCount[1]);
    writeU32(out, 32, (uint32_t)typeCount[2]);

    std::string reason = world.finishReason() ? world.finishReason() : "";
    for (size_t i = 0; i < kFinishReasonLen; ++i) {
        out[36 + i] = i < reason.size() ? (uint8_t)reason[i] : 0;
    }

    writeU32(out, 64, (uint32_t)(int32_t)cyanoNodes.size());   // cyanoNodeCount
    writeU32(out, 68, (uint32_t)(int32_t)cyanoLinks.size());   // cyanoLinkCount
    writeF32(out, 72, cyanoAvgEnergy);                          // cyanoAvgEnergy
    writeF32(out, 76, sunLevel);                                // sunLevel

    // ---- 球记录 ×ballCount（6 字节）----
    const double sx = (double)65535.0 / cfg.width;
    const double sy = (double)65535.0 / cfg.height;
    size_t off = kHeaderSize;
    for (const Ball& b : balls) {
        uint16_t x = (uint16_t)(b.pos.x * sx);
        uint16_t y = (uint16_t)(b.pos.y * sy);
        uint32_t ownerBits = 0;
        if (b.ownerId >= 0 && b.ownerId < (int)nuclei.size()) {
            ownerBits = (uint32_t)(b.ownerId + 1);
        }
        if (ownerBits > 16383) ownerBits = 16383;
        uint16_t meta = (uint16_t)((ownerBits << 2) | (uint32_t)b.type);
        writeU16(out, off, x);
        writeU16(out, off + 2, y);
        writeU16(out, off + 4, meta);
        off += kBallRecSize;
    }

    // ---- 核记录 ×aliveNucleusCount（32 字节）----
    for (const Nucleus& n : nuclei) {
        if (!n.alive) continue;
        writeF32(out, off, (float)n.pos.x);
        writeF32(out, off + 4, (float)n.pos.y);
        writeF32(out, off + 8, (float)n.energy);
        writeF32(out, off + 12, (float)n.params.attackStrength);
        writeF32(out, off + 16, (float)n.params.maxSpeed);
        writeF32(out, off + 20, (float)n.params.energyThreshold);
        writeU32(out, off + 24, (uint32_t)n.id);
        writeF32(out, off + 28, (float)n.hue);  // 0..360 遗传色相（WS3 已落盘）
        off += kNucleusRecSize;
    }

    // ---- 蓝细菌节点 ×cyanoNodeCount（24 字节）----
    for (const CyanoNodeView& n : cyanoNodes) {
        writeF32(out, off, n.x);
        writeF32(out, off + 4, n.y);
        writeF32(out, off + 8, n.energy);
        writeU32(out, off + 12, n.id);
        writeU16(out, off + 16, n.linkCount);
        writeU16(out, off + 18, n.colonyId);
        writeU16(out, off + 20, n.age);
        writeU16(out, off + 22, n.flags);
        off += kCyanoNodeRecSize;
    }

    // ---- 蓝细菌连接 ×cyanoLinkCount（8 字节，快照内数组下标，a < b）----
    for (const CyanoLinkView& l : cyanoLinks) {
        writeU32(out, off, l.a);
        writeU32(out, off + 4, l.b);
        off += kCyanoLinkRecSize;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    bytes_.swap(out);
}

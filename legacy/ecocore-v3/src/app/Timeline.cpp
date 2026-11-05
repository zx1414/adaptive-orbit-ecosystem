// agentsim - app/Timeline.cpp（R5-C 时间轴实现）
#include "app/Timeline.h"

#include <cstring>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <istream>
#include <ostream>
#include <utility>

#include "app/SaveStore.h"
#include "sim/Simulation.h"

namespace agentsim {
namespace app {
namespace {
namespace tl {

    constexpr std::uint32_t kMagic = 0x41544D4Cu;  // "ATML"
    constexpr std::uint32_t kVersion = 1u;
    
    void wU32(std::ostream& os, std::uint32_t v) {
        unsigned char b[4] = {static_cast<unsigned char>(v & 0xFFu),
                              static_cast<unsigned char>((v >> 8) & 0xFFu),
                              static_cast<unsigned char>((v >> 16) & 0xFFu),
                              static_cast<unsigned char>((v >> 24) & 0xFFu)};
        os.write(reinterpret_cast<const char*>(b), 4);
    }
    
    void wU64(std::ostream& os, std::uint64_t v) {
        unsigned char b[8];
        for (int i = 0; i < 8; ++i) b[i] = static_cast<unsigned char>((v >> (i * 8)) & 0xFFu);
        os.write(reinterpret_cast<const char*>(b), 8);
    }
    
    bool rU32(std::istream& is, std::uint32_t& v) {
        unsigned char b[4];
        if (!is.read(reinterpret_cast<char*>(b), 4)) return false;
        v = static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
            (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
        return true;
    }
    
    bool rU64(std::istream& is, std::uint64_t& v) {
        unsigned char b[8];
        if (!is.read(reinterpret_cast<char*>(b), 8)) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(b[i]) << (i * 8);
        return true;
    }
    
    
} // namespace tl
} // namespace

Timeline::Timeline(SimConfig cfg) : cfg_(std::move(cfg)) {}

// ---- 抽稀（任务卡：内存上限 2000，超出抽稀、保首尾）----
// 策略（确定性）：kMaxCheckpoints 上限 2000 是**硬顶**；触发时先隔一去一
//（保留下标 0,2,4,...，容量减半），若仍超限（interval 配置远小于帧推进速度时
// 可能一次涌入大量检查点）再按"超出量从中间均匀抽取"丢弃，任何时刻都保证
// checkpoints_.front()（最老）与 back()（最新）不被丢弃——"保首尾"。
// 复杂度：摊还 O(n) per thin；触发频率随容量减半指数下降，整体摊还 O(1)/帧。
void Timeline::thin() {
    if (checkpoints_.size() <= kMaxCheckpoints) return;
    // 第一档：隔一去一（保留偶数下标）。
    if (checkpoints_.size() / 2 + 1 <= kMaxCheckpoints) {
        std::vector<Checkpoint> kept;
        kept.reserve(checkpoints_.size() / 2 + 1);
        for (std::size_t i = 0; i < checkpoints_.size(); i += 2) {
            kept.push_back(std::move(checkpoints_[i]));
        }
        checkpoints_.swap(kept);
        if (checkpoints_.size() <= kMaxCheckpoints) return;
    }
    // 第二档：仍超限，从中间均匀抽掉 excess 个（首尾绝不进丢弃集）。
    const std::size_t excess = checkpoints_.size() - kMaxCheckpoints;
    const std::size_t inner = checkpoints_.size() - 2;  // 首尾之外的候选数
    std::vector<bool> drop(inner, false);
    for (std::size_t k = 0; k < excess && inner > 0; ++k) {
        // 确定性均匀分布：候选区内等距取样。
        const std::size_t idx = (k * inner) / excess;
        drop[idx] = true;
    }
    std::vector<Checkpoint> kept;
    kept.reserve(kMaxCheckpoints);
    kept.push_back(std::move(checkpoints_.front()));
    for (std::size_t i = 0; i < inner; ++i) {
        if (!drop[i]) kept.push_back(std::move(checkpoints_[i + 1]));
    }
    kept.push_back(std::move(checkpoints_.back()));
    checkpoints_.swap(kept);
}

void Timeline::maybeCheckpoint(const Simulation& sim, std::uint64_t tick,
                               std::uint64_t hash) {
    if (interval_ == 0) return;
    if (tick % interval_ != 0) return;
    Checkpoint cp;
    cp.frame = tick;
    cp.hash = hash;
    // 全量快照到内存（含 rng 状态——seek 后逐位续跑的前提）。
    // serializeState 写入内存串；哈希在捕获时由调用方提供（写侧世界快照）。
    {
        std::ostringstream os;
        if (sim.serializeState(os)) {
            const std::string& s = os.str();
            cp.payload.assign(s.begin(), s.end());
        } else {
            return;  // 快照失败：跳过本检查点（世界仍在跑，不打断主循环）
        }
    }
    checkpoints_.push_back(std::move(cp));
    thin();
}

bool Timeline::seek(std::uint64_t frame, Simulation& sim, std::string& err) {
    err.clear();
    const Checkpoint* best = nullptr;
    // 线性扫描（容器有序，可二分；2000 条线性已足够快，二分收益可忽略）。
    for (const Checkpoint& cp : checkpoints_) {
        if (cp.frame <= frame) best = &cp;
        else break;
    }
    if (best == nullptr) {
        err = "no checkpoint at or before frame " + std::to_string(frame) +
              " (timeline size " + std::to_string(checkpoints_.size()) + ")";
        return false;
    }
    std::string payload(best->payload.begin(), best->payload.end());
    std::istringstream is(payload);
    if (!sim.deserializeState(is, err)) {
        err = "checkpoint restore failed: " + err;
        return false;
    }
    // 损坏检测：恢复后的世界指纹必须与捕获时一致。
    if (sim.stateHash() != best->hash) {
        err = "checkpoint hash mismatch at frame " + std::to_string(best->frame) +
              " (payload corrupt or serialized format drifted)";
        return false;
    }
    return true;
}

void Timeline::branch(const SimConfig& newCfg) {
    // 语义契约见 Timeline.h：只换运行参数；世界几何/种子强制保留旧值；
    // 实体状态本函数一概不碰（调用方决定是否 restartClock）。
    SimConfig merged = newCfg;
    merged.world = cfg_.world;
    cfg_ = merged;
    checkpoints_.clear();   // 新世界线的过去不可用旧时间轴回看
    branched_ = true;
}

bool Timeline::exportTo(const std::string& path, std::string& err) const {
    err.clear();
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os.is_open()) { err = "cannot open " + path; return false; }
    tl::wU32(os, tl::kMagic);
    tl::wU32(os, tl::kVersion);
    tl::wU64(os, interval_);
    const unsigned char branched = branched_ ? 1u : 0u;
    os.put(static_cast<char>(branched));
    for (int i = 0; i < 7; ++i) os.put('\0');
    std::string cfgErr;
    if (!savedetail::writeCfgBlock(os, cfg_, cfg_.policy.name, cfgErr)) {
        err = "cfg block write failed: " + cfgErr;
        return false;
    }
    tl::wU64(os, checkpoints_.size());
    for (const Checkpoint& cp : checkpoints_) {
        tl::wU64(os, cp.frame);
        tl::wU64(os, cp.hash);
        tl::wU64(os, cp.payload.size());
        os.write(reinterpret_cast<const char*>(cp.payload.data()),
                 static_cast<std::streamsize>(cp.payload.size()));
    }
    os.close();
    if (os.fail()) { err = "write failed on " + path; return false; }
    return true;
}

bool Timeline::importFrom(const std::string& path, std::string& err) {
    err.clear();
    std::ifstream is(path, std::ios::binary);
    if (!is.is_open()) { err = "cannot open " + path; return false; }
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!tl::rU32(is, magic) || !tl::rU32(is, version)) { err = "cannot read header"; return false; }
    if (magic != tl::kMagic) { err = "bad magic (not ATML)"; return false; }
    if (version != tl::kVersion) {
        err = "unsupported timeline version: " + std::to_string(version) +
              " (expected 1)";
        return false;
    }
    std::uint64_t interval = 0;
    if (!tl::rU64(is, interval)) { err = "cannot read interval"; return false; }
    unsigned char branched = 0;
    if (!is.get(reinterpret_cast<char&>(branched))) { err = "cannot read flags"; return false; }
    unsigned char pad[7];
    if (!is.read(reinterpret_cast<char*>(pad), 7)) { err = "cannot read pad"; return false; }
    std::string pn;
    if (!savedetail::readCfgBlock(is, cfg_, pn, err)) {
        err = "cfg block read failed: " + err;
        return false;
    }
    // timeline 块自带权威 policyName（exportTo 写入 cfg_.policy.name）。
    if (!pn.empty()) cfg_.policy.name = pn;
    std::uint64_t count = 0;
    if (!tl::rU64(is, count) || count > (1u << 20)) {
        err = "bad checkpoint count"; return false;
    }
    std::vector<Checkpoint> loaded;
    loaded.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        Checkpoint cp;
        std::uint64_t len = 0;
        if (!tl::rU64(is, cp.frame) || !tl::rU64(is, cp.hash) || !tl::rU64(is, len) ||
            len > (1u << 30)) {
            err = "cannot read checkpoint record"; return false;
        }
        cp.payload.resize(static_cast<std::size_t>(len));
        if (len > 0 &&
            !is.read(reinterpret_cast<char*>(cp.payload.data()),
                     static_cast<std::streamsize>(len))) {
            err = "cannot read checkpoint payload"; return false;
        }
        loaded.push_back(std::move(cp));
    }
    interval_ = interval;
    branched_ = (branched != 0u);
    checkpoints_ = std::move(loaded);
    return true;
}

} // namespace app
} // namespace agentsim

// agentsim - perception/AgentSensors.cpp
// 24 维传感器填充。R1-B：只读 AgentEnvironment（环面几何）+ 调用方传入的
// 蓝细菌节点容器（唯一食物来源）+ TemperatureField，不写任何状态。
// 噪声由调用方传入的 PCG32 产生；rng == nullptr 表示无噪声（确定性路径）。
// G2 修复：读噪声直接叠加在决策实际消费的维上（方向 s[0..5]、接近度 s[6..8]、
// 距离 s[9]），幅度 amp = kReadNoiseK * speed/(1+speed) 随速度单调递增（S6 读通道）。
// s[14] 写本帧读噪声幅度 amp 本身（元信息维，非随机数）；s[15] 恒 0 保留。
// S16 修复：读噪声偏移按候选独立抽取（旧版整帧一组 (ax,ay,ap) 三候选共享，假噪声）。
// P2-8 修复：s[9]（最近蓝细菌归一化距离）不再复用候选 0 的接近度偏移 a0p——
//   那是"距离维吃接近度噪声"的串维 bug。现在 s[9] 独立抽 1 次偏移（第 4 组
//   随机数）：有候选且 amp>0 时每帧至多 9（三候选 x3）+1（s[9]）= 10 次
//   nextDouble()；amp==0 或 rng==nullptr 时一次都不抽（零副作用，S16 测试守着）。
// S17：感知半径一律走 effectiveSenseRadius(cfg.senseRadius, scoutCount)——
//   侦察球是菌落对外感知的唯一来源；资源球/护盾球零影响（签名无入口）。
// R1-B：候选来自 cyanoNodes（按环距升序取最近 3 个存活节点）——与旧 food 版
//   同构的"取前 3 候选"语义；节点容器顺序 = CyanoField 内部下标（确定性）。
//
// R2 追加（V2-SPEC-AMEND-1）：尾部 6 个新维 s[18..23]，全部为**无 rng 副作用**
// 的纯归一化（不消耗随机数、不加噪声——社会/施力感知走"确定性读数"通道，
// 噪声只污染觅食通道，保持 S16/S17 的 rng 消耗序不变）：
//   s[18] = 最近邻距离 / sightRadius（clamp01；无邻居恒 1）
//   s[19..20] = 邻居单位方向（防御性再归一化；无邻居恒 0 —— 方向零向量 = 哨兵）
//   s[21] = clamp01(energyRatio)（能量比；非有限输入清洗为 0）
//   s[22] = targets.count / kMaxForceTargets
//   s[23] = targets.items[0].dist / R（clamp01；count==0 恒 1）
// s[0..17] 的计算路径与旧 18 维版逐位一致（写入序、rng 消耗序均不变）。
#include "perception/AgentSensors.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/PCG32.h"

namespace agentsim {
namespace {

// 读噪声幅度系数。PolicyConfig 目前没有 readNoiseK 字段（Config.h 已核对），
// 按任务约定硬编码 0.15；等 Config.h 增加字段后改为 cfg.readNoiseK。
constexpr double kReadNoiseK = 0.15;

// GOAL：温度失真对读噪声的放大增益。d=1（完全失真）时读噪声 x4；
// 温度偏离最适越远感知越不准。20000 tick 生态验证 3.0 不引发生态崩溃。
constexpr double kTempGain = 3.0;

// 读噪声幅度：amp = k * speed/(1+speed)，speed>=0 时单调递增、上界 k、speed=0 时为 0（S6）。
double readNoiseAmp(double speed) {
    if (speed < 0.0) speed = 0.0;
    return kReadNoiseK * speed / (1.0 + speed);
}

double clamp01(double v) {
    if (!(v > 0.0)) return 0.0; // NaN/负值归 0
    if (v > 1.0) return 1.0;
    return v;
}

// 把候选 i 的读数向"本帧随机抖动"方向做 amp 失真：读数被速度污染（规格语义）。
void perturbCandidate(SensorVector& s, int i, double ax, double ay, double ap) {
    s[2 * i + 0] += ax;
    s[2 * i + 1] += ay;
    double prox = s[6 + i] + ap;
    if (prox < 0.0) prox = 0.0;
    if (prox > 1.0) prox = 1.0;
    s[6 + i] = prox;
}

} // namespace

// S17：有效感知半径（公开纯函数）。无侦察球退到"原生盲区"（基准半径的 1/4，
// 核保留最基本的接触感知，不至于完全瞎）；有 n 个侦察球按 sqrt(1+n) 扩张
//（边际递减），上限 4 倍基准半径。资源球/护盾球在此函数中没有入口——
// 它们对感知零影响是构造性成立的（S17 用户纠正过的语义）。
double effectiveSenseRadius(double baseRadius, int scoutCount) {
    if (scoutCount <= 0) return baseRadius * 0.25;
    const double r = baseRadius * std::sqrt(1.0 + static_cast<double>(scoutCount));
    const double cap = baseRadius * 4.0;
    return r > cap ? cap : r;
}

SensorVector sense(const AgentEnvironment& env, Vec2 self, Vec2 vel, double speed,
                   const PolicyConfig& cfg, PCG32* rng,
                   const std::vector<CyanoNode>& cyanoNodes,
                   const TemperatureField& temp, const Terrain* terrain,
                   int scoutCount,
                   const NeighborInfo& nearest, double energyRatio,
                   const TargetList& targets) {
    SensorVector s{};

    const double w = env.config().world.width;
    const double h = env.config().world.height;

    // S17：本帧有效感知半径。查询半径与全部归一化分母统一用它，
    // 不再直接用 cfg.senseRadius（scoutCount<=0 时 = 1/4 原生盲区）。
    const double R = effectiveSenseRadius(cfg.senseRadius, scoutCount);

    // ---- [0..9] 蓝细菌特征（R1-B）：半径内存活节点，按最小环距升序取最近 3 个 ----
    struct Cand {
        Vec2 delta{};  // self -> 节点 的最小环距位移
        double dist = 0.0;
    };
    Cand cand[3];
    int candN = 0;
    // 单遍"最近 3 候选"维护（插入排序，候选数 <= 3；节点按容器下标顺序扫描，
    // 距离相等时先扫到的优先——确定性红线下标序 tie-break）。
    for (const CyanoNode& node : cyanoNodes) {
        if (!node.alive) continue;
        Cand c;
        c.delta = env.torusDelta(self, node.pos);
        c.dist = length(c.delta);
        if (!(c.dist <= R)) continue; // 半径外不进入候选（<=：贴边算在内）
        // 插入到 cand[0..candN)（升序；扫描序即 tie-break 序）。
        int pos = candN < 3 ? candN : 2;
        while (pos > 0 && c.dist < cand[pos - 1].dist) {
            if (pos < 3) cand[pos] = cand[pos - 1];
            --pos;
        }
        if (pos < 3) {
            cand[pos] = c;
            if (candN < 3) ++candN;
        }
    }

    // 本帧读噪声幅度（S6 读通道）：speed=0 时 amp=0，随速度单调递增。
    // GOAL 温度接线：amp 乘以 (1 + kTempGain*d)，d = temp.distortion(self)（同 s[16]）。
    // d==0 或 speed==0（本底 amp 为 0）时严格保持原值：温度失真只放大既有噪声。
    double amp = 0.0;
    const double d = temp.distortion(self);
    if (rng != nullptr) {
        amp = readNoiseAmp(speed) * (1.0 + kTempGain * d);
    }
    // S16：随机偏移在候选循环内逐候选独立抽取（本帧第 1..3 组随机数）。
    const double r = R; // 归一化分母 = 有效感知半径（S17）
    for (int i = 0; i < 3; ++i) {
        if (i < candN && cand[i].dist > 0.0) {
            // [0..5] 最近 3 个蓝细菌节点的相对环距方向（单位向量），x/y 交错。
            const Vec2 dir = Vec2{cand[i].delta.x / cand[i].dist, cand[i].delta.y / cand[i].dist};
            s[2 * i + 0] = dir.x;
            s[2 * i + 1] = dir.y;
            // [6..8] 归一化接近度 1 - dist/R：越近越大，有效半径外 <=0。
            double prox = 1.0 - cand[i].dist / r;
            prox = std::max(0.0, std::min(1.0, prox));
            s[6 + i] = prox;
            // S16：每个候选独立抽取 U(-amp,+amp) 偏移；amp==0（speed==0）或
            // rng==nullptr 时不抽，两条路径的 rng 消耗量保持为 0（逐位零副作用）。
            double ax = 0.0, ay = 0.0, ap = 0.0;
            if (amp > 0.0 && rng != nullptr) {
                ax = amp * (2.0 * rng->nextDouble() - 1.0);
                ay = amp * (2.0 * rng->nextDouble() - 1.0);
                ap = amp * (2.0 * rng->nextDouble() - 1.0);
            }
            // G2：读噪声叠加到有语义的维——方向与接近度同时失真，决策真实吃到干扰。
            perturbCandidate(s, i, ax, ay, ap);
        }
        // 不足 3 个的槽位保持 0（SensorVector 默认零初始化）。
    }
    // C5: 读噪加扰后的方向对 s[0..5] 逐一重新归一化（长度归 1，零向量保持零）——
    // "方向是单位向量"的契约被决策层按点积消费（RulePolicy 对齐项），不得被噪声破坏。
    for (int i = 0; i < 3; ++i) {
        const double dx = s[2 * i + 0];
        const double dy = s[2 * i + 1];
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.0) {
            s[2 * i + 0] = dx / len;
            s[2 * i + 1] = dy / len;
        } else {
            s[2 * i + 0] = 0.0;
            s[2 * i + 1] = 0.0;
        }
    }
    // [9] 最近蓝细菌距离的归一化 dist/R：越近越小；无节点为 0。
    // P2-8：读噪声独立抽取（本帧第 4 组随机数，不再复用候选 0 的 ap）：
    // 距离维有自己的噪声通道，与接近度噪声统计独立。仅在有候选时抽取——
    // 无候选时该维语义是"没有目标"，恒 0 不加噪（amp==0/rng==nullptr 同样不抽）。
    s[9] = (candN > 0) ? std::min(1.0, cand[0].dist / r) : 0.0;
    if (candN > 0 && amp > 0.0) {
        s[9] += amp * (2.0 * rng->nextDouble() - 1.0);
        s[9] = std::max(0.0, std::min(1.0, s[9]));
    }

    // ---- [10..12] 自身速度特征：统一用 1/(1+speed) 作尺度（无最大速度字段）。----
    const double scale = 1.0 / (1.0 + speed);
    // [10..11] 速度分量（缩放后，保留方向信息）。
    s[10] = vel.x * scale;
    s[11] = vel.y * scale;
    // [12] 速度大小归一化 = speed/(1+speed)，恰为 [10..11] 的模长。
    s[12] = speed * scale;

    // ---- [13] 距最近边界的归一化距离（按较小半边长归一，夹到 [0,1]）。----
    // 环面拓扑下边界只是坐标卷绕缝，此维是占位的空间位置特征。
    if (w > 0.0 && h > 0.0) {
        const double dx = std::min(self.x, w - self.x);
        const double dy = std::min(self.y, h - self.y);
        const double halfMin = 0.5 * std::min(w, h);
        s[13] = std::max(0.0, std::min(1.0, std::min(dx, dy) / halfMin));
    }

    // ---- [14] 元信息维：本帧读噪声幅度 amp 本身（不再写随机数，G2）。
    // amp 只由 speed 决定，speed=0 时恒为 0；决策层可读取它估计读数可信度。
    // amp 与 rng 无关，保持确定性路径（rng==nullptr）下 s[14] 也可用。
    s[14] = amp;
    // [15] 保留维，恒 0。
    s[15] = 0.0;
    // ---- [16][17] 温度维（GOAL）：该点失真度与归一化温度采样，均 ∈ [0,1]。
    // T43：地形温度区以 temperatureOffset 叠加到采样值（失真路径保持原样，
    // 见头文件注释）。terrain==nullptr 时表达式与原实现逐位一致。
    s[16] = d;
    s[17] = (terrain != nullptr)
                ? clamp01(temp.sample(self) + terrain->temperatureOffset(self))
                : temp.sample(self);

    // ================= R2：社会/施力感知 s[18..23]（确定性尾部，无 rng 副作用） =================
    // 哨兵约定（决策层依赖，勿改）：
    //   s[18] == 1 && s[19] == 0 && s[20] == 0  <=>  无邻居（或邻居在视距外）
    //   s[23] == 1                              <=>  无可施力球（count==0）
    if (nearest.found && nearest.sightRadius > 1e-12) {
        // [18] 邻居距离占基因视距的比例（>1 = 视野外，clamp 到 1；NaN/0 分母防御）。
        s[18] = clamp01(std::fabs(nearest.dist) / nearest.sightRadius);
        // [19..20] 邻居方向：组合根已给环面单位方向；感知层防御性再归一化，
        // 长度退化（<1e-12 或非有限）时写零向量 = "无邻居"哨兵语义。
        double nx = nearest.dirX;
        double ny = nearest.dirY;
        if (!std::isfinite(nx)) nx = 0.0;
        if (!std::isfinite(ny)) ny = 0.0;
        const double nlen = std::sqrt(nx * nx + ny * ny);
        if (nlen > 1e-12) {
            s[19] = nx / nlen;
            s[20] = ny / nlen;
        }
    } else {
        // 无邻居 / 无信息（found=false 或视距退化）：s[18] 恒 1（契约："1 = 无邻居
        // 或超出视野"——"看不见"与"在视距边缘外"同读数），方向保持零向量哨兵。
        s[18] = 1.0;
    }
    // [21] 能量比 ∈ [0,1]（容量归一由调用方完成；非有限/负值归 0）。
    s[21] = clamp01(energyRatio);
    // [22] 可施力球密度：count / 8（恒 ∈ [0,1]；count 由契约保证 <= 8，防御钳制）。
    {
        int cnt = targets.count;
        if (!(cnt > 0)) cnt = 0;           // NaN 不可能（int），负值防御归 0
        if (cnt > kMaxForceTargets) cnt = kMaxForceTargets;
        s[22] = static_cast<double>(cnt) / static_cast<double>(kMaxForceTargets);
    }
    // [23] 最近可施力球距离 / 有效感知半径；无目标恒 1（"没有可施力球"哨兵）。
    if (targets.count > 0 && R > 1e-12) {
        s[23] = clamp01(std::fabs(targets.items[0].dist) / R);
    } else {
        s[23] = 1.0;
    }

    return s;
}

} // namespace agentsim

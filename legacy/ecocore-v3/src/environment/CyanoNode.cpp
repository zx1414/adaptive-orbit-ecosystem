// agentsim - environment/CyanoNode.cpp
// 蓝细菌节点场实现（S22 上半，R1-B 补全）。RNG 消耗顺序的硬性约定见 CyanoNode.h 文件头。
//
// 公式与选型声明（照任务规格，不自创）：
// - 光合（Michaelis-Menten 饱和型，R3-A 起带自遮光）：
//     localSun = sunLevel * exp(-shadeK * shade_[i])
//     gain = photosynthesisRate * localSun / (localSun + lightHalfSaturation)
//   shade_[i] = Σ_{j≠i, alive, d<shadeRadius} exp(-d²/(2σ²))，由 linkTransfer()
//   的 O(n²) 邻居扫描顺带累计并缓存（热路径零新增扫描；退化配置走仅遮蔽
//   分支）。step() 的光合读取上一结算帧的缓存（一帧滞后，见 shadeCache()）。
//   shadeK=0 且 shade_=0 时 exp(-0)=1，与旧的全局阳光公式逐位一致。
//   sunLevel <= 0 时短路取 0：分母为零的表达式永不求值（不出 NaN）。
//   进账口径：只计真正存进节点的部分——受 energyCap 余量约束（credited gain），
//   被上限截断的部分不计入 report.photosynthesisGain；预置超限能量不产生负入账。
// - 代谢：本实现选用【线性常数型】 loss = metabolismRate（每 tick 固定耗散）。
//   备选的能量加权型 metabolismRate * (1 + energy / energyCap)（大节点更耗）未采用：
//   常数型使收支与当前能量解耦，便于账本级守恒核查，也贴合基础呼吸直觉。
// - 能量上限：energy = min(energy, energyCap)，仅截 surplus，不产生任何 gain。
// - 产球：判定通过且 energy >= ballSpawnCost 时扣 ballSpawnCost，
//   report.ballEnergyTotal += ballSpawnCost * ballRadius（自由球初始能量）。
// - 分裂：判定通过且 energy >= reproductionCost 时亲代扣 reproductionCost，
//   子代初能 = reproductionCost * 0.5（能量平分），另一半作为耗散计入
//   report.reproductionLoss。子代位置 = 亲代位置 + 偏移后环面 wrap；若与最近
//   存活同类（含亲代）的最小环距 < minSeparation 则放弃：不扣能量、不 births++，
//   但判定与位置两次 rng 抽取已消耗——判定先于位置的固定消耗序所要求，有意为之。
//   R1-B：子代 hue = 亲代 hue + hueJitterSigma * 高斯样本（Box-Muller 极法，
//   单侧双掷：每父代恰消耗一次 U 抽取，见头文件），wrap 到 [0,360)。
// - 环面几何：复用 core/Vec2.h 的 torusDelta/torusDistance（不依赖 AgentEnvironment）。
// - R8/P2-1（GOAL §2.2.1 ③）：产球位置改为"产者附近随机"——距产者
//   [ballSpawnMinDist, ballSpawnMaxDist] 环面距离、方向均匀随机，wrap 回环面内。
// - R8/P2-2（GOAL §2.2.1 ④）：连线传输按 linkCheckInterval 节流——仅在
//   frameCount_ % linkCheckInterval == 0 的帧调用 linkTransfer()。N=1（默认）
//   与旧行为逐位一致；N>1 时是**整体跳过的简化语义**：跳过帧既不重算拓扑、
//   也不结算能量传输（links() 拓扑与能量口径同步保持上一结算帧快照）。

#include "CyanoNode.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace agentsim {

namespace {
constexpr double kTwoPi = 6.283185307179586;
// 环绕单位：色相 ∈ [0,360)。
constexpr double kHueFull = 360.0;
// 高斯样本的地板（防 U=0 时 log(0) = -inf）：Box-Muller 输入下界。
constexpr double kGaussEps = 1e-12;
// R7-C 子任务 3：分裂半径两段分布（空间异质性折中）。
// R3-A 实测数据：offset=300（全量远抛）群落破碎加速灭绝；offset=125（全量近聚）
// 单向聚簇过死。折中：70% 子代落在父附近 [0, offset*0.5]（局域密度/聚落形态
// 维持），30% 落在远处 [offset*0.5, offset]（允许开拓）。纯确定性映射：仍恰
// 消耗 1 次 nextDouble（半径），rng 消耗序与消耗量逐位不变。
constexpr double kReproNearFraction = 0.7; // 近段概率质量
constexpr double kReproNearSpan = 0.5;     // 近段半径跨度（占 offset 比例）

// 双射 [0,1) -> [-1,1)：把 Box-Muller 输入折返（x>1/2 取 1-x），输出恒正。
double foldCentered(double u) {
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    return 2.0 * (u < 0.5 ? u : 1.0 - u);
}

double wrapHue(double h) {
    double r = std::fmod(h, kHueFull);
    if (r < 0.0) r += kHueFull;
    if (r >= kHueFull) r -= kHueFull; // 数值容错
    return r;
}
} // namespace

CyanoField::CyanoField(const CyanoParams& p) : params_(p) {}

double CyanoField::cyanoConversionRate(int mode, double baseRate, double exponent,
                                       double speedRef, double accelRef,
                                       double wSpeed, double wAccel,
                                       double minRate, double maxRate,
                                       double maxSpeed, double maxAccel) {
    (void)exponent; // R1-B 占位：惩罚指数预留（=1 时与 v2.3.1 线性比式逐位一致）。
    if (!(baseRate > 0.0)) return 0.0; // 非正基率：无转化（防 NaN/负值传播）
    const bool okSpeed = maxSpeed > 0.0 && speedRef > 0.0;
    const bool okAccel = maxAccel > 0.0 && accelRef > 0.0;
    double rate = baseRate;
    if (mode == 1) {
        // 速度惩罚：越快越难抓。
        if (okSpeed) rate = baseRate * (speedRef / (speedRef + maxSpeed));
    } else if (mode == 2) {
        // 加速度惩罚：机动越强越难抓。
        if (okAccel) rate = baseRate * (accelRef / (accelRef + maxAccel));
    } else if (mode == 3) {
        // 混合：两项连乘（权重缺省 1；退化项取 1 不惩罚）。
        const double ps = okSpeed ? wSpeed * speedRef / (speedRef + maxSpeed) : 1.0;
        const double pa = okAccel ? wAccel * accelRef / (accelRef + maxAccel) : 1.0;
        rate = baseRate * ps * pa;
    }
    // mode 0 / 未知：rate = baseRate（常数）。
    if (minRate < maxRate) {
        if (rate < minRate) rate = minRate;
        if (rate > maxRate) rate = maxRate;
    }
    return rate;
}

void CyanoField::seed(int count, double worldW, double worldH, PCG32& rng) {
    worldW_ = worldW; // 环面尺寸快照（连线传输 linkTransfer 使用）
    worldH_ = worldH;
    frameCount_ = 0;  // R8/P2-2：重新播种即重置连线节流相位
    links_.clear();   // 新拓扑从零开始（首次 linkTransfer 前为空图）
    nodes_.clear();
    nodes_.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
    if (count <= 0) return; // 空场：零 rng 消耗（与旧版一致）

    // R7-C 聚落播种：参数退化（无聚落/无半径）时走旧均匀撒点（回归逐位）。
    // 语义照旧版 v2.3.1 Cyanobacteria::initialize：cyanoColonies 个聚落中心
    // （全图均匀），count 个节点轮转分配（i%colonies）绕中心随机方向撒点，
    // 半径 clusterSpread*sqrt(u)（圆盘内均匀）——真实植被的斑块状分布，
    // 自遮光负反馈生效的空间前提（均匀撒点下遮蔽几乎无效，R3-A 实测）。
    const int colonies = params_.cyanoColonies > 0 && params_.clusterSpread > 0.0
                             ? params_.cyanoColonies
                             : 0;
    if (colonies > 0) {
        // rng 消耗序：每聚落 2 抽（cx, cy）-> 每节点 3 抽（角度, 半径, hue）。
        std::vector<Vec2> centers(static_cast<std::size_t>(colonies));
        for (int i = 0; i < colonies; ++i) {
            centers[static_cast<std::size_t>(i)] =
                wrap(Vec2{rng.nextDouble() * worldW, rng.nextDouble() * worldH},
                     worldW, worldH);
        }
        for (int i = 0; i < count; ++i) {
            CyanoNode node;
            node.id = nextId_++;
            const Vec2& c = centers[static_cast<std::size_t>(i % colonies)];
            const double ang = rng.nextDouble() * kTwoPi;
            const double rad = params_.clusterSpread * std::sqrt(rng.nextDouble());
            node.pos = wrap(Vec2{c.x + rad * std::cos(ang),
                                 c.y + rad * std::sin(ang)},
                            worldW, worldH);
            node.energy = 0.0;
            node.alive = true;
            node.hue = wrapHue(360.0 * rng.nextDouble()); // 节点第 3 抽（R1-B 同式）
            nodes_.push_back(node);
        }
        return;
    }
    // 均匀模式（cyanoColonies<=0，模块默认）：旧序逐位不变。
    for (int i = 0; i < count; ++i) {
        CyanoNode node;
        node.id = nextId_++;
        // spawn rng 消耗顺序固定：先 x 后 y，再 hue，各一次 nextDouble（R1-B）。
        node.pos = wrap(Vec2{rng.nextDouble() * worldW, rng.nextDouble() * worldH},
                        worldW, worldH);
        node.energy = 0.0;
        node.alive = true;
        node.hue = wrapHue(360.0 * rng.nextDouble()); // 遗传色相：[0,360) 均匀（R1-B）
        nodes_.push_back(node);
    }
}

CyanoStepReport CyanoField::step(double sunLevel, double worldW, double worldH,
                                 PCG32& rng) {
    CyanoStepReport report;
    worldW_ = worldW; // 环面尺寸快照（连线传输 linkTransfer 使用）
    worldH_ = worldH;

    // ---- 阶段 1：光合（不耗 rng）----
    // R3-A 自遮光：localSun = sunLevel * exp(-shadeK * shade_[i])，
    // gain = photosynthesisRate * localSun / (localSun + lightHalfSaturation)。
    // shade_ 是上一结算帧 linkTransfer() 缓存的遮蔽量（一帧滞后：节点位置
    // 永不移动，滞后仅来自帧间存活集合变化；见 shadeCache() 注释）。
    // sunLevel <= 0 短路为 0；localSun 下溢为 0 时同样短路（防 halfSat=0 的
    // 0/0 NaN）。shadeK=0 且 shade_=0 时 exp(-0)=1，与旧公式逐位一致。
    if (shade_.size() != nodes_.size()) shade_.resize(nodes_.size(), 0.0);
    for (std::size_t ci = 0; ci < nodes_.size(); ++ci) {
        CyanoNode& node = nodes_[ci];
        if (!node.alive) continue;
        double gain = 0.0;
        if (sunLevel > 0.0) {
            const double localSun =
                sunLevel * std::exp(-params_.shadeK * shade_[ci]);
            if (localSun > 0.0) {
                gain = params_.photosynthesisRate * localSun /
                       (localSun + params_.lightHalfSaturation);
            }
        }
        // 进账口径：只计真正存进节点的部分（受 energyCap 余量约束）。
        // 预置能量已超上限的节点入账 0，绝不产生负 gain。
        //
        // 【红队 R8-P0 修正 2026-10-31】必须 += credited 而不是 += gain。
        // 旧写法：credited（入账）和 gain（实加）不一致——能量已封顶的节点
        // 每帧仍 +gain，随后在阶段 2 被 clip 回 energyCap，多出来的部分凭空
        // 蒸发且不入任何账目字段。修正后：入账 = 实加，两侧恒等。
        const double room =
            params_.energyCap > node.energy ? params_.energyCap - node.energy : 0.0;
        const double credited = gain < room ? gain : room;
        node.energy += credited;
        report.photosynthesisGain += credited;
    }

    // ---- 阶段 2：代谢（不耗 rng）----
    // 采用【线性常数型】：loss = metabolismRate，每 tick 固定耗散；
    // energy <= 0 判饿死。截断至 energyCap 的 surplus 不计入任何 gain。
    //
    // 【红队 R8-P0 修正 2026-10-31】饿死清算必须入账：实际耗散 = 死前能量，
    // 记入 metabolismLoss（账面耗散），账实相等。
    for (CyanoNode& node : nodes_) {
        if (!node.alive) continue;
        const double loss = params_.metabolismRate;
        node.energy -= loss;
        if (node.energy <= 0.0) {
            // 饿死：实际耗散 = 死前能量（而不是名义 loss，因为能量可能不足 loss）。
            report.metabolismLoss += loss + node.energy; // node.energy 为负，等价于 loss - |剩|
            node.energy = 0.0;
            node.alive = false; // 饿死
            continue;
        }
        report.metabolismLoss += loss;
        if (node.energy > params_.energyCap) {
            node.energy = params_.energyCap; // 状态归一；入账口径已在阶段1限定
        }
    }

    // ---- 阶段 2.5：连线能量传输（S22 下半；不耗 rng）----
    // RNG 红线：linkTransfer() 绝不消耗随机数（无 rng 参数，纯几何+算术），
    // 位于"光合 -> 代谢"之后、"产球判定"之前，判定阶段的消耗序保持不变。
    // 零和内部转移：不产生不消耗，不影响任何账目字段；转移总量仅
    // 记入 report.linkTransferred 供观测。
    // R8/P2-2（GOAL §2.2.1 ④）：仅当 frameCount_ % linkCheckInterval == 0 时
    // 才调用 linkTransfer()。interval=1（默认）每帧都调，与旧行为逐位一致。
    // interval=N>1 时是**整体跳过的简化语义**：跳过帧既不重算拓扑、也不结算
    // 能量传输（与旧版"重算拓扑但每帧结算"不同，见 CyanoParams 注释）。
    // report.linkTransferred 只在真正结算的帧刷新，非结算帧保持 0。
    if (params_.linkCheckInterval > 0 && frameCount_ % params_.linkCheckInterval == 0) {
        linkTransfer();
        report.linkTransferred = linkMoved_;
    }

    // ---- 阶段 3：产球判定（每存活节点恰好一次 nextDouble）----
    // 判定与位置抽取分离：先全体判定，再对判定通过者取位置，
    // 保证 rng 消耗顺序 = 判定 -> 位置，与任务规格逐位一致。
    std::vector<std::size_t> ballSpawned;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        CyanoNode& node = nodes_[i];
        if (!node.alive) continue;
        if (rng.nextDouble() < params_.ballSpawnProbability &&
            node.energy >= params_.ballSpawnCost) {
            node.energy -= params_.ballSpawnCost;
            ++report.ballsProduced;
            report.ballEnergyTotal += params_.ballSpawnCost * params_.ballRadius;
            ballSpawned.push_back(i);
        }
    }

    // ---- 阶段 4：产球位置（每个已判定产球节点两次 nextDouble：距离,角度）----
    // R8/P2-1（GOAL §2.2.1 ③"在节点附近随机生成自由小球"）：出生点取
    // 距产者 [ballSpawnMinDist, ballSpawnMaxDist] 环面距离、方向均匀随机的点，
    // wrap 回环面内——替代旧版的全图均匀随机（球散布全图没人捡得到）。
    // 注：偏移距离以欧氏直距取值、方向均匀随机——wrap 后的环面最短距仍属
    // [minDist, maxDist]（见 cyano_ball_spawn_near_parent）。
    // ballSpawned 存的是节点下标：用它取回产者位置（判定序 = 下标升序）。
    ballSpawns_.clear();
    ballSpawns_.reserve(ballSpawned.size());
    for (const std::size_t parentIdx : ballSpawned) {
        const CyanoNode& parent = nodes_[parentIdx];
        const double off = params_.ballSpawnMinDist +
                           rng.nextDouble() * (params_.ballSpawnMaxDist -
                                               params_.ballSpawnMinDist);
        const double ang = rng.nextDouble() * kTwoPi;
        // Vec2 为纯聚合类型、无 operator+：按分量构造偏移后 wrap（与分裂位置同惯用法）。
        const Vec2 bp = wrap(Vec2{parent.pos.x + off * std::cos(ang),
                                  parent.pos.y + off * std::sin(ang)},
                             worldW, worldH);
        ballSpawns_.push_back(bp);
    }

    // ---- 阶段 5：分裂判定（每存活节点恰好一次 nextDouble；帧初快照集合）----
    // 新生儿（含本帧出生者）不参与本帧判定与间距校验，保证同输入逐位可复现。
    // R1-B：判定通过者立即抽一次色相高斯抖动 u（Box-Muller 单侧双掷），
    // 无论后续间距校验是否放行都恰消耗一次；消耗序 = 判定 -> 全体色相抖动 ->
    // 位置（半径,角度）。放弃者丢弃该抖动样本（数值无副作用）。
    std::vector<std::size_t> splitParents;
    std::vector<double> splitJitter;
    const std::size_t frameStartCount = nodes_.size();
    for (std::size_t i = 0; i < frameStartCount; ++i) {
        CyanoNode& node = nodes_[i];
        if (!node.alive) continue;
        // RNG 纪律：无论是否触顶，这里都必须消费一次 nextDouble()，
        // 否则 maxNodes 的取值会改变后续随机流（破坏"同 seed 同轨迹"）。
        // 触顶时把判定结果强制为 false，随机数照抽。
        const bool rollOk = rng.nextDouble() < params_.reproductionProbability;
        const bool hasRoom = nodes_.size() < params_.maxNodes;
        if (rollOk && hasRoom && node.energy >= params_.reproductionCost) {
            splitParents.push_back(i);
            // R1-B：色相抖动抽取点（固定于"该节点判定通过之后"）。
            const double u1 = foldCentered(rng.nextDouble()); // (0,1]
            const double u2 = rng.nextDouble();               // [0,1)
            const double r = std::sqrt(-2.0 * std::log(u1));
            splitJitter.push_back(r * std::cos(kTwoPi * u2));
        }
    }

    // ---- 阶段 6：分裂位置与校验（每个已判定亲代两次 nextDouble：半径,角度）----
    // R7-C 子任务 3：分裂半径两段分布——70% 落在父附近 [0, 0.5*offset)
    // （聚落形态维持），30% 落在远处 [0.5*offset, offset)（允许开拓新域）。
    // 半径仍由单次 nextDouble 确定性线性映射到两段——消耗序/消耗量与旧版
    // 逐位一致，仅取值分布不同。设计依据见文件头 kReproNear* 常量注释。
    for (std::size_t k = 0; k < splitParents.size(); ++k) {
        const std::size_t i = splitParents[k];
        CyanoNode& parent = nodes_[i];
        // R7-C 子任务 3：分裂半径两段分布（见文件头常量注释）。
        // u ∈ [0,1) 单抽线性映射到两段：近段 [0, nearSpan*offset)（概率 0.7）、
        // 远段 [nearSpan*offset, offset)（概率 0.3），段边界连续。仍恰 1 抽。
        const double u = rng.nextDouble();
        const double nearSpan = params_.reproductionOffset * kReproNearSpan;
        const double radius = u < kReproNearFraction
            ? (u / kReproNearFraction) * nearSpan
            : nearSpan + ((u - kReproNearFraction) / (1.0 - kReproNearFraction)) *
                             (params_.reproductionOffset - nearSpan);
        const double angle = rng.nextDouble() * kTwoPi;
        const Vec2 childPos = wrap(Vec2{parent.pos.x + radius * std::cos(angle),
                                        parent.pos.y + radius * std::sin(angle)},
                                   worldW, worldH);

        // 子代间距校验：对全部存活同类求最小环距——亲代自身除外（它位于偏移原点、距离恰为偏移半径），
        // 不足 minSeparation 则放弃本次分裂：不扣能量、不 births++、不记 reproductionLoss；
        // 上面两次 rng 抽取照常消耗——判定先于位置的固定时序所要求，有意为之。
        bool ok = true;
        const double sep = params_.minSeparation;
        const std::size_t priorCount = nodes_.size();
        for (std::size_t j = 0; j < priorCount; ++j) {
            if (j == i || !nodes_[j].alive) continue; // 亲代豁免；兄弟子代计入
            if (torusDistance(childPos, nodes_[j].pos, worldW, worldH) < sep) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        // 【红队 R9-P2 修正 2026-10-31】push_back 前回查上限。
        // 阶段 5 的 hasRoom 是对"帧初快照"判定的：同帧多个亲代全部通过后，
        // 阶段 6 会连续 push_back，nodes_.size() 在循环内增长——
        // 实测 50k tick 出现 nodes=65（maxNodes=64）。
        // 回查不补抽 rng（阶段 5 已按帧初快照抽过），确定性契约不变。
        if (nodes_.size() >= params_.maxNodes) continue;

        // 真正分裂：亲代扣 reproductionCost，子代得一半，另一半耗散。
        parent.energy -= params_.reproductionCost;
        CyanoNode child;
        child.id = nextId_++;
        child.pos = childPos;
        child.energy = params_.reproductionCost * 0.5;
        child.alive = true;
        // R1-B：遗传色相 = 父 + 高斯抖动（splitJitter[k] 与 splitParents[k] 严格配对），
        // 环绕回 [0,360)。分裂成功才消费抖动样本；放弃分支的样本被丢弃。
        child.hue = wrapHue(parent.hue + params_.hueJitterSigma * splitJitter[k]);
        nodes_.push_back(child);
        ++report.births;
        report.reproductionLoss += params_.reproductionCost * 0.5;
    }

    // 【红队 R8-P1 修正 2026-10-31】压缩尸体槽位。
    // 旧问题：maxNodes 按 nodes_.size()（含 !alive 尸体）判定"有没有空位"，
    // 而尸体永不清理 → 历史死亡数单调蚕食出生空间。
    // 修法：与 nuclei_ 的压缩同一手法（remove_if 保序、不消耗 RNG、不改确定性）。
    // 能量安全：尸体 energy 已是 0（饿死/被吃绝时归零），erase 不丢失任何能量。
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                [](const CyanoNode& n) { return !n.alive; }),
                 nodes_.end());
    // R8/P2-2：帧计数递增（linkCheckInterval 的节流相位；seed() 清零）。
    ++frameCount_;
    return report;
}

// ---- S22 下半：连线能量再分配（确定性、零和、零随机）----
// 结构说明：两段式，全程不消耗任何随机数——
//   (1) 候选：每个存活节点收集 linkRadius 环距内的全部存活节点，按
//       （距离, 对方下标）升序排序后截取前 linkMaxNeighbors 个（不足则照收）。
//   (2) 成边+结算：i 选 j 且 j 选 i（双向互选）才成边。因边必两端互选，
//       任一节点度数 <= 自身候选数 <= linkMaxNeighbors（度数上限由构造保证，
//       无需先到先得）。边按 (i<j) 有序对去重，每对只处理一次；
//       能量差 > linkDeadband 时自高能端向低能端传输 (hi - lo) * linkTransferRate，
//       差值 <= deadband 的对跳过（防抖）。
//   R1-B：实际成交的边（两端节点 id 有序对）记入 links_（拓扑对外可见）。
//   防抖跳过的边不计入（无能量成交）；每次调用先清空重建——links() 恒等于
//   "最近一次结算的拓扑快照"。
// 零和账本：每笔转移同时记 -amt/+amt；本函数不读写任何 rng。
void CyanoField::linkTransfer() {
    linkMoved_ = 0.0;
    linkEdges_ = 0;
    links_.clear();
    const std::size_t n = nodes_.size();
    // ---- R3-A：遮蔽量缓存刷新（复用本函数的 O(n²) 邻居扫描，热路径零新增）----
    // 每次结算先把缓存清零，再在下方候选扫描里顺带累计。对称性：d(i,j)=d(j,i)，
    // 每个无序对只算一次 exp、同时记入两端（j>i 时）。连线参数退化（关连线/
    // 无候选）时走早退分支的仅遮蔽扫描——生产默认路径永远不进该分支，卡面
    // "不许新增独立 O(n²)"的预算只约束热路径。linkCheckInterval>1 的跳过帧
    // 不刷新（与拓扑/能量快照同一口径）。纯几何，零 rng 消耗。
    const bool wantShade = params_.shadeK > 0.0 && params_.shadeRadius > 0.0 &&
                           params_.shadeSigma > 0.0;
    const double twoSigma2 = 2.0 * params_.shadeSigma * params_.shadeSigma;
    shade_.assign(n, 0.0);
    if (n < static_cast<std::size_t>(2) || params_.linkMaxNeighbors <= 0 ||
        params_.linkRadius <= 0.0) {
        // 退化分支：没有候选扫描可搭。n<2 时遮蔽恒 0（不存在邻居，物理正确）；
        // n>=2 且遮蔽开启时跑一趟仅遮蔽的对称对扫描（仅测试/关连线配置可达，
        // 保证"关连线"不会顺带静默关闭自遮光）。
        if (wantShade && n >= static_cast<std::size_t>(2)) {
            for (std::size_t si = 0; si < n; ++si) {
                if (!nodes_[si].alive) continue;
                for (std::size_t sj = si + 1; sj < n; ++sj) {
                    if (!nodes_[sj].alive) continue;
                    const double d = torusDistance(nodes_[si].pos, nodes_[sj].pos,
                                                   worldW_, worldH_);
                    if (d < params_.shadeRadius) {
                        const double w = std::exp(-(d * d) / twoSigma2);
                        shade_[si] += w;
                        shade_[sj] += w;
                    }
                }
            }
        }
        return;
    }
    const std::size_t cap = static_cast<std::size_t>(params_.linkMaxNeighbors);
    const double radius = params_.linkRadius;
    const double rate = params_.linkTransferRate;
    const double deadband = params_.linkDeadband;

    // (1) 每个存活节点的最近候选表（按 距离, 对方下标 升序，截取前 cap 个）。
    //     R11 性能重写：全对 O(n²) 扫描 → 环面空间网格（cell = max(linkRadius,
    //     shadeRadius)，3×3 邻域）。语义逐位等价的三个保证：
    //       a) 候选集合不变：d ≤ radius 的 j 必在本 cell 或相邻 cell（cell ≥ radius）；
    //       b) picks[i] 排序键 (d, j) 在互异 j 上是全序 → 排序结果与扫描顺序无关；
    //       c) 遮蔽累加顺序不变：先收集 (i<j, w) 对再按 (i,j) 字典序回放——每个
    //          shade_[k] 的加法序列与旧实现完全一致（浮点逐位相同）。
    //     世界任一轴 < 3×cell（微型测试世界）时退回旧全对扫描，正确性优先。
    const double cellSize = std::max(radius, params_.shadeRadius);
    const bool useGrid = cellSize > 0.0 &&
                         worldW_ >= 3.0 * cellSize && worldH_ >= 3.0 * cellSize;
    std::vector<std::vector<std::pair<double, std::size_t>>> picks(n);
    if (!useGrid) {
        // ---- 旧全对扫描（微型世界/退化配置保底，与 R11 前逐位一致）----
        for (std::size_t i = 0; i < n; ++i) {
            if (!nodes_[i].alive) continue;
            for (std::size_t j = 0; j < n; ++j) {
                if (j == i || !nodes_[j].alive) continue;
                const double d =
                    torusDistance(nodes_[i].pos, nodes_[j].pos, worldW_, worldH_);
                if (wantShade && j > i && d < params_.shadeRadius) {
                    const double w = std::exp(-(d * d) / twoSigma2);
                    shade_[i] += w;
                    shade_[j] += w;
                }
                if (d > radius) continue; // 环距超限：不连
                picks[i].push_back({d, j});
            }
            std::sort(picks[i].begin(), picks[i].end());
            if (picks[i].size() > cap) picks[i].resize(cap);
        }
    } else {
        // ---- 环面网格路径（生产默认路径；桶只装存活节点）----
        const int cellsX = std::max(1, static_cast<int>(worldW_ / cellSize));
        const int cellsY = std::max(1, static_cast<int>(worldH_ / cellSize));
        std::vector<std::vector<std::size_t>> buckets(
            static_cast<std::size_t>(cellsX) * static_cast<std::size_t>(cellsY));
        for (std::size_t i = 0; i < n; ++i) {
            if (!nodes_[i].alive) continue;
            int bx = static_cast<int>(nodes_[i].pos.x / cellSize);
            int by = static_cast<int>(nodes_[i].pos.y / cellSize);
            if (bx < 0) bx = 0;
            if (by < 0) by = 0;
            if (bx >= cellsX) bx %= cellsX;   // x∈[cellsX*cell, W) 溢出带 → 环回 0
            if (by >= cellsY) by %= cellsY;
            buckets[static_cast<std::size_t>(by) * cellsX + bx].push_back(i);
        }
        // 遮蔽对先收集后按 (i,j) 字典序回放（保证浮点加法序列与旧实现一致）。
        struct ShadePair { std::size_t i; std::size_t j; double w; };
        std::vector<ShadePair> shadePairs;
        if (wantShade) shadePairs.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            if (!nodes_[i].alive) continue;
            int bx = static_cast<int>(nodes_[i].pos.x / cellSize);
            int by = static_cast<int>(nodes_[i].pos.y / cellSize);
            if (bx < 0) bx = 0;
            if (by < 0) by = 0;
            if (bx >= cellsX) bx %= cellsX;
            if (by >= cellsY) by %= cellsY;
            for (int dy = -1; dy <= 1; ++dy) {
                const int ny = (by + dy + cellsY) % cellsY;
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = (bx + dx + cellsX) % cellsX;
                    const std::vector<std::size_t>& bucket =
                        buckets[static_cast<std::size_t>(ny) * cellsX + nx];
                    for (std::size_t bi = 0; bi < bucket.size(); ++bi) {
                        const std::size_t j = bucket[bi];
                        if (j == i) continue;
                        const double d =
                            torusDistance(nodes_[i].pos, nodes_[j].pos, worldW_, worldH_);
                        if (wantShade && j > i && d < params_.shadeRadius) {
                            shadePairs.push_back({i, j, std::exp(-(d * d) / twoSigma2)});
                        }
                        if (d > radius) continue; // 环距超限：不连
                        picks[i].push_back({d, j});
                    }
                }
            }
            std::sort(picks[i].begin(), picks[i].end());
            if (picks[i].size() > cap) picks[i].resize(cap);
        }
        if (wantShade) {
            std::sort(shadePairs.begin(), shadePairs.end(),
                      [](const ShadePair& a, const ShadePair& b) {
                          if (a.i != b.i) return a.i < b.i;
                          return a.j < b.j;
                      });
            for (std::size_t sp = 0; sp < shadePairs.size(); ++sp) {
                shade_[shadePairs[sp].i] += shadePairs[sp].w;
                shade_[shadePairs[sp].j] += shadePairs[sp].w;
            }
        }
    }

    // (2) 双向互选成边并结算：只从小下标侧发起（i<j 有序对，每对至多一次）。
    for (std::size_t i = 0; i < n; ++i) {
        if (!nodes_[i].alive) continue;
        for (const std::pair<double, std::size_t>& pr : picks[i]) {
            const std::size_t j = pr.second;
            if (j <= i) continue; // 有序对去重
            bool mutual = false;
            for (const std::pair<double, std::size_t>& back : picks[j]) {
                if (back.second == i) { mutual = true; break; } // j 回选 i
            }
            if (!mutual) continue;
            CyanoNode& a = nodes_[i];
            CyanoNode& b = nodes_[j];
            CyanoNode& hi = a.energy >= b.energy ? a : b;
            CyanoNode& lo = a.energy >= b.energy ? b : a;
            const double diff = hi.energy - lo.energy;
            if (diff <= deadband) continue; // 防抖：差值过小不传输
            const double amount = diff * rate;
            hi.energy -= amount;
            lo.energy += amount;
            linkMoved_ += amount;
            ++linkEdges_;
            // R1-B：记录成交边（两端点 id，按 (小,大) 有序对存储——i<j 遍历序保证）。
            links_.push_back({a.id, b.id});
        }
    }
}

double CyanoField::graze(EntityId id, double rawEnergy, double conversionRate) {
    for (CyanoNode& node : nodes_) {
        if (node.id != id || !node.alive) continue;
        // 负请求防御：期望抽取量下限 0，避免负 taken 反向注能。
        const double want =
            rawEnergy * conversionRate > 0.0 ? rawEnergy * conversionRate : 0.0;
        // R3-A 捕食避难所：可食余量 = energy - grazeRefugeEnergy（下限 0）。
        // refuge <= 0 时 edible == node.energy，与旧式逐位一致（回归安全）。
        const double edible = node.energy - params_.grazeRefugeEnergy;
        const double avail = edible > 0.0 ? edible : 0.0;
        const double taken = want < avail ? want : avail;
        node.energy -= taken;
        if (node.energy <= 0.0) {
            // 【红队 R8-P0 修正】被吃光时 taken 上限就是 node.energy，
            // 节点能量恰归零，没有能量蒸发。归零并标记死亡即可。
            // （仅 refuge <= 0 的旧语义可达：refuge > 0 时 taken <= energy-refuge，
            // 吃后 energy >= refuge > 0，本分支不可达——节点永不被捕食抽死。）
            node.energy = 0.0;
            node.alive = false;
        }
        return taken;
    }
    return 0.0;
}

// ---- R3-A（营养循环）：尸体能量真实转移 ----
// 实现契约见 CyanoNode.h 的 fertilize 注释。进账口径与光合的 credited 一致：
// 只入真正存进节点的部分（受 energyCap 余量约束），溢出部分由调用方耗散；
// id 不存在 / 已死 / amount 非正返回 0（能量绝不滞留在模块内，绝不反向扣款）。
double CyanoField::fertilize(EntityId nodeId, double amount) {
    if (!(amount > 0.0)) return 0.0;
    for (CyanoNode& node : nodes_) {
        if (node.id != nodeId || !node.alive) continue;
        const double room =
            params_.energyCap > node.energy ? params_.energyCap - node.energy : 0.0;
        const double accepted = amount < room ? amount : room;
        node.energy += accepted;
        return accepted;
    }
    return 0.0;
}

std::size_t CyanoField::aliveCount() const {
    std::size_t c = 0;
    for (const CyanoNode& node : nodes_) {
        if (node.alive) ++c;
    }
    return c;
}

double CyanoField::totalEnergy() const {
    double s = 0.0;
    for (const CyanoNode& node : nodes_) {
        if (node.alive) s += node.energy;
    }
    return s;
}


// ---- R3-B（实时调控接线）追加：追加式播种 ----
// 实现契约见 CyanoNode.h 的 appendSeed 注释。与 seed() 的差异只有三点：
//   1) 不清场（nodes_/links_/frameCount_ 一概不动）；
//   2) 逐节点先查 maxNodes 护栏，满了立即停（返回实际追加数）；
//   3) 追加节点能量 0（能量从光合长出来，账本由 step() 的 injectPrimary 入账）。
// rng 消耗：每个实际追加的节点恰好 3 次 nextDouble（x, y, hue），顺序与 seed()
// 的逐节点消耗序一致；护栏截断的节点不消耗（先检查后消耗，与分裂触顶同法）。
int CyanoField::appendSeed(int count, double worldW, double worldH, PCG32& rng) {
    worldW_ = worldW; // 环面尺寸快照（连线传输 linkTransfer 使用；与 seed() 同步）
    worldH_ = worldH;
    int added = 0;
    for (int i = 0; i < count; ++i) {
        if (nodes_.size() >= params_.maxNodes) break; // 性能护栏：与分裂触顶同源
        CyanoNode node;
        node.id = nextId_++;
        node.pos = wrap(Vec2{rng.nextDouble() * worldW, rng.nextDouble() * worldH},
                        worldW, worldH);
        node.energy = 0.0;
        node.alive = true;
        node.hue = wrapHue(360.0 * rng.nextDouble()); // 与 seed() 同式（wrapHue 恒等，保险）
        nodes_.push_back(node);
        ++added;
    }
    return added;
}

void CyanoField::restoreForSaveLoad(std::vector<CyanoNode> nodes, EntityId nextId,
                                    long long frameCount) {
    nodes_ = std::move(nodes);
    nextId_ = nextId;
    frameCount_ = frameCount;
    links_.clear();      // 拓扑由下一次 linkTransfer 结算帧重建（不主动结算）
    linkMoved_ = 0.0;    // 观测量归零（与"尚未结算"语义一致）
    linkEdges_ = 0;
    ballSpawns_.clear(); // 逐帧瞬态，恢复期必空
}

void CyanoField::refreshShadeCache() {
    const std::size_t n = nodes_.size();
    shade_.assign(n, 0.0);
    if (n < 2) return;
    if (!(params_.shadeK > 0.0) || !(params_.shadeRadius > 0.0) ||
        !(params_.shadeSigma > 0.0)) return;
    const double twoSigma2 = 2.0 * params_.shadeSigma * params_.shadeSigma;
    for (std::size_t i = 0; i < n; ++i) {
        if (!nodes_[i].alive) continue;
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!nodes_[j].alive) continue;
            const double d = torusDistance(nodes_[i].pos, nodes_[j].pos,
                                           worldW_, worldH_);
            if (d < params_.shadeRadius) {
                const double w = std::exp(-(d * d) / twoSigma2);
                shade_[i] += w;
                shade_[j] += w;
            }
        }
    }
}
} // namespace agentsim


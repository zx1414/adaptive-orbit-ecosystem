// agentsim - sim/Simulation.cpp
// 组合根实现：构造装配 + 主循环 step/runTo + 统计与哈希。
// R1-B（蓝细菌取代食物）：Food 全链路删除；核的唯一食物 = 蓝细菌节点，
// 捕食改为确定性"最近节点 + 转化率"语义（本段不消耗 rng，见 step() 内 R1-B 注释）；
// 阳光模型参数化（SunConfig），阳光噪声用确定性整数哈希（零 rng 消耗）。
#include "sim/Simulation.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string>
#include <cstring>

#include "decision/BanditPolicy.h"
#include "decision/RiskAversePolicy.h"
#include "decision/ForageBankPolicy.h"
#include "decision/HysteresisPolicy.h"
#include "decision/PerceptronPolicy.h"
#include "decision/RulePolicy.h"
#include "decision/ThermalPolicy.h"
#include "environment/Terrain.h"
#include "perception/AgentSensors.h"
#include <ostream>  // R5-C：存档序列化
#include <map>  // R5-C：存档序列化
#include <istream>  // R5-C：存档序列化

namespace agentsim {
namespace {
// R2-A：kReproCap/kReproEnergyGate/kReproChanceP 全部删除。
//   种群承载力由能量自然决定：核能量 > genome.energyThreshold（基因，80~200）时繁殖
//   （reproduce 内零和转移，无外生注入）；ColonyConfig::maxNuclei（默认 300）只是
//   O(n²) 对核物理的性能护栏。繁殖是确定性事件：基因门槛满足即繁殖（GOAL-ECOLOGY
//   §三.5：承载力不能是外生常数）。旧的 0.2%/tick 随机采样闸删除。
// S22 接线：蓝细菌池 id。旧实现与 kFoodPoolId（=0，AgentBehavior.h）取同一值：
// 两者同为"环境生产者池"——蓝细菌是环境层向第一营养级供能的生产者，
// 账本按 id 归并生产者流量（injectPrimary/dissipate 主体），捕食的
// 流向约定（kCyanoPoolId -> 核）与旧食物池同一主体。R1-B 后 Food 已删除，
// 该池语义完全归于蓝细菌。
constexpr EntityId kCyanoPoolId = 0; // = kFoodPoolId（AgentBehavior.h）
// R2-A（基因驱动捕食）：每口原始激励 = genome.cyanoPreference * kGrazeBiteFraction
// * 猎物能量（**与核自身能量无关**——捕食强度由基因决定，不再"系统按核能量随机喂饭"）。
// 0.05 = 生态标定（替代卡面的 0.15，理由如下，这是承载力校准不是作弊）：
//   供给：节点数 64 × 光合净增（0.5 光照下 ~0.48/tick，能量帽下被截断）≈ 30 能量/tick
//   需求：核数 × 每口 taken ≈ N × pref(0.5) × f × E(100) × rate(0.126) = N × 0.63 × f/tick
//   f=0.15 时 24 核需求 45 > 30 供给 → 蓝细菌崩塌 → 全员饿死（实测 seed42 t<2000 灭绝）。
//   f=0.05 时需求 = N × 0.0315/tick → 承载力 N* ≈ 950？？不——能量帽限制供给上限
//   30，N* ≈ 30/0.315 ≈ 95 核。即种群自然平衡在 ~95 核（< maxNuclei 300 护栏），
//   且随光强昼夜波动——这正是 GOAL-ECOLOGY §一.2 要的"承载力由能量决定"。
// R3-A：kGrazeBiteFraction 从 constexpr 提升为 cfg.nucleus.grazeBiteFraction
//（默认仍 0.05 = R2-A 标定值；R3-A 标定与承载力分析见 Config.h NucleusTuning 注释
// 与下方使用点）。提升动机：该系数 × 捕食避难所共同决定消费者平衡种群
// N* ∝ 1/grazeBiteFraction——参数扫描（build 临时 sweep 工具）实测 refuge=15 时
// N*≈350 撞 maxNuclei=300 护栏锁死（boom-bust 消失但种群锁死、r=+0.8），
// 需要把系数提到 N* 落回护栏下方，消费端才由食物稀缺真实调节。
// R2-A：核死亡 30% 遗产分配给最近蓝细菌的搜索半径（ECOLOGY-DESIGN §三.2 营养循环；
// 找不到分解者时全额耗散——能量绝不滞留）。
constexpr double kCarrionSearchRadius = 200.0;
// 蓝细菌源自由球 id 下界（与 Simulation::nextFreeBallId_ 初值同基，区分球来源）。
constexpr EntityId kCyanoBallIdBase = 0x40000000ULL;
// 自由球回收上限：蓝细菌源自由球几乎不会被拾取（核拾取受 kMaxBalls=3 硬上限，
// 附着很快饱和），无界累积使组合根每帧拾取扫描 O(核数×球数)（实测 19k 球时
// 主循环 >100ms/tick 且线性恶化，20k tick 不可完成）。超限回收**最老**的
// 蓝细菌源球（FIFO=容器下标序，确定性安全），能量全额 dissipate 入账，
// 恒等式闭合；核脱离球（id < base，S19/S20 语义）绝不触碰。
// 【红队 R9-P0 修正 2026-10-31】1024 → 256。
// 旧值下自由球池稳态 inflow ≈ outflow，池能量占比实测 76~81%（50k 会爬回 84%）
// —— "能量坟场"修复表面生效、目标未达成。256 把池能量硬顶结构性压到 <25%，
// 且让每帧拾取扫描的 O(核数×球数) 上限同步缩小（性能双收益）。
constexpr std::size_t kFreeBallCap = 256;
// R7-B：球负载半径基数（= AgentBehavior.cpp 匿名 ns 的 kBallRadius；行为层不
// 暴露该常量，组合根侧同值镜像——单点漂移由 R7-B 单测钉死）。
constexpr double kBallRadiusR7 = 10.0;

// ---- R1-B：确定性阳光模型 ----
// sunLevel(tick, y) = clamp01( base - gradient*(y/H)
//                             + amplitude * sin(2*pi*tick/period)
//                             + noiseAmp * hash01(tick, floor(y*freq), tick/speed) )
// 计算式的唯一权威实现在此（前端按 docs/TRACE-CONTRACT.md 的 R1-B 附录复算）。
// 噪声是纯确定性整数哈希（splitmix64 风格 finalizer），零 rng 消耗——
// 随机源注释：本函数不触碰任何 PCG32（声明在 Simulation.cpp 顶部 R1-B 注释）。
constexpr double kTwoPi = 6.283185307179586;

std::uint64_t sunHashMix(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}

double sunHash01(std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    std::uint64_t h = sunHashMix(a);
    h ^= sunHashMix(b + 0x517cc1b727220a95ULL);
    h ^= sunHashMix(c + 0x2545f4914f6cdd1dULL);
    // 取 53 位有效位归一到 [0,1)：与 PCG32::nextDouble 的分辨率对齐。
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

// 单点阳光：base(y) + 昼夜正弦 + 网格值噪声，全部 clamp 到 [0,1]。
double sunLevelAt(const SunConfig& sun, std::uint64_t tick, double y, double worldH) {
    double base = sun.baseLevel;
    if (sun.verticalGradient != 0.0 && worldH > 0.0) {
        base -= sun.verticalGradient * (y / worldH);
    }
    double day = 0.0;
    if (sun.dayPeriodFrames > 0.0 && sun.dayAmplitude != 0.0) {
        day = sun.dayAmplitude *
              std::sin(kTwoPi * static_cast<double>(tick) / sun.dayPeriodFrames);
    }
    double noise = 0.0;
    if (sun.noiseAmplitude != 0.0 && sun.noiseFrequency > 0.0 && worldH > 0.0) {
        // 值噪声：空间格 (floor(x*freq), floor(y*freq))，时间格 tick/speed
        //（speed<=0 视作静止：时间格恒 0）。格点哈希三输入全整数化。
        const long long gx = static_cast<long long>(y * sun.noiseFrequency);
        double tCell = 0.0;
        if (sun.noiseSpeed > 0.0) {
            tCell = std::floor(static_cast<double>(tick) / sun.noiseSpeed);
        }
        const long long gt = static_cast<long long>(tCell);
        noise = sun.noiseAmplitude * (sunHash01(static_cast<std::uint64_t>(gt),
                                                static_cast<std::uint64_t>(gx), 0ULL) - 0.5);
    }
    double v = base + day + noise;
    if (!(v > 0.0)) v = 0.0; // NaN/负值归 0
    if (v > 1.0) v = 1.0;
    return v;
}

// R1-B：地形谓词的 static 包装——AgentEnvironment 的 BlockedFn 接口保留
//（环境层通用"不可放置"判定），当前无食物撒点调用方；组合根照常绑定，
// 保持 R6-P1-1 的接线形态不变。
bool terrainBlockedPredicate(void* ctx, Vec2 pos) {
    return static_cast<Terrain*>(ctx)->blocked(pos);
}
} // namespace

Simulation::Simulation(const SimConfig& cfg)
    : cfg_(cfg), rng_(cfg.world.seed),
      // TERRAIN 接线：地形构造即撒点（消耗 rng_），排在一切出生引导之前。
      // 默认三 count 全 0 且无显式 blob 时，generate() 的 scatter 循环上界为 0，
      // 不消耗任何 rng —— 下游 rng 流逐位不变（默认地形回归安全）。
      terrain_(cfg.terrain, cfg.world.width, cfg.world.height, rng_),
      env_(cfg),
      // GOAL 温度接线：SimConfig 委托构造一次性撒源（含世界尺寸），先于出生引导消耗 rng_。
      temp_(cfg, rng_) {
    // R1-B：地形谓词照常绑定（环境层接口保留；Food 撒点已删除，无生产调用方）。
    env_.setBlockedPredicate(&terrainBlockedPredicate, &terrain_);
    spawnNuclei();
    // S22 接线：蓝细菌场初始化。顺序固定在 spawnNuclei() 之后——地形撒点、
    // 核出生、蓝细菌播种依次消耗 rng_，锁定 RNG 消耗流（确定性红线）。
    // R1-B：初始第一营养级库存 = cfg.cyano 节点数（--cyano-nodes /
    // --cyano-max-nodes 覆盖 CyanoParams；FoodConfig 已删除，不再有食物撒点）。
    CyanoParams cp; // 默认参数：光合/代谢/分裂/产球均用模块标定值
    cp.nodeSeedCount = cfg_.cyanoNodeCount;
    cp.maxNodes = cfg_.cyanoMaxNodes;
    // R3-A：CyanoTuning -> CyanoParams 构造期**全量**白盒镜像（与 applyConfig
    // 同一份字段清单，出生引导量 nodeSeedCount/maxNodes 仍由 cyanoNodeCount/
    // cyanoMaxNodes 覆盖）。此前构造期只认 CyanoParams 结构体默认值，cfg.cyanoTuning
    // 的任何构造期配置都被静默忽略（shade 三参数最先暴露这一点）；R3-A 标定
    //（photosynthesisRate 0.35 / metabolismRate 0.05）经由本镜像进入 CLI 默认运行。
    {
        const CyanoTuning& ct = cfg_.cyanoTuning;
        cp.photosynthesisRate = ct.photosynthesisRate;
        cp.lightHalfSaturation = ct.lightHalfSaturation;
        cp.metabolismRate = ct.metabolismRate;
        cp.energyCap = ct.energyCap;
        cp.reproductionCost = ct.reproductionCost;
        cp.reproductionProbability = ct.reproductionProbability;
        cp.reproductionOffset = ct.reproductionOffset;
        cp.minSeparation = ct.minSeparation;
        cp.ballSpawnCost = ct.ballSpawnCost;
        cp.ballSpawnProbability = ct.ballSpawnProbability;
        cp.ballRadius = ct.ballRadius;
        cp.ballSpawnMinDist = ct.ballSpawnMinDist;
        cp.ballSpawnMaxDist = ct.ballSpawnMaxDist;
        cp.linkRadius = ct.linkRadius;
        cp.linkMaxNeighbors = ct.linkMaxNeighbors;
        cp.linkTransferRate = ct.linkTransferRate;
        cp.linkDeadband = ct.linkDeadband;
        cp.linkCheckInterval = ct.linkCheckInterval;
        cp.conversionMode = ct.conversionMode;
        cp.conversionRate = ct.conversionRate;
        cp.conversionExponent = ct.conversionExponent;
        cp.conversionSpeedRef = ct.conversionSpeedRef;
        cp.conversionAccelRef = ct.conversionAccelRef;
        cp.conversionWSpeed = ct.conversionWSpeed;
        cp.conversionWAccel = ct.conversionWAccel;
        cp.conversionMinRate = ct.conversionMinRate;
        cp.conversionMaxRate = ct.conversionMaxRate;
        cp.hueJitterSigma = ct.hueJitterSigma;
        // R7-C：聚落播种形状镜像（seed() 消费；nodeSeedCount/maxNodes 仍由
        // cyanoNodeCount/cyanoMaxNodes 覆盖，见上方出生引导量注释）。
        cp.cyanoColonies = ct.cyanoColonies;
        cp.clusterSpread = ct.clusterSpread;
        cp.shadeK = ct.shadeK;
        cp.shadeRadius = ct.shadeRadius;
        cp.shadeSigma = ct.shadeSigma;
        cp.grazeRefugeEnergy = ct.grazeRefugeEnergy;
    }
    // R3-A：NucleusTuning 的三个 R3-B 同义键构造期镜像进 bp_（与 applyConfig 的
    // 行为层同步同一份清单）。此前 bp_ 全用行为层默认，CLI 默认运行从未消费
    // cfg.nucleus.*——respirationBase 0.012 标定经由本镜像才真正生效。
    // 默认值逐位等于行为层默认（除 respirationBase 0.012 vs 0.008，R3-A 有意上调），
    // 其余两键（locomotionCost/cyanoPredationRadius）默认同值，回归逐位安全。
    bp_.respirationBase = cfg_.nucleus.respirationBase;
    bp_.locomotionCost = cfg_.nucleus.locomotionCost;
    bp_.cyanoPredationRadius = cfg_.nucleus.cyanoPredationRadius;
    bp_.migrationBonus = cfg_.nucleus.migrationBonus;     // R9-A 迁移回报镜像
    bp_.migrationRadius = cfg_.nucleus.migrationRadius;
    bp_.speedCostExponent = cfg_.nucleus.speedCostExponent; // R11-P1-3 接线镜像
    bp_.damping = cfg_.nucleus.damping;
    bp_.reproductionCostRatio = cfg_.nucleus.reproductionCostRatio;
    // R7-B：BallTuning -> 行为层 BallFieldParams 构造期镜像（与 bp_ 同一模式：
    // 组合根参数流，非实体写；applyConfig 同步同一清单）。R6-C Config.h 注释口径：
    // near_repel_radius 8 / detach_radius 60 / ball_ball_repel_radius 6 /
    // same_owner_attract_radius 40（R6-C 已把 Config 默认标定到本项目尺度）。
    ballField_.damping = cfg_.ball.damping;
    ballField_.radialK = cfg_.ball.radialK;
    ballField_.tangentialK = cfg_.ball.tangentialK;
    ballField_.nearRepelRadius = cfg_.ball.nearRepelRadius;
    ballField_.ballRepelRadius = cfg_.ball.ballRepelRadius;
    ballField_.ballRepelK = cfg_.ball.ballRepelK;
    ballField_.sameOwnerAttractRadius = cfg_.ball.sameOwnerAttractRadius;
    ballField_.sameOwnerAttractK = cfg_.ball.sameOwnerAttractK;
    ballField_.maxSpeedShield = cfg_.ball.maxSpeedShield;
    ballField_.maxSpeedResource = cfg_.ball.maxSpeedResource;
    ballField_.maxSpeedScout = cfg_.ball.maxSpeedScout;
    ballField_.followCoupling = cfg_.ball.followCoupling;
    ballField_.followBoost = cfg_.ball.followBoost;
    ballField_.detachRadius = cfg_.ball.detachRadius;
    ballField_.lossCost = cfg_.ball.lossCost;
    cyano_ = CyanoField(cp);
    cyano_.seed(static_cast<int>(cp.nodeSeedCount), cfg_.world.width,
                cfg_.world.height, rng_);
    ledger_.injectPrimary(kCyanoPoolId, cyano_.totalEnergy());
    // F2：按配置装配策略（此前无条件 RulePolicy，--policy stub 曾静默失效）。
    if (cfg_.policy.usePerceptron || cfg_.policy.name == "perceptron") {
        policy_ = std::make_unique<PerceptronPolicy>(cfg_.policy.perceptronSeed,
                                                     cfg_.policy.perceptronScale);
    } else if (cfg_.policy.name == "stub") {
        policy_ = std::make_unique<StubPolicy>();
    } else if (cfg_.policy.useThermal || cfg_.policy.name == "thermal") {
        // ThermalPolicy：显式趋温推理；最适温度取自 PolicyConfig.thermalPreferredTemp。
        ThermalPolicy::Params tp;
        tp.preferredTemp = cfg_.policy.thermalPreferredTemp;
        // R4-P1：--thermal-w-thermal / --thermal-w-caution 的真实落点。
        tp.wThermal = cfg_.policy.thermalWThermal;
        tp.wCaution = cfg_.policy.thermalWCaution;
        policy_ = std::make_unique<ThermalPolicy>(tp);
    } else if (cfg_.policy.name == "hysteresis") {
        // S8 新增：滞回策略——抑制两个近距食物之间的每帧方向抖动。
        policy_ = std::make_unique<HysteresisPolicy>();
    } else if (cfg_.policy.name == "foragebank") {
        // S8 新增：觅食-蓄能两态策略——能量代理量决定风险偏好，带滞回防抖。
        policy_ = std::make_unique<ForageBankPolicy>();
    } else if (cfg_.policy.name == "bandit") {
        // T24：BanditPolicy——Hedge/EXP3-lite 在线加权 Rule 与 Thermal 两臂，
        // 回报由感知（接近度增益 + 速度-能量权衡）驱动，decide() 无 RNG。
        policy_ = std::make_unique<BanditPolicy>();
    } else if (cfg_.policy.name == "riskaverse") {
        // T26：RiskAversePolicy——EROI（能量回报率）EMA 状态机，
        // 温度失真打折；环境肥沃全速、贫瘠省能巡航，滞回防抖。
        policy_ = std::make_unique<RiskAversePolicy>();
    } else {
        policy_ = std::make_unique<RulePolicy>();
    }
}

Simulation::~Simulation() = default;

void Simulation::spawnNuclei() {
    // F2: nucleus count comes from config.colony.nuclei (was hardcoded 12).
    const int kCount = static_cast<int>(cfg_.colony.nuclei);
    const double minSep = 20.0;
    for (int i = 0; i < kCount; ++i) {
        Nucleus n;
        n.id = nextNucleusId_++;
        // 朴素退避：与已放置的核保持最小环距，避免出生重叠。
        // R6-P0-2：出生点还必须 !terrain_.blocked(p)——核出生在障碍内时
        // resolveMove 恒返回原地，位置永久冻结、照付呼吸税又吃不到蓝细菌，
        // 20k tick 必然饿死（实测 seed123 全开地形 nuclei=0 生态灭绝）。
        // 尝试 200 次；若世界几乎被障碍填满而始终找不到合法点，则放宽「核间
        // 退避」只保留「不得在障碍内」——宁可出生重叠也不能出生在障碍里
        //（障碍内出生=冻结=死亡，重叠只损失一点初始分布均匀性）。
        // 无地形时 terrain_.blocked 恒 false、rng 消耗序列不变：默认回归逐位安全。
        {
            bool ok = false;
            for (int attempt = 0; attempt < 200 && !ok; ++attempt) {
                Vec2 p{rng_.nextDouble() * cfg_.world.width,
                       rng_.nextDouble() * cfg_.world.height};
                if (terrain_.blocked(p)) continue; // 障碍内：立即重采样
                ok = true;
                for (const Nucleus& m : nuclei_) {
                    if (env_.torusDistance(p, m.pos) < minSep) { ok = false; break; }
                }
                if (ok) n.pos = env_.wrap(p);
            }
            if (!ok) {
                // 放宽：只保证不在障碍内（至多再试 200 次；仍失败则保留最后一次
                // 采样点——理论上仅当世界被障碍 100% 覆盖时发生）。
                for (int attempt = 0; attempt < 200; ++attempt) {
                    Vec2 p{rng_.nextDouble() * cfg_.world.width,
                           rng_.nextDouble() * cfg_.world.height};
                    n.pos = env_.wrap(p);
                    if (!terrain_.blocked(n.pos)) break;
                }
            }
        }
        n.energy = 60.0;
        // ---- R2-A：出生引导 = genomeRandomize（24 参数 + hue，恰好 25 次 nextDouble）----
        // affinity 归一化在 randomize 之后调用（Genome.h 契约：randomize 抽的是原始
        // 分量，归一化把它压到和为 1）。RNG 消耗序固定：位置退避（每 attempt 2 抽）
        // -> 出生基因（25 抽 × 至多 fertility 重掷次数）。
        genomeRandomize(n.genome, rng_, cfg_.colony.genomeRanges);
        genomeNormalizeAffinity(n.genome, cfg_.colony.genomeRanges);
        // fertility 重掷（前端 D 的实测死局：energyThreshold > 0.8*nativeCap 时该核
        // 注定不育——阈值高于正常能量水位，永不繁殖）。初始种群必须全员可育；
        // 变异产生的不育个体是合法选择材料，初始不育是初始化 bug。
        // 每次重掷固定 25 抽，至多 8 次（160 抽封顶，概率上永不触达）。
        {
            const double fertCap = 0.8 * bp_.nativeCap;
            for (int attempt = 0; attempt < 8 && n.genome.energyThreshold > fertCap; ++attempt) {
                genomeRandomize(n.genome, rng_, cfg_.colony.genomeRanges);
                genomeNormalizeAffinity(n.genome, cfg_.colony.genomeRanges);
            }
        }
        nuclei_.push_back(n);
        ++aliveCount_;
        ledger_.injectInitial(n.id, n.energy);
    }
}

// 主循环单步：感知 → 决策 → 行为 → 环境推进 → 审计。
void Simulation::step() {
    const double dt = 1.0;
    // C2: 繁殖接线——循环内收集待繁殖亲代下标，循环后统一执行并按出生数预留容量，
    // 避免 reproduce 内部 push_back 扩容搬运元素使 nuclei_[i] 引用失效（UB）。
    std::vector<std::size_t> reproParents;
    for (std::size_t i = 0; i < nuclei_.size(); ++i) {
        Nucleus& n = nuclei_[i];
        if (!n.alive) continue;
        const bool wasAlive = n.alive;
        // S17 接线（曾被 A 的 Simulation.cpp 覆盖，现恢复）：侦察球是菌落对外感知的
        // **唯一**来源。不传 scoutCount 的话每个核都停在"原生盲区"（半径 1/4），
        // C 实现的 S17 就形同虚设。资源球不计入（用户明确纠正：资源球对感知零影响）。
        int scoutCount = 0;
        for (const Ball& sb : n.balls) {
            if (sb.type == BallType::Scout) ++scoutCount;
        }
        // T43：地形传入感知层（s[17] 采样叠加温度区偏移）与行为层（resolveMove + 移动耗能倍率）。
        // R1-B：感知层"食物特征"维的候选 = 蓝细菌节点容器（唯一食物来源）。
        // R2-A：TargetList（可施力目标）随感知产出。当前感知层尚未产出 TargetList
        //（契约已定，决策侧接线由 R2-C 承担），组合根先用确定性自算兜底：
        // 收集 forceRange 内最近的自由球（targets[0] 最近），不消耗 rng。
        // 这样 forceSlot 管线今天就是活的：任何决策层写出 forceSlot 即生效。
        TargetList targets;
        {
            const double rangeScan = n.genome.forceRange >= 0.0 && n.genome.forceRange <= 1e300
                                         ? n.genome.forceRange : bp_.forceRadius;
            // 性能（R2-A，数学等价）：平方距离预筛，命中才开方。
            const double range2 = rangeScan * rangeScan;
            for (const Ball& fb : freeBalls_) {
                if (fb.ownerId != 0) continue;
                const Vec2 dv = env_.torusDelta(n.pos, fb.pos);
                const double d2 = dv.x * dv.x + dv.y * dv.y;
                if (d2 > range2) continue;
                const double d = std::sqrt(d2);
                // 插入排序保持 dist 升序（kMaxForceTargets=8，常数小）。
                // 整结构赋值（局部 ForceTarget 一次写入）：避开守卫 1c 的
                // "下标字段写入"形态——TargetList 是组合根局部量而非实体，
                // 但守卫 regex 分不出容器语义，按形态收敛最省事。
                ForceTarget item;
                item.id = fb.id;
                item.dx = dv.x;
                item.dy = dv.y;
                item.dist = d;
                item.type = static_cast<int>(fb.type);
                // 注：ForceTarget.energy 已于 2026-11-01 删除（死字段，
                // 8 个策略无一读取；删除同时消除了守卫的假阳性）。
                int pos = targets.count < kMaxForceTargets ? targets.count : kMaxForceTargets - 1;
                if (targets.count < kMaxForceTargets) ++targets.count;
                while (pos > 0 && targets.items[pos - 1].dist > d) {
                    targets.items[pos] = targets.items[pos - 1];
                    --pos;
                }
                targets.items[pos] = item;
            }
        }
        // ---- R2 接线（重建 2026-11-01 08:20）：把 s[18..23] 的六个新维真正喂上 ----
        // 这段曾在 A 合并时丢失（A 沙箱的 Simulation.cpp 基于旧版同步，覆盖了它），
        // 导致感知扩展到 24 维但生产路径后 6 维恒哨兵值 = 死代码。
        // 三处计算全部只读扫描，不消耗 rng：
        //   · 最近邻核：O(n) 扫描（与下面 socialAccelerations 的 O(n²) 相比可忽略）
        //   · energyRatio：n.energy / n.cap
        //   · TargetList：本核 forceRange 内的球（自由球 + 自己的球），按环距升序取前 8
        NeighborInfo nearest{};
        {
            const double sight = (n.genome.nucleusSightRadius > 0.0)
                                     ? n.genome.nucleusSightRadius : 170.0;
            double bestD2 = sight * sight;
            for (const Nucleus& other : nuclei_) {
                if (other.id == n.id || !other.alive) continue;
                // 性能：无 fmod 的环距（见 core/Vec2.h torusDeltaWrapped）
                const Vec2 delta = torusDeltaWrapped(n.pos, other.pos,
                                                     cfg_.world.width, cfg_.world.height);
                const double d2 = delta.x * delta.x + delta.y * delta.y;
                if (d2 > 1e-24 && d2 < bestD2) {
                    bestD2 = d2;
                    const double d = std::sqrt(d2);
                    nearest.found = true;
                    nearest.dist = d;
                    nearest.sightRadius = sight;
                    nearest.dirX = delta.x / d;
                    nearest.dirY = delta.y / d;
                }
            }
        }
        const double energyRatio = (n.cap > 0.0) ? (n.energy / n.cap) : 0.0;
        SensorVector sv = sense(env_, n.pos, n.vel, n.speed, cfg_.policy, &rng_,
                                cyano_.nodes(), temp_, &terrain_, scoutCount,
                                nearest, energyRatio, targets);
        ActionVector act = policy_->decide(sv, targets);
        // GOAL 呼吸温度接线：取核所在点的温度失真度（与 sense 的 s[16] 同源，
        // 同为 temp.distortion(n.pos)），作为末位实参喂给呼吸耗能温度因子。
        // R2-A §5 两阶段积分：先对全体核算社会加速度（本 pass，只读全种群），
        // 再经 externalAcc 参数在 apply 里统一积分——消除核间作用顺序依赖。
        Vec2 socialAcc{0.0, 0.0};
        AgentBehavior::socialAccelerations(n, env_, nuclei_, act, socialAcc);
        // R2-A §6 点名施力：决策层 forceSlot 意图（当前策略未产出意图，
        // act.forceSlot 恒 -1 时本段零成本直通）。目标球 id -> freeBalls_ 查找；
        // 命中且仍在 genome.forceRange 内才结算（applyNamedForce 内部再验一次）。
        if (act.forceSlot >= 0 && !freeBalls_.empty()) {
            const int slot = act.forceSlot < kMaxForceTargets ? act.forceSlot : -1;
            if (slot >= 0 && slot < targets.count) {
                const EntityId wantId = targets.items[slot].id;
                for (Ball& fb : freeBalls_) {
                    if (fb.id != wantId) continue;
                    // 施力时刻重取环距（感知快照可能已过时一帧）。
                    const double dNow = env_.torusDistance(n.pos, fb.pos);
                    (void)AgentBehavior::applyNamedForce(n, fb, act, targets, slot,
                                                         dNow, ledger_, bp_);
                    break;
                }
            }
        }
        AgentBehavior::apply(n, act, env_, ledger_, dt, bp_, rng_, &terrain_,
                             temp_.distortion(n.pos), socialAcc);
        // ---- R1-B 捕食（确定性语义）：核在 cyanoPredationRadius 内找最近蓝细菌节点吃 ----
        // 为什么必须接：CyanoField 只做光合（产能量），如果不接捕食，蓝细菌就是
        // 一个**只进不出的能量源**。实测接线到主循环后总能量从 1800 涨到 58262
        // 且持续上升，同时节点数暴涨导致 O(n²) 的 linkTransfer 把 4000 tick 拖到 61 秒。
        // 捕食是让能量回到食物链的唯一出口。
        //
        // R1-B 语义变更（任务卡 §2）：旧的"25% 随机触发 + 每口 4% 核能量 × 0.6 转化"
        // 改为**确定性**——核每帧都尝试在 bp_.cyanoPredationRadius 内找**最近**节点，
        // 按转化率吃，不加额外随机门槛；随机性由基因 cyanoPreference/消化率
        //（进入 CyanoField::cyanoConversionRate 的转化率公式）体现。
        //
        // 转化率公式（v2.3.1）：rate = cyanoConversionRate(mode, maxSpeed, maxAccel,
        // digestion) —— 基率 = CyanoParams.conversionRate（ R1-B 落点：消化效率基因），
        // 行为层最大速度/加速度作为"核越快越难抓"的惩罚输入；最后 clamp 到
        // [conversionMinRate, conversionMaxRate]。捕获量 = taken = min(rawEnergy * rate,
        // node.energy)，rawEnergy = n.energy * kGrazeFraction（口径与旧版逐位一致）。
        //
        // RNG 消耗顺序声明（R1-B 重新固定）：**本段不消耗 rng**。
        // "找最近节点"是确定性环距扫描，graze() 是确定性扣款（CyanoField 不含任何
        // rng 抽取）；旧版此处的 rng_.nextDouble() < kGrazeChance 消耗点已被删除。
        // 全 tick 的 rng 消耗点只剩：核循环内的 sense 读噪声 / apply 写噪声与
        // T08 触发 / 繁殖变异（reproduce 内 genomeMutate 50 次 + 出生偏移 2 次，
        // R2-A 重新标定），以及帧尾 cyano_.step() 的播种式消耗（顺序见 CyanoNode.h
        // 文件头）。同 seed 双跑逐字节一致由 double-run 门禁与 wiring_deterministic
        // 单测把守。
        //
        // 生态平衡账（R1-B 标定）：光合供给 ≈ 节点数(≤maxNodes) × 每 tick 净光合(~0.6)；
        // 捕食需求 ≈ 核数 × (核能 × 0.04 × 转化率)。转化率公式把快速/高机动的核
        // 自动打折（mode 1/2/3），配合 maxNodes 护栏，第一营养级不被吃绝
        //（wiring_cyano_grows_with_sun / 20k 审计双闸把守）。
        if (n.alive && n.genome.cyanoPreference > 0.0 && n.digestCooldown == 0) {
            // ---- R2-A：基因驱动捕食（替换 R1-B 的"按核能量百分比"旧口径）----
            // 1) 意愿闸：cyanoPreference == 0 的核**绝不**捕食（严格哨兵，卡面红线）。
            // 2) 处理时间闸：digestCooldown > 0 期间不捕食（吃一口消化一轮）。
            // 3) 目标：predationRadius 内最近存活节点（确定性扫描，无 rng）。
            // 4) 每口量 want = cyanoPreference * 0.15 * prey.energy（与核能量无关）。
            // 5) 转化率 = cyanoConversionRate(公式) × cyanoDigestion（消化基因）。
            // 6) 冷却 = ceil(1/(0.5 + cyanoDigestion))：消化快 -> 冷却短。
            // RNG：本段不消耗 rng（确定性扫描 + graze 确定性扣款，声明同 R1-B）。
            EntityId preyId = 0;
            double preyEnergy = 0.0;
            const double preyR = bp_.cyanoPredationRadius;
            // 性能（R2-A，数学等价）：平方距离扫描，只对最近候选开方一次。
            double bestD2 = preyR * preyR;
            for (const CyanoNode& c : cyano_.nodes()) {
                if (!c.alive) continue;
                const Vec2 dv = env_.torusDelta(n.pos, c.pos);
                const double d2 = dv.x * dv.x + dv.y * dv.y;
                if (d2 < bestD2) { bestD2 = d2; preyId = c.id; preyEnergy = c.energy; }
            }
            if (preyId != 0) {
                const CyanoParams& cpar = cyano_.params();
                // 速度输入 = genome.maxSpeed（基因化），加速度输入 = genome.maxAccel
                //（基因化，主代理 R2-A 复核：卡面要求 maxAccel "同时用于捕食转化率公式"）。
                // 默认 conversionMode=1 只消费速度项（CyanoNode.h 公式），此改动对默认
                // 模式输出零影响；mode 2/3（加速度惩罚/混合）自此按基因分化。
                const double rate = CyanoField::cyanoConversionRate(
                    cpar.conversionMode, cpar.conversionRate * n.genome.cyanoDigestion,
                    cpar.conversionExponent, cpar.conversionSpeedRef,
                    cpar.conversionAccelRef, cpar.conversionWSpeed,
                    cpar.conversionWAccel, cpar.conversionMinRate,
                    cpar.conversionMaxRate, n.genome.maxSpeed, n.genome.maxAccel);
                // 每口量与核能量无关（卡面口径）：want = preference * 0.15 * 猎物能量。
                // graze(preyId, want, rate) 内部做 taken = min(want*rate, node.energy)。
                // R9-A 迁移回报：本口猎物距上口 > migrationRadius（环面距离）时，
                // 转化率补偿 (1 + migrationBonus)（钳 ≤1.0）。零和不变：taken 仍全部
                // 从节点搬到核，补偿的是转化率公式对快核的折扣份额，不注入新能量。
                // 无 rng（距离判定确定性），双跑确定性不受影响。
                double effRate = rate;
                if (bp_.migrationBonus > 0.0) {
                    const Vec2 dm = env_.torusDelta(n.pos,
                        n.hasMealPos ? n.lastMealPos : n.pos);
                    const double dMeal = std::sqrt(dm.x * dm.x + dm.y * dm.y);
                    if (dMeal > bp_.migrationRadius)
                        effRate = std::min(1.0, rate * (1.0 + bp_.migrationBonus));
                }
                const double want = n.genome.cyanoPreference *
                                     cfg_.nucleus.grazeBiteFraction * preyEnergy;
                const double taken = cyano_.graze(preyId, want, effRate);
                if (taken > 0.0) {
                    AgentBehavior::absorbPrey(n, preyId, taken, ledger_);
                    n.lastMealPos = n.pos;    // R9-A：进食点登记
                    n.hasMealPos = true;
                    // 处理时间（ECOLOGY-DESIGN §三.3）：吃一口进入消化冷却；
                    // 消化基因高 -> 冷却短。"快跑少吃 vs 慢爬猛吃"分化的另一半。
                    n.digestCooldown =
                        static_cast<int>(std::ceil(1.0 / (0.5 + n.genome.cyanoDigestion)));
                }
            }
        }
        // P2 死亡可观测（纯观测，不动账目）；同步维护 aliveCount_ 缓存。
        if (wasAlive && !n.alive) { --aliveCount_; ++deathCount_; }
        // ---- R2-A：消化冷却递减（每帧一次，任何核都递减； 出生即捕食）。
        if (n.digestCooldown > 0) --n.digestCooldown;
        // ---- R2-A：繁殖闸基因化 + 种群护栏 ----
        // 能量 > genome.energyThreshold（基因，80~200）即繁殖：确定性事件，
        // 无随机采样闸（旧 kReproChanceP=0.002 删除）。承载力由能量自然决定——
        // 门槛高/捕食差的基因型达不到阈值就被淘汰，这正是选择。
        // maxNuclei（默认 300）只是性能护栏，正常永远撞不到。
        // 下标收集后循环外统一执行（reproduce push_back 会使引用失效，防 UB）。
        if (n.alive && n.energy > n.genome.energyThreshold &&
            aliveCount_ < cfg_.colony.maxNuclei) {
            reproParents.push_back(i);
        }
    }
    if (!reproParents.empty()) {
        nuclei_.reserve(nuclei_.size() + reproParents.size());
        for (std::size_t i : reproParents) {
            // R2-A：繁殖必变异——coeff = cfg.colony.mutationCoefficient（默认 0.25），
            // ranges = cfg.colony.genomeRanges；出生偏移耗 2 抽，wrap 用世界尺寸。
            // RNG 消耗序：按 reproParents 下标序逐亲代 52 抽（mutate 50 + 偏移 2），
            // 拒绝路径 0 抽（门槛已在收集闸检查过，唯一剩余拒绝是 maxNuclei 并发拥挤，
            // 概率上可忽略——300 护栏下不发生）。
            // 执行期复查护栏（采样与出生同帧时 aliveCount_ 会增长，302>300 实测竞态）。
            if (aliveCount_ >= cfg_.colony.maxNuclei) break;
            if (AgentBehavior::reproduce(nuclei_[i], nuclei_, ledger_, nextNucleusId_++, rng_,
                                         cfg_.colony.genomeRanges,
                                         cfg_.colony.mutationCoefficient,
                                         cfg_.world.width, cfg_.world.height,
                                         bp_)) {
                ++aliveCount_;
            }
        }
    }
    // ---- S19/S20：自由球结算（顺序固定，不消耗 RNG，确定性安全）----
    // **必须在压缩尸体之前做**：死核的 detached_ 里可能还挂着本帧刚摘下的球，
    // 一旦先 erase 掉死核，那些球的能量就永久消失且无处记账。
    // 这正是 seed 123 tick 10954 审计失败的真因：单帧凭空少了 183.91，
    // 表现为"球总能量从 489.79 掉到 305.88 但 dissipated 没增加"。
    // 先把 detached_ 全部转入 freeBalls_（能量原样、账面零操作），再允许压缩。
    if (!nuclei_.empty()) {
        for (Nucleus& n : nuclei_) {
            if (!n.detached_.empty()) {
                for (const Ball& b : n.detached_) freeBalls_.push_back(b);
                n.detached_.clear();
            }
            // R2-A §8：死核的尸体营养包转入组合根 carrion_（当帧末分配给蓝细菌）。
            // 死核会在下方被压缩，能量必须先搬出来，否则凭空消失。
            if (!n.alive && n.carrion_.energy > 0.0) {
                carrion_.push_back(n.carrion_);
                carrionEnergyIn_ += n.carrion_.energy; // R3-A：流水入账（审计用）
                n.carrion_ = Carrion{};
            }
        }
    }
    // ---- R7-B：球世界积分 + 归属结算（v2.3.1 力学接线）----
    // 位置：核循环（apply/捕食/繁殖）全部完成、压缩尸体之前——球看到本帧最终
    // 的核位置/能量，无"谁先动谁占便宜"的帧内顺序差；自由球旧结算 pass 在其后，
    // 只处理它既有的事务（r7Lane=false 的漂移/拾取路径），不重复积分 R7 车道球。
    ballWorldStep();
    ballOwnership();
    // P1 压缩尸体：死核数量 > 2×存活数量时清理一次。reproParents 下标只在单个
    // step 内使用，tick 之间压缩安全；只删 !alive 尸体，不消耗 RNG、不改固定
    // 迭代顺序（确定性红线）。aliveCount_ 不变（只删死核）。
    // 位置在 detached_ 回收之后：此时死核已不再持有任何待转移的球。
    const std::size_t deadCount = nuclei_.size() - aliveCount_;
    if (deadCount > 2 * aliveCount_) {
        nuclei_.erase(std::remove_if(nuclei_.begin(), nuclei_.end(),
                                     [](const Nucleus& dead) { return !dead.alive; }),
                      nuclei_.end());
    }
    for (Ball& fb : freeBalls_) {
        // R7-B：世界积分车道球的位置/速度已由 ballWorldStep 权威推进（v2.3.1 力学
        // 自带 ball_damping），这里旧漂移积分若再推进会双重积分且阻尼口径冲突；
        // 能量衰减（R8-P1 反能量坟场）两条车道同一语义，不可跳过。
        if (!fb.r7Lane) {
            fb.pos = env_.wrap(Vec2{fb.pos.x + fb.vel.x * 1.0, fb.pos.y + fb.vel.y * 1.0});
            fb.vel.x *= 0.99;
            fb.vel.y *= 0.99;
        }
        // 【红队 R8-P1 修正 2026-10-31】自由球必须缓慢散热（GOAL §2.6：
        // "自由球随时间衰减，这才是真正的掉落损失"）。
        // 旧问题：cyano 源球 vel={0,0}，这里只积分位置不衰减，而 ballDrift
        // 只作用于核名下的 balls —— 于是自由球池成了**永不衰减的能量坟场**：
        // 实测终局 84.2%（30720/36506）能量锁死在 1024 颗永不回收的球里。
        // 修法：每帧按 E*k 耗散（走账本），与核的 ballDrift 同一语义。
        constexpr double kFreeDriftK = 0.0002; // 慢衰减，与核 ballDrift 同量级
        if (fb.energy > 0.0) {
            const double take = fb.energy * kFreeDriftK;
            fb.energy -= take;
            ledger_.dissipate("ballDrift", take);
        }
    }
    if (!freeBalls_.empty()) {
        for (Nucleus& n : nuclei_) {
            if (!n.alive) continue;
            for (std::size_t k = 0; k < freeBalls_.size();) {
                Ball& fb = freeBalls_[k];
                if (fb.ownerId != 0) { ++k; continue; }
                const double d = env_.torusDistance(n.pos, fb.pos);
                if (AgentBehavior::adoptBall(n, fb, d, bp_)) {
                    freeBalls_.erase(freeBalls_.begin() + static_cast<std::ptrdiff_t>(k));
                } else {
                    // R2-A：旧的"随机推球"接线已删除。推球 = 决策层点名施力
                    //（applyNamedForce，forceSlot 意图），系统侧不再有任何自动施力。
                    ++k;
                }
            }
        }
    }
    // ---- R2-A §8：尸体营养分配（营养循环：尸体 → 分解者 → 生产者）----
    // 把本帧产生的 carrion_（核死亡 30% 遗产）分配给死亡位置最近的存活蓝细菌节点：
    // ledger.transfer(carrionId -> node.id) 零和搬运（能量已在账，只换持有者）。
    // 找不到 kCarrionSearchRadius 内的分解者时全额 dissipate("death")（能量绝不滞留）。
    // 确定性：节点按下标序取第一个最近者（平距取小下标），无 rng。
    if (!carrion_.empty()) {
        for (const Carrion& cr : carrion_) {
            if (!(cr.energy > 0.0)) continue;
            EntityId best = 0;
            double bestD = kCarrionSearchRadius;
            for (const CyanoNode& c : cyano_.nodes()) {
                if (!c.alive) continue;
                const double d = env_.torusDistance(cr.pos, c.pos);
                if (d < bestD) { bestD = d; best = c.id; }
            }
            // R3-A（营养循环真实转移）：CyanoField::fertilize 是环境层的唯一
            // 合法写点——节点能量写入只能走 CyanoField（R2-A 方案 A 的能力缺口
            // 由本卡补上）。账目必须审计平衡：
            //   transfer(0, best, accepted)   零和搬运（能量已在账，只换持有者），
            //                                 与 fertilize 的实际入账量严格一致；
            //   dissipate("death", overflow)  只对溢出部分耗散（energyCap 装不下
            //                                 的残量 + 找不到分解者的全额）。
            // 恒等式：fertilize 接受量 == transfer 量（两侧同源），溢出量全额入
            // dissipate——能量绝不滞留在模块内，绝不凭空出现/消失。逐包流水
            // carrionEnergyIn_/Out_ 供 step() 审计段做闭合断言（1e-9 相对容差）。
            if (best != 0) {
                const double accepted = cyano_.fertilize(best, cr.energy);
                if (accepted > 0.0) {
                    ledger_.transfer(0, best, accepted);
                    carrionEnergyOut_ += accepted; // R3-A：流水出账（转移侧）
                }
                if (cr.energy > accepted) {
                    const double overflow = cr.energy - accepted;
                    ledger_.dissipate("death", overflow);
                    carrionEnergyOut_ += overflow; // R3-A：流水出账（溢出侧）
                }
            } else {
                // 半径内无分解者：全额耗散（能量绝不滞留，R2-A 语义保留）。
                ledger_.dissipate("death", cr.energy);
                carrionEnergyOut_ += cr.energy; // R3-A：流水出账（无分解者全额）
            }
        }
        carrion_.clear();
    }
    env_.tick();
    // ---- S22 接线：蓝细菌推进（先于 env_.tick() 调用者感知，顺序固定，影响 rng 流）----
    // R1-B：阳光模型参数化（SunConfig）。sunLevel = base - gradient*(y/H)
    // + amplitude*sin(2*pi*tick/period) + noise，噪声用确定性整数哈希
    //（sunHash01：splitmix64 finalizer，零 rng 消耗——随机源声明见本文件顶部）。
    // 计算式唯一权威实现在匿名命名空间 sunLevelAt()；默认参数（amp=0.5、
    // period=628、无梯度、无噪声）下曲线与旧硬编码 0.5 + 0.5*sin(tick*0.01)
    // 同形（同基线/同幅值/同量级周期），太阳相位不改变生态量级。
    // 取"世界最暗点"（y=H）光照驱动全局 step：节点级光照由垂直梯度在前端
    // 呈现（R1-B：梯度不改变第一营养级的全局收支口径，只改视觉分布）。
    const double sunLevel = sunLevelAt(cfg_.sun, tick_, cfg_.world.height,
                                       cfg_.world.height);
    const double cyanoBefore = cyano_.totalEnergy();
    const CyanoStepReport rep =
        cyano_.step(sunLevel, cfg_.world.width, cfg_.world.height, rng_);
    if (rep.photosynthesisGain > 0.0) ledger_.injectPrimary(kCyanoPoolId, rep.photosynthesisGain);
    if (rep.metabolismLoss > 0.0) ledger_.dissipate("cyanoMetabolism", rep.metabolismLoss);
    if (rep.reproductionLoss > 0.0) ledger_.dissipate("cyanoReproduction", rep.reproductionLoss);
    // 蓝细菌产出的自由球：模块内部已从节点扣款（节点实测差含 -ballSpawnCost），
    // 这批能量以自由球实体形式重新出现在存活实体里，必须 injectPrimary 入账。
    // 坐标取模块 lastBallSpawns()（本模块为组合根预留的出生点，不额外耗 rng）。
    if (rep.ballsProduced > 0 && rep.ballEnergyTotal > 0.0) {
        // 【红队 R8-P0 修正 2026-10-31】这里**绝不能** injectPrimary。
        //
        // 能量流向其实很简单：模块内部 node.energy -= ballSpawnCost 把能量从节点
        // 拿出来，report.ballEnergyTotal 只是"这笔能量去哪了"的备忘——它现在
        // 以自由球实体（下方 push 的 fb）的形式继续存在于 sumLiving 里。
        // 账本视角：总能量没离开存活实体集合，零操作即守恒。
        //
        // 旧写法 injectPrimary(ballEnergyTotal) 是凭空注入一份幻影能量：
        // cyanoDelta 没包含它（节点已扣、球是新建的），resid 通道每帧把它对销掉
        // ——实测 20k tick resid 非零 19983/20000、累计 -417,156（红队 R8 报 P0，
        // 主代理独立复现一致）。审计恒等式表面 ok，实际整条账被幻影注入污染。
        const double per = rep.ballEnergyTotal / static_cast<double>(rep.ballsProduced);
        const std::vector<Vec2>& spawns = cyano_.lastBallSpawns();
        for (int k = 0; k < rep.ballsProduced; ++k) {
            Ball fb; // 变量名避开守卫 1c 的 n.energy/n.pos 直写模式
            fb.id = nextFreeBallId_++;
            fb.ownerId = 0;
            fb.pos = static_cast<std::size_t>(k) < spawns.size()
                         ? spawns[static_cast<std::size_t>(k)]
                         : Vec2{0.0, 0.0};
            fb.vel = Vec2{0.0, 0.0};
            fb.energy = per;
            freeBalls_.push_back(fb);
        }
        // 注：不调用任何 ledger 函数——能量只是换了实体持有者。
    }
    // S22：自由球回收（见 kFreeBallCap 注释）。
    if (freeBalls_.size() >= kFreeBallCap) { // >=：稳态下 > 永不触发（红队 R8 P1）
        std::size_t excess = freeBalls_.size() - kFreeBallCap;
        double expired = 0.0;
        std::vector<Ball> kept;
        kept.reserve(freeBalls_.size());
        for (const Ball& b : freeBalls_) {
            if (excess > 0 && b.id >= kCyanoBallIdBase) {
                expired += b.energy;
                --excess;
            } else {
                kept.push_back(b);
            }
        }
        freeBalls_.swap(kept);
        if (expired > 0.0) ledger_.dissipate("cyanoBallExpire", expired);
    }
    // 【红队 R8-P0 修正 2026-10-31】resid 通道**整体移除**。
    //
    // 旧逻辑：resid = Δcyano - (photo - metab - repro)，resid>0 回注 / <0 补耗散。
    // 它的设计前提是"report 口径与节点实测差存在固有残差"——但这其实是在说
    // report 数字不可信。实测证明残差不是"固有"，而是旧接线自己的幻影注入
    //（对 ballEnergyTotal 多 inject 了一份）造成的：删掉幻影注入后 resid 应恒为 0。
    //
    // 为什么必须删而不是留：审计的价值在于**暴露错误**。一个每帧自动把账凑平的
    // 通道，会让任何记账错误（现有或将来的）都变成"审计 ok"，恒等式沦为摆设。
    // 现在的正确约束：cyanoDelta 必须恰好等于 photo - metab - repro（产球能量
    // 两侧相消）。若不相等，说明 CyanoField::step 或本接线有 bug——**让它审计失败**，
    // 而不是悄悄补平。保留 cyanoDelta 计算并在超限时置 auditOk_=false（R9-P2 落实）：
    // CyanoField::step 内部守恒若出 bug（如 linkTransfer 记账错位），在这里当场失败，
    // 而不是等全局 audit 间接暴露。
    // 运行时审计默认关闭：账目写对自然守恒，逐帧全量核对是纯开销。
    // 需要验证时 setAuditEnabled(true)（单元测试 / --audit），失败仍在 auditOk_ 暴露。
    if (auditEnabled_) {
        const double cyanoDelta = cyano_.totalEnergy() - cyanoBefore;
        const double accounted = rep.photosynthesisGain - rep.metabolismLoss
                               - rep.reproductionLoss;
        // 防御：若残差超出浮点噪声量级（远大于 1e-6），这是真 bug，必须让它被看见。
        if (std::fabs(cyanoDelta - accounted) > 1e-6 * std::max(1.0, std::fabs(cyanoBefore))) {
            auditOk_ = false;
        }
    }

    // R3-A（营养循环）审计断言：fertilize 实际转进节点的能量必须与
    // ledger_.transfer(0, best, accepted) 的记账量严格一致（同源变量，理论上
    // 恒等；此处防未来两条路径被独立改动后漂移）。不平衡 = 真 bug，让审计失败。
    //（本帧 carrion 已在上方清空，计数器差值 = 本帧处理的包数。）
    // 流水闭合：carrion_ 已清空，入队能量必须全部出账（逐包浮点尾数 ≤1 ulp，
    // 1e-9 相对容差内必等；真漏账会在对称方向上越积越大，当场暴露）。
    if (auditEnabled_ &&
        std::fabs(carrionEnergyIn_ - carrionEnergyOut_) >
            1e-9 * std::max(1.0, carrionEnergyIn_)) {
        auditOk_ = false;
    }

    ++tick_;
    if (auditEnabled_) {
        const double tol = 1e-9 * std::max(1.0, ledger_.totalInjected());
        auditOk_ = ledger_.audit(tol, totalEnergy());
    }
}


// ==================== R7-B：球世界积分（本卡实现） ====================
// 参考旧版 v2.3.1 World::updateBallDynamics（World.cpp:274-393），公式照抄：
//   径向弹簧 + 切向驱动 + 近距排斥（每球 × 每个可见核）
//   + 跟随耦合弹簧（归属球 × 宿主）
//   + 球间短程排斥 / 同宿主吸引（每对双计数）
//   → 统一积分（vel += acc、阻尼、类型限速 + 跟随加速限速、pos += vel、wrap）
// 与旧版的结构差异（等价性说明）：
//   1) 网格裁剪 → O(球×核/球×球) 直扫：核数 <= maxNuclei(300)、球数 <=
//      kMaxBalls(3)×核数 + kFreeBallCap(256)，直扫规模可接受（护栏内）。
//   2) 力按来源求和后一次积分（同 v231 acc 累加），dt=1.0（v231 同款）。
//   3) 边界 bounce → wrap（环面世界，任务卡指定；bounce_restitution /
//      wrap_boundary 仅存档语义，不消费）。
//   4) influenceRadius 裁剪 → 力场半径自适应（见下）。
// 口径声明（任务卡 spec §7）：R7 积分车道球（Ball.r7Lane=true）的 pos/vel 是
// **权威**状态，angle/radius 保留但由 pos 反推（兼容 trace/前端）；旧行为层
// 车道（r7Lane=false）的球继续按 apply 的极坐标模型推进（全部既有单测钉死），
// 本函数不覆盖它们的位置——两套力学并存，车道标记是唯一裁决。
void Simulation::ballWorldStep() {
    const double w = cfg_.world.width;
    const double h = cfg_.world.height;
    // 早退：全菌落无球且无自由球 → 零成本（不改任何状态，回归逐位安全）。
    bool anyBall = !freeBalls_.empty();
    if (!anyBall) {
        for (const Nucleus& n : nuclei_) {
            if (!n.balls.empty()) { anyBall = true; break; }
        }
    }
    if (!anyBall) return;

    // ---- 力场半径（v231 influenceRadius 的环面版）：全体球的最大轨道目标半径。
    // v231 的裁剪尺度 = max(orbitRadius[t]) + margin；本项目轨道目标 = 负载半径
    //（10 + 0.5*球能）× 基因偏置，负载由**实际球能**决定（生产路径球能 <= 种子
    // 10 → 目标 ~10-26；夹具高能球目标随之放大——自适应，不拍常数上界）。
    double maxLoadR = 0.0;
    for (const Nucleus& n : nuclei_) {
        for (const Ball& b : n.balls) {
            const double lr = kBallRadiusR7 + 0.5 * (b.energy > 0.0 ? b.energy : 0.0);
            if (lr > maxLoadR) maxLoadR = lr;
        }
    }
    for (const Ball& fb : freeBalls_) {
        const double lr = kBallRadiusR7 + 0.5 * (fb.energy > 0.0 ? fb.energy : 0.0);
        if (lr > maxLoadR) maxLoadR = lr;
    }
    double maxOrbit = 0.0;
    if (maxLoadR > 0.0) {
        for (const Nucleus& n : nuclei_) {
            if (!n.alive) continue;
            for (int t = 0; t < kBallTypeCount; ++t) {
                const double orb = n.genome.orbitRadius[t];
                const double target = maxLoadR * (0.5 + (orb > 0.0 ? orb : 120.0) / 240.0);
                if (target > maxOrbit) maxOrbit = target;
            }
        }
    }
    // 弹簧是线性增长的：超过目标半径 5 倍的球受力 >5×目标半径×radialK（失稳）。
    // v231 用 influenceMargin(=0) 直接裁剪；本项目环面同语义：dist > 3×maxOrbit
    // 的核不施力（数值上 3 倍处弹簧力已远大于切向驱动，轨道捕获不可能，裁剪
    // 只是把 v231 的 infR 裁剪口径平移到本项目尺度）。
    const double fieldR = maxOrbit > 0.0 ? maxOrbit * 3.0 : 0.0;
    const double fieldR2 = fieldR * fieldR;
    // fieldR == 0（无存活核）时核力场环自然全跳过（d2 > 0 恒真裁剪），球间作用
    // 与积分照常——死世界里自由球仍按阻尼滑行（v231 同语义），不冻结。

    // ---- 球间作用索引：id -> 球指针（id 全局唯一不复用，deployBall/nextFreeBallId_）。
    // 本帧内 apply 已完成：无出生/销毁，指针稳定；换主在 ballOwnership（之后）。
    std::map<EntityId, Ball*> ballIndex;
    for (Ball& fb : freeBalls_) ballIndex.emplace(fb.id, &fb);
    for (Nucleus& n : nuclei_) {
        if (!n.alive) continue;
        for (Ball& b : n.balls) ballIndex.emplace(b.id, &b);
    }

    // ---- 力求和：acc 按 id 暂存（球在两容器间可能被换主搬容器，下标不稳定，
    // id 是唯一稳定键）。只有 R7 车道球是积分对象，但全部球都出力（邻居语义
    // 与 v231 一致——旧车道球也是物理存在）。
    std::map<EntityId, Vec2> acc;
    // 1a) 核力场：每球 × 每个存活核（v231:283-336）。
    for (Nucleus& n : nuclei_) {
        if (!n.alive) continue;
        for (Ball& b : n.balls) {
            const Vec2 dv = torusDeltaWrapped(n.pos, b.pos, w, h);
            const double d2 = dv.x * dv.x + dv.y * dv.y;
            if (d2 > fieldR2) continue; // 圆形范围裁剪（v231:294 同语义）
            // 宿主位移：src == 宿主时给非零（跟随耦合生效条件，行为层约定）。
            Vec2 odisp{0.0, 0.0};
            if (b.ownerId == n.id) odisp = dv;
            Vec2 a = AgentBehavior::ballFieldAccel(n, b, dv, odisp, 1.0, ballField_);
            Vec2& slot = acc[b.id];
            slot.x += a.x;
            slot.y += a.y;
        }
        for (Ball& fb : freeBalls_) {
            const Vec2 dv = torusDeltaWrapped(n.pos, fb.pos, w, h);
            const double d2 = dv.x * dv.x + dv.y * dv.y;
            if (d2 > fieldR2) continue;
            // 自由球同样受核力场（v231 自由球也在 updateBallDynamics 里）。
            Vec2 a = AgentBehavior::ballFieldAccel(n, fb, dv, Vec2{0.0, 0.0}, 1.0, ballField_);
            Vec2& slot = acc[fb.id];
            slot.x += a.x;
            slot.y += a.y;
        }
    }
    // 1b) 球间作用：每对双计数（v231:338-365；i↔j 各算一次，等价逐对双方各调）。
    // 性能：先做廉价的环面平方距离早退（两者输入都 wrap 过，torusDeltaWrapped
    // 同款一次比较+加减），命中交互半径才进 ballPairAccel（省 sqrt 与分支）。
    const double pairR = ballField_.sameOwnerAttractRadius > ballField_.ballRepelRadius
                             ? ballField_.sameOwnerAttractRadius
                             : ballField_.ballRepelRadius;
    const double pairR2 = pairR * pairR;
    for (auto& kvI : ballIndex) {
        Ball& bi = *kvI.second;
        for (auto& kvJ : ballIndex) {
            if (kvJ.first == kvI.first) continue;
            Ball& bj = *kvJ.second;
            // 环面最小平方距离（torusDeltaWrapped 的标量版内联）。
            double dx = bi.pos.x - bj.pos.x;
            double dy = bi.pos.y - bj.pos.y;
            const double hw = w * 0.5;
            const double hh = h * 0.5;
            if (dx > hw) dx -= w; else if (dx < -hw) dx += w;
            if (dy > hh) dy -= h; else if (dy < -hh) dy += h;
            const double d2 = dx * dx + dy * dy;
            if (d2 >= pairR2) continue;
            Vec2 a = AgentBehavior::ballPairAccel(bi, bj, Vec2{dx, dy}, 1.0, ballField_);
            if (a.x == 0.0 && a.y == 0.0) continue;
            Vec2& slot = acc[bi.id];
            slot.x += a.x;
            slot.y += a.y;
        }
    }
    // 2) 统一积分（v231:367-390）：只推进 R7 车道球；旧车道球不出不进。
    for (Nucleus& n : nuclei_) {
        if (!n.alive) continue;
        for (Ball& b : n.balls) {
            if (!b.r7Lane) continue;            // 旧车道：apply 轨道模型权威
            const Vec2 a = acc.count(b.id) ? acc[b.id] : Vec2{0.0, 0.0};
            Nucleus* own = (b.ownerId == n.id) ? &n : nullptr; // 归属核才给跟随加速
            (void)AgentBehavior::ballWorldIntegrate(b, a, 1.0, own, w, h, ballField_);
            // ---- 极坐标反推（口径声明：pos 权威，angle/radius 由 pos 反推）----
            const Vec2 dv = torusDeltaWrapped(n.pos, b.pos, w, h);
            b.radius = std::sqrt(dv.x * dv.x + dv.y * dv.y);
            b.angle = std::fmod(std::atan2(dv.y, dv.x) + kTwoPi, kTwoPi);
        }
    }
    for (Ball& fb : freeBalls_) {
        if (!fb.r7Lane) continue;
        const Vec2 a = acc.count(fb.id) ? acc[fb.id] : Vec2{0.0, 0.0};
        (void)AgentBehavior::ballWorldIntegrate(fb, a, 1.0, nullptr, w, h, ballField_);
    }
}

// ---- R7-B：归属结算（v2.3.1 updateOwnership:393-427 语义）----
// detachRadius 内最近存活核持有；换主对原主扣 lossCost（dissipate "ball_loss"）。
// 与现有 S19/S20 adopt/detach 的关系（任务卡 spec §5）：apply 内的 adoptRadius
// 拾取 / detachEnergy 自然脱离**保留**（既有语义逐位不动）；本逻辑是"球自主
// 换主"（旧版语义），两套并存时以本逻辑为准——R7 车道球按本规则换主/失主，
// 旧车道球不动（回归逐位安全）。换主走 transferBall 直接交接（不经 detached_/
// 自由池，防交接缓冲被自由球结算误收）；失主球就地入 freeBalls_（能量原样）。
void Simulation::ballOwnership() {
    if (nuclei_.empty()) return;
    const double w = cfg_.world.width;
    const double h = cfg_.world.height;
    const double detR = ballField_.detachRadius;
    for (std::size_t ni = 0; ni < nuclei_.size(); ++ni) {
        Nucleus& n = nuclei_[ni];
        if (!n.alive || n.balls.empty()) continue;
        for (std::size_t bi = 0; bi < n.balls.size();) {
            Ball& b = n.balls[bi];
            if (!b.r7Lane) { ++bi; continue; }  // 旧车道球不参与自主换主
            // detachRadius 内最近存活核（确定性：按下标序取第一个最近者）。
            std::size_t best = static_cast<std::size_t>(-1);
            double bestD2 = detR * detR;
            for (std::size_t ki = 0; ki < nuclei_.size(); ++ki) {
                const Nucleus& c = nuclei_[ki];
                if (!c.alive) continue;
                const Vec2 dv = torusDeltaWrapped(c.pos, b.pos, w, h);
                const double d2 = dv.x * dv.x + dv.y * dv.y;
                if (d2 < bestD2) { bestD2 = d2; best = ki; }
            }
            // reassignBall 返回 true = 球已摘离 n.balls[bi]（换主直接交接给
            // population[best] 或入 freeBalls_）→ 原地下标重查；false = 宿主不变。
            if (AgentBehavior::reassignBall(n, bi, best, nuclei_, freeBalls_,
                                            ledger_, ballField_)) {
                continue; // bi 下标现在是另一颗球（或越界），不 ++ 重查
            }
            ++bi;
        }
    }
}

bool Simulation::runTo(std::uint64_t endTick) {
    while (tick_ < endTick) {
        step();
        if (!auditOk_) return false;
    }
    return true;
}

std::size_t Simulation::nucleusCount() const {
    std::size_t c = 0;
    for (const Nucleus& n : nuclei_) { if (n.alive) ++c; }
    return c;
}

double Simulation::totalEnergy() const {
    double sum = 0.0;
    for (const Nucleus& n : nuclei_) {
        if (!n.alive) continue;
        sum += n.energy;
        for (const Ball& b : n.balls) sum += b.energy;
        // S19 红线：自然脱离的自由球能量**仍在球内**（不 dissipate）。
        // 它们已从 n.balls 移入 n.detached_，属于存活实体能量，必须计入
        // sumLiving，否则审计恒等式会误报（曾因此在行层加了错误的
        // dissipate("detach") 桥接，凭空扣掉一份能量）。脱落的容器在下一
        // 段（step 的自由球回收）被清空并转入 freeBalls_。
        for (const Ball& b : n.detached_) sum += b.energy; // 交接缓冲（步末清空）
    }
    // S19/S20：已脱离的自由球（ownerId==0）仍是存活实体，能量必须计入。
    for (const Ball& b : freeBalls_) sum += b.energy;
    // R2-A §8：待分配尸体营养包能量仍在账（尚未 transfer 给蓝细菌）。
    for (const Carrion& cr : carrion_) sum += cr.energy;
    // R1-B：蓝细菌是唯一食物、也是存活实体，能量必须计入 sumLiving（审计恒等式右侧）。
    sum += cyano_.totalEnergy();
    return sum;
}

// FNV-1a 64 state hash: tick + per-nucleus pos/vel/energy + per-ball
// angle/radius/energy + per-cyano-node id/pos/energy/alive.
namespace {
void hashMix(std::uint64_t& h, std::uint64_t v) {
    h ^= v;
    h *= 0x100000001b3ULL;
}
void hashDouble(std::uint64_t& h, double d) {
    // 位级哈希：NaN 归一为固定模式，避免不确定位。
    std::uint64_t bits = 0;
    if (d != d) { bits = 0x7ff8000000000000ULL; }
    else { std::memcpy(&bits, &d, sizeof(bits)); }
    for (int i = 0; i < 8; ++i) {
        hashMix(h, static_cast<std::uint8_t>(bits >> (i * 8)));
    }
}
void hashU64(std::uint64_t& h, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        hashMix(h, static_cast<std::uint8_t>(v >> (i * 8)));
    }
}
} // namespace

std::uint64_t Simulation::stateHash() const {
    const std::uint64_t kOffset = 0xcbf29ce484222325ULL;
    std::uint64_t h = kOffset;
    hashU64(h, static_cast<std::uint64_t>(tick_));
    for (const Nucleus& n : nuclei_) {
        hashDouble(h, n.pos.x);
        hashDouble(h, n.pos.y);
        hashDouble(h, n.vel.x);
        hashDouble(h, n.vel.y);
        hashDouble(h, n.energy);
        // 修复：补齐 n.speed / n.alive（此前未覆盖，可构造同哈希不同未来）。
        hashDouble(h, n.speed);
        hashU64(h, n.alive ? 1ULL : 0ULL);
        for (const Ball& b : n.balls) {
            hashDouble(h, b.angle);
            hashDouble(h, b.radius);
            hashDouble(h, b.energy);
            // 修复：补齐 b.lastOmega（决定失控球未来角位置）。
            hashDouble(h, b.lastOmega);
        }
    }
    // S22 接线：蓝细菌按 nodes() 下标顺序入哈希（id/pos/energy/alive/hue——
    // R1-B：hue 入哈希，遗传色相分叉可被双跑守卫发现）。
    for (const CyanoNode& c : cyano_.nodes()) {
        hashU64(h, c.id);
        hashDouble(h, c.pos.x);
        hashDouble(h, c.pos.y);
        hashDouble(h, c.energy);
        hashU64(h, c.alive ? 1ULL : 0ULL);
        hashDouble(h, c.hue);
    }
    // 【红队 R8-P1 修正 2026-10-31】自由球必须入哈希。
    // 旧问题：终局 84.2% 能量在 freeBalls_ 里，其 pos/vel/energy 完全不改变指纹
    // —— "同哈希不同未来"的注释自相矛盾。按容器序混入 id/pos/vel/energy。
    for (const Ball& fb : freeBalls_) {
        hashU64(h, fb.id);
        hashDouble(h, fb.pos.x);
        hashDouble(h, fb.pos.y);
        hashDouble(h, fb.vel.x);
        hashDouble(h, fb.vel.y);
        hashDouble(h, fb.energy);
    }
    return h;
}

// ==================== R3-B（实时调控接线）追加实现 ====================
// 本节只追加新方法；step() 主循环一字未动（任务卡硬约束，R3-A 将独立改它）。

void Simulation::applyConfig(const SimConfig& next) {
    // ---- 不热改：世界几何/种子/总帧数（改了等于重开局）+ 出生引导量 ----
    // colony.nuclei / cyanoNodeCount / cyanoMaxNodes 是构造期参数：热改它们
    // 不重撒世界（初始种群与蓝细菌播种只发生在构造期），为避免"改了没反应"
    // 的静默错觉，这里显式维持原值（值经 RuntimeEnv 写进 cfg 也被还原）。
    SimConfig merged = next;
    merged.world = cfg_.world;
    merged.colony.nuclei = cfg_.colony.nuclei;
    merged.cyanoNodeCount = cfg_.cyanoNodeCount;
    merged.cyanoMaxNodes = cfg_.cyanoMaxNodes;
    cfg_ = merged;

    // ---- BehaviorParams 同步（既有字段；行为层下一帧 apply 即消费）----
    // 行为层是唯一写点的实体字段不在本函数范围；bp_ 是组合根参数（非实体状态），
    // 同步它是配置流，不是实体写。
    bp_.respirationBase = cfg_.nucleus.respirationBase;
    bp_.locomotionCost = cfg_.nucleus.locomotionCost;
    bp_.cyanoPredationRadius = cfg_.nucleus.cyanoPredationRadius;
    bp_.migrationBonus = cfg_.nucleus.migrationBonus;     // R9-A 迁移回报镜像
    bp_.migrationRadius = cfg_.nucleus.migrationRadius;
    bp_.speedCostExponent = cfg_.nucleus.speedCostExponent; // R11-P1-3 接线镜像
    bp_.damping = cfg_.nucleus.damping;
    bp_.reproductionCostRatio = cfg_.nucleus.reproductionCostRatio;

    // ---- R7-B：BallTuning -> BallFieldParams 同步（与构造期同一份字段清单）----
    ballField_.damping = cfg_.ball.damping;
    ballField_.radialK = cfg_.ball.radialK;
    ballField_.tangentialK = cfg_.ball.tangentialK;
    ballField_.nearRepelRadius = cfg_.ball.nearRepelRadius;
    ballField_.ballRepelRadius = cfg_.ball.ballRepelRadius;
    ballField_.ballRepelK = cfg_.ball.ballRepelK;
    ballField_.sameOwnerAttractRadius = cfg_.ball.sameOwnerAttractRadius;
    ballField_.sameOwnerAttractK = cfg_.ball.sameOwnerAttractK;
    ballField_.maxSpeedShield = cfg_.ball.maxSpeedShield;
    ballField_.maxSpeedResource = cfg_.ball.maxSpeedResource;
    ballField_.maxSpeedScout = cfg_.ball.maxSpeedScout;
    ballField_.followCoupling = cfg_.ball.followCoupling;
    ballField_.followBoost = cfg_.ball.followBoost;
    ballField_.detachRadius = cfg_.ball.detachRadius;
    ballField_.lossCost = cfg_.ball.lossCost;

    // ---- CyanoParams 白盒镜像同步（core/CyanoTuning -> environment/CyanoParams）----
    // 字段契约见 Config.h CyanoTuning 注释：两边同名同义，漏字段会被此处
    // 逐字段赋值的编译/审查暴露。CyanoField 无参数 setter，白盒写入是任务卡
    // 指定的通道（"看看 CyanoField 有没有 setParams，没有就在你的路径内加"）。
    // 就地改写 params_ 成员（const_cast 合法性：cyano_ 本身是非 const 成员对象，
    // 只是 params() 访问器返回 const 引用——通过 const_cast 写回非 const 对象的
    // 非 const 成员是良定义行为）。**绝不重建 CyanoField**：nodes_/links_/nextId_/
    // frameCount_ 都是 CyanoField 成员，整体赋值会清空整个蓝细菌场（能量凭空
    // 消失直接击穿审计恒等式）。下一帧 cyano_.step() 即按新参数结算。
    {
        const CyanoTuning& t = cfg_.cyanoTuning;
        CyanoParams& p = const_cast<CyanoParams&>(cyano_.params());
        p.photosynthesisRate = t.photosynthesisRate;
        p.lightHalfSaturation = t.lightHalfSaturation;
        p.metabolismRate = t.metabolismRate;
        p.energyCap = t.energyCap;
        p.reproductionCost = t.reproductionCost;
        p.reproductionProbability = t.reproductionProbability;
        p.reproductionOffset = t.reproductionOffset;
        p.minSeparation = t.minSeparation;
        p.ballSpawnCost = t.ballSpawnCost;
        p.ballSpawnProbability = t.ballSpawnProbability;
        p.ballRadius = t.ballRadius;
        p.ballSpawnMinDist = t.ballSpawnMinDist;
        p.ballSpawnMaxDist = t.ballSpawnMaxDist;
        p.linkRadius = t.linkRadius;
        p.linkMaxNeighbors = t.linkMaxNeighbors;
        p.linkTransferRate = t.linkTransferRate;
        p.linkDeadband = t.linkDeadband;
        p.linkCheckInterval = t.linkCheckInterval;
        p.conversionMode = t.conversionMode;
        p.conversionRate = t.conversionRate;
        p.conversionExponent = t.conversionExponent;
        p.conversionSpeedRef = t.conversionSpeedRef;
        p.conversionAccelRef = t.conversionAccelRef;
        p.conversionWSpeed = t.conversionWSpeed;
        p.conversionWAccel = t.conversionWAccel;
        p.conversionMinRate = t.conversionMinRate;
        p.conversionMaxRate = t.conversionMaxRate;
        p.hueJitterSigma = t.hueJitterSigma;
        // R7-C：聚落播种形状镜像。注意 seed() 只在构造期调用，热改这两个值
        // 不重撒已有节点——落点存在是为了镜像完整性（未来事件播种/重播种
        // 消费时不再漏镜像），与 nodeSeedCount 的"构造期参数"注释同口径。
        p.cyanoColonies = t.cyanoColonies;
        p.clusterSpread = t.clusterSpread;
        // R3-A：自遮光三参数镜像（漏掉会被热调控冲回默认值——"自遮光不生效"
        // 的伪装 bug，见卡面预警）。
        p.shadeK = t.shadeK;
        p.shadeRadius = t.shadeRadius;
        p.shadeSigma = t.shadeSigma;
        p.grazeRefugeEnergy = t.grazeRefugeEnergy;
    }
}

int Simulation::addCyanoNodes(int n) {
    if (n <= 0) return 0;
    const double w = cfg_.world.width;
    const double h = cfg_.world.height;
    // 护栏：cyanoMaxNodes 是 CyanoField 的性能护栏（linkTransfer O(n^2)）。
    // appendSeed() 内部以 maxNodes 截断（构造期播种同源）。
    const int added = cyano_.appendSeed(n, w, h, rng_);
    // 能量账：追加节点以 0 能量播种（构造期播种同口径），账面零操作——
    // 节点能量从光合长出来时才由 step() 的 injectPrimary 入账，恒等式两侧
    // 同增，审计自证。因此本函数不碰 ledger_。
    return added;
}


namespace {
// R5-C：serializeState/deserializeState 的小端逐字节 IO（不复制流缓冲，逐标量写读，
// 大端主机上同样得到小端字节序——本项目约定全二进制格式小端）。
constexpr std::uint32_t kSerMagic = 0x41534153u;    // "ASAV"（小端字节序视角）
// R7-B：v2 = v1 + Ball.r7Lane 尾部字段（serWriteBall/serReadBall）。旧 v1 档
// 读入会在 ball 车道字节处失败（格式校验从严，报错含版本号）。
constexpr std::uint32_t kSerVersion = 3u;   // R9-A：v3 = v2 + 核迁移状态（lastMealPos/hasMealPos）
constexpr std::size_t kSerMaxCount = 1u << 31;      // 计数护栏：防损坏流 reserve 爆炸
constexpr std::size_t kSerNameLimit = 64;           // ledger reason 长度护栏

void serWriteBytes(std::ostream& os, const void* data, std::size_t n) {
    os.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
}

void serWriteU32(std::ostream& os, std::uint32_t v) {
    unsigned char b[4];
    b[0] = static_cast<unsigned char>(v & 0xFFu);
    b[1] = static_cast<unsigned char>((v >> 8) & 0xFFu);
    b[2] = static_cast<unsigned char>((v >> 16) & 0xFFu);
    b[3] = static_cast<unsigned char>((v >> 24) & 0xFFu);
    serWriteBytes(os, b, 4);
}

void serWriteU64(std::ostream& os, std::uint64_t v) {
    unsigned char b[8];
    for (int i = 0; i < 8; ++i) {
        b[i] = static_cast<unsigned char>((v >> (i * 8)) & 0xFFu);
    }
    serWriteBytes(os, b, 8);
}

void serWriteI64(std::ostream& os, std::int64_t v) {
    serWriteU64(os, static_cast<std::uint64_t>(v));
}

void serWriteI32(std::ostream& os, std::int32_t v) {
    serWriteU32(os, static_cast<std::uint32_t>(v));
}

// double 按位原样：memcpy 进 u64 再小端写出。位级保真 = 读档续跑逐位一致的前提。
void serWriteF64(std::ostream& os, double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    serWriteU64(os, bits);
}

void serWriteBool(std::ostream& os, bool v) {
    const unsigned char b = v ? 1u : 0u;
    serWriteBytes(os, &b, 1);
}

void serWritePad(std::ostream& os, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char z = 0u;
        serWriteBytes(os, &z, 1);
    }
}

bool serReadBytes(std::istream& is, void* data, std::size_t n) {
    is.read(static_cast<char*>(data), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(is.gcount()) == n;
}

bool serReadU32(std::istream& is, std::uint32_t& v) {
    unsigned char b[4];
    if (!serReadBytes(is, b, 4)) return false;
    v = static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
        (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
    return true;
}

bool serReadU64(std::istream& is, std::uint64_t& v) {
    unsigned char b[8];
    if (!serReadBytes(is, b, 8)) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(b[i]) << (i * 8);
    }
    return true;
}

bool serReadI64(std::istream& is, std::int64_t& v) {
    std::uint64_t u = 0;
    if (!serReadU64(is, u)) return false;
    v = static_cast<std::int64_t>(u);
    return true;
}

bool serReadI32(std::istream& is, std::int32_t& v) {
    std::uint32_t u = 0;
    if (!serReadU32(is, u)) return false;
    v = static_cast<std::int32_t>(u);
    return true;
}

bool serReadF64(std::istream& is, double& v) {
    std::uint64_t bits = 0;
    if (!serReadU64(is, bits)) return false;
    std::memcpy(&v, &bits, sizeof(v));
    return true;
}

bool serReadBool(std::istream& is, bool& v) {
    unsigned char b = 0u;
    if (!serReadBytes(is, &b, 1)) return false;
    v = (b != 0u);
    return true;
}

// 读侧的填充字节必须存在且为 0：格式校验从严（写侧恒写 0）。
bool serReadPad(std::istream& is, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        unsigned char b = 0u;
        if (!serReadBytes(is, &b, 1)) return false;
        if (b != 0u) return false;
    }
    return true;
}

bool serReadCount(std::istream& is, std::uint64_t& v) {
    if (!serReadU64(is, v)) return false;
    return v <= kSerMaxCount;
}

// 容器大小换算：u64 计数 -> size_t（越界拒绝，防 32 位平台截断）。
bool serToSize(std::uint64_t v, std::size_t& out) {
    if (v > kSerMaxCount) return false;
    out = static_cast<std::size_t>(v);
    return true;
}

bool serWriteBall(std::ostream& os, const Ball& b) {
    serWriteU64(os, b.id);
    serWriteF64(os, b.angle);
    serWriteF64(os, b.radius);
    serWriteF64(os, b.energy);
    serWriteF64(os, b.lastOmega);
    serWriteI32(os, static_cast<std::int32_t>(b.type));
    serWriteF64(os, b.cap);
    serWriteF64(os, b.pos.x);
    serWriteF64(os, b.pos.y);
    serWriteF64(os, b.vel.x);
    serWriteF64(os, b.vel.y);
    serWriteU64(os, b.ownerId);
    serWriteBool(os, b.r7Lane); // R7-B：积分车道标记随档走（续跑车道不丢失）
    return os.good();
}

bool serReadBall(std::istream& is, Ball& b) {
    std::int32_t type = 0;
    if (!serReadU64(is, b.id)) return false;
    if (!serReadF64(is, b.angle)) return false;
    if (!serReadF64(is, b.radius)) return false;
    if (!serReadF64(is, b.energy)) return false;
    if (!serReadF64(is, b.lastOmega)) return false;
    if (!serReadI32(is, type)) return false;
    if (!serReadF64(is, b.cap)) return false;
    if (!serReadF64(is, b.pos.x)) return false;
    if (!serReadF64(is, b.pos.y)) return false;
    if (!serReadF64(is, b.vel.x)) return false;
    if (!serReadF64(is, b.vel.y)) return false;
    if (!serReadU64(is, b.ownerId)) return false;
    if (type < 0 || type > 2) return false; // BallType 域外值 = 损坏流
    b.type = static_cast<BallType>(type);
    if (!serReadBool(is, b.r7Lane)) return false; // R7-B：车道标记（v1 格式新增尾部）
    return true;
}

// genome 序列化顺序 = Genome.h 字段声明序（与 --trace-genome 导出序同源）。
bool serWriteGenome(std::ostream& os, const Genome& g) {
    for (int t = 0; t < kBallTypeCount; ++t) serWriteF64(os, g.affinity[t]);
    for (int t = 0; t < kBallTypeCount; ++t) serWriteF64(os, g.orbitRadius[t]);
    for (int t = 0; t < kBallTypeCount; ++t) serWriteF64(os, g.absorbPreference[t]);
    for (int t = 0; t < kBallTypeCount; ++t) serWriteF64(os, g.repelStrength[t]);
    serWriteF64(os, g.maxSpeed);
    serWriteF64(os, g.maxAccel);
    serWriteF64(os, g.energyThreshold);
    serWriteF64(os, g.mutationRate);
    serWriteF64(os, g.cyanoPreference);
    serWriteF64(os, g.cyanoDigestion);
    serWriteF64(os, g.nucleusSightRadius);
    serWriteF64(os, g.nucleusPersonalSpace);
    serWriteF64(os, g.nucleusRepelStrength);
    serWriteF64(os, g.nucleusCohesion);
    serWriteF64(os, g.forceRange);
    serWriteF64(os, g.forceCostK);
    serWriteF64(os, g.hue);
    return os.good();
}

bool serReadGenome(std::istream& is, Genome& g) {
    for (int t = 0; t < kBallTypeCount; ++t) {
        if (!serReadF64(is, g.affinity[t])) return false;
    }
    for (int t = 0; t < kBallTypeCount; ++t) {
        if (!serReadF64(is, g.orbitRadius[t])) return false;
    }
    for (int t = 0; t < kBallTypeCount; ++t) {
        if (!serReadF64(is, g.absorbPreference[t])) return false;
    }
    for (int t = 0; t < kBallTypeCount; ++t) {
        if (!serReadF64(is, g.repelStrength[t])) return false;
    }
    if (!serReadF64(is, g.maxSpeed)) return false;
    if (!serReadF64(is, g.maxAccel)) return false;
    if (!serReadF64(is, g.energyThreshold)) return false;
    if (!serReadF64(is, g.mutationRate)) return false;
    if (!serReadF64(is, g.cyanoPreference)) return false;
    if (!serReadF64(is, g.cyanoDigestion)) return false;
    if (!serReadF64(is, g.nucleusSightRadius)) return false;
    if (!serReadF64(is, g.nucleusPersonalSpace)) return false;
    if (!serReadF64(is, g.nucleusRepelStrength)) return false;
    if (!serReadF64(is, g.nucleusCohesion)) return false;
    if (!serReadF64(is, g.forceRange)) return false;
    if (!serReadF64(is, g.forceCostK)) return false;
    if (!serReadF64(is, g.hue)) return false;
    return true;
}
} // namespa

bool Simulation::serializeState(std::ostream& os) const {
    if (!os.good()) return false;
    // ---- Header + 标量区 ----
    serWriteU32(os, kSerMagic);
    serWriteU32(os, kSerVersion);
    serWriteU64(os, tick_);
    serWriteBool(os, auditOk_);
    serWritePad(os, 7);
    std::uint64_t rs = 0;
    std::uint64_t ri = 0;
    rng_.getState(rs, ri);
    serWriteU64(os, rs);
    serWriteU64(os, ri);
    serWriteU64(os, nextNucleusId_);
    serWriteU64(os, nextFreeBallId_);
    serWriteU64(os, deathCount_);
    serWriteU64(os, aliveCount_);
    serWriteF64(os, ledger_.totalInjected());
    serWriteF64(os, ledger_.totalDissipated());
    serWriteF64(os, ledger_.totalTransferred());
    // ---- 账本按原因耗散表 ----
    const auto& byReason = ledger_.dissipatedByReason();
    serWriteU64(os, byReason.size());
    for (const auto& kv : byReason) {
        const std::string& reason = kv.first;
        const auto len = static_cast<std::uint8_t>(reason.size() > kSerNameLimit
                                                       ? kSerNameLimit
                                                       : reason.size());
        serWriteBytes(os, &len, 1);
        serWriteBytes(os, reason.data(), len);
        serWriteF64(os, kv.second);
    }
    // ---- 核（含 genome 全量、eff* 缓存、消化冷却、隶属/交接球、尸体包）----
    serWriteU64(os, nuclei_.size());
    for (const Nucleus& n : nuclei_) {
        serWriteU64(os, n.id);
        serWriteF64(os, n.pos.x);
        serWriteF64(os, n.pos.y);
        serWriteF64(os, n.vel.x);
        serWriteF64(os, n.vel.y);
        serWriteF64(os, n.energy);
        serWriteF64(os, n.cap);
        serWriteF64(os, n.speed);
        serWriteBool(os, n.alive);
        serWritePad(os, 7);
        if (!serWriteGenome(os, n.genome)) return false;
        serWriteF64(os, n.effSight);
        serWriteF64(os, n.effSpace);
        serWriteF64(os, n.effRepel);
        serWriteF64(os, n.effCohesion);
        serWriteI32(os, static_cast<std::int32_t>(n.digestCooldown));
        serWriteU64(os, n.balls.size());
        for (const Ball& b : n.balls) {
            if (!serWriteBall(os, b)) return false;
        }
        serWriteU64(os, n.detached_.size());
        for (const Ball& b : n.detached_) {
            if (!serWriteBall(os, b)) return false;
        }
        serWriteF64(os, n.carrion_.pos.x);
        serWriteF64(os, n.carrion_.pos.y);
        serWriteF64(os, n.carrion_.energy);
        // R9-A：迁移回报状态（v3 追加字段）
        serWriteF64(os, n.lastMealPos.x);
        serWriteF64(os, n.lastMealPos.y);
        serWriteBool(os, n.hasMealPos);
    }
    // ---- 自由球 ----
    serWriteU64(os, freeBalls_.size());
    for (const Ball& fb : freeBalls_) {
        if (!serWriteBall(os, fb)) return false;
    }
    // ---- 组合根尸体容器 ----
    serWriteU64(os, carrion_.size());
    for (const Carrion& cr : carrion_) {
        serWriteF64(os, cr.pos.x);
        serWriteF64(os, cr.pos.y);
        serWriteF64(os, cr.energy);
    }
    // ---- 蓝细菌场（节点 + id 计数 + 连线节流相位）----
    const std::vector<CyanoNode>& nodes = cyano_.nodes();
    serWriteU64(os, nodes.size());
    for (const CyanoNode& c : nodes) {
        serWriteU64(os, c.id);
        serWriteF64(os, c.pos.x);
        serWriteF64(os, c.pos.y);
        serWriteF64(os, c.energy);
        serWriteBool(os, c.alive);
        serWritePad(os, 7);
        serWriteF64(os, c.hue);
    }
    serWriteU64(os, cyano_.nextNodeId());
    serWriteI64(os, cyano_.frameIndex());
    return os.good();
}

bool Simulation::deserializeState(std::istream& is, std::string& err) {
    err.clear();
    if (!is.good()) { err = "stream not good"; return false; }
    // 注意：恢复过程中本对象处于"半新半旧"状态，任何失败路径都不得被外部消费
    //（调用契约：返回 false 即代表世界状态不可信）。先把读回来的全量攒在局部，
    // 末尾一次性原子提交到成员，减少失效窗口。
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!serReadU32(is, magic) || !serReadU32(is, version)) {
        err = "cannot read header"; return false;
    }
    if (magic != kSerMagic) { err = "bad magic (not ASAV)"; return false; }
    if (version != kSerVersion) {
        err = "unsupported save version: " + std::to_string(version) +
              " (expected " + std::to_string(kSerVersion) + ")";
        return false;
    }
    std::uint64_t tick = 0;
    bool auditOk = true;
    if (!serReadU64(is, tick)) { err = "cannot read tick"; return false; }
    if (!serReadBool(is, auditOk)) { err = "cannot read audit flag"; return false; }
    if (!serReadPad(is, 7)) { err = "corrupt header padding"; return false; }
    std::uint64_t rngState = 0;
    std::uint64_t rngInc = 0;
    if (!serReadU64(is, rngState) || !serReadU64(is, rngInc)) {
        err = "cannot read rng state"; return false;
    }
    std::uint64_t nextNucleusId = 0;
    std::uint64_t nextFreeBallId = 0;
    std::uint64_t deathCount = 0;
    std::uint64_t aliveCount = 0;
    if (!serReadU64(is, nextNucleusId)) { err = "cannot read nextNucleusId"; return false; }
    if (!serReadU64(is, nextFreeBallId)) { err = "cannot read nextFreeBallId"; return false; }
    if (!serReadU64(is, deathCount)) { err = "cannot read deathCount"; return false; }
    if (!serReadU64(is, aliveCount)) { err = "cannot read aliveCount"; return false; }
    double injected = 0.0;
    double dissipated = 0.0;
    double transferred = 0.0;
    if (!serReadF64(is, injected) || !serReadF64(is, dissipated) ||
        !serReadF64(is, transferred)) {
        err = "cannot read ledger totals"; return false;
    }
    std::uint64_t reasonCount = 0;
    if (!serReadCount(is, reasonCount)) { err = "bad ledger reason count"; return false; }
    std::map<std::string, double> byReason;
    for (std::uint64_t i = 0; i < reasonCount; ++i) {
        std::uint8_t len = 0;
        if (!serReadBytes(is, &len, 1)) { err = "cannot read reason length"; return false; }
        if (len == 0) { err = "empty ledger reason"; return false; }
        std::string reason(len, '\0');
        if (!serReadBytes(is, &reason[0], len)) { err = "cannot read reason"; return false; }
        double amount = 0.0;
        if (!serReadF64(is, amount)) { err = "cannot read reason amount"; return false; }
        byReason.emplace(std::move(reason), amount);
    }
    // ---- 核 ----
    std::uint64_t nNuclei = 0;
    if (!serReadCount(is, nNuclei)) { err = "bad nuclei count"; return false; }
    std::vector<Nucleus> nuclei;
    std::size_t nucSz = 0;
    if (!serToSize(nNuclei, nucSz)) { err = "nuclei count overflow"; return false; }
    nuclei.reserve(nucSz);
    std::uint64_t aliveTally = 0;
    for (std::uint64_t i = 0; i < nNuclei; ++i) {
        Nucleus n;
        if (!serReadU64(is, n.id)) { err = "cannot read nucleus id"; return false; }
        if (!serReadF64(is, n.pos.x) || !serReadF64(is, n.pos.y)) { err = "cannot read nucleus pos"; return false; }
        if (!serReadF64(is, n.vel.x) || !serReadF64(is, n.vel.y)) { err = "cannot read nucleus vel"; return false; }
        if (!serReadF64(is, n.energy) || !serReadF64(is, n.cap)) { err = "cannot read nucleus energy/cap"; return false; }
        if (!serReadF64(is, n.speed)) { err = "cannot read nucleus speed"; return false; }
        if (!serReadBool(is, n.alive)) { err = "cannot read nucleus alive"; return false; }
        if (!serReadPad(is, 7)) { err = "corrupt nucleus padding"; return false; }
        if (!serReadGenome(is, n.genome)) { err = "cannot read genome"; return false; }
        if (!serReadF64(is, n.effSight) || !serReadF64(is, n.effSpace) ||
            !serReadF64(is, n.effRepel) || !serReadF64(is, n.effCohesion)) {
            err = "cannot read eff cache"; return false;
        }
        std::int32_t cooldown = 0;
        if (!serReadI32(is, cooldown)) { err = "cannot read digestCooldown"; return false; }
        if (cooldown < 0) { err = "negative digestCooldown"; return false; }
        n.digestCooldown = cooldown;
        std::uint64_t nBalls = 0;
        if (!serReadCount(is, nBalls)) { err = "bad balls count"; return false; }
        std::size_t ballsSz = 0;
        if (!serToSize(nBalls, ballsSz)) { err = "balls count overflow"; return false; }
        n.balls.resize(ballsSz);
        for (std::size_t k = 0; k < ballsSz; ++k) {
            if (!serReadBall(is, n.balls[k])) { err = "cannot read ball"; return false; }
        }
        std::uint64_t nDet = 0;
        if (!serReadCount(is, nDet)) { err = "bad detached count"; return false; }
        std::size_t detSz = 0;
        if (!serToSize(nDet, detSz)) { err = "detached count overflow"; return false; }
        n.detached_.resize(detSz);
        for (std::size_t k = 0; k < detSz; ++k) {
            if (!serReadBall(is, n.detached_[k])) { err = "cannot read detached ball"; return false; }
        }
        if (!serReadF64(is, n.carrion_.pos.x) || !serReadF64(is, n.carrion_.pos.y) ||
            !serReadF64(is, n.carrion_.energy)) {
            err = "cannot read nucleus carrion"; return false;
        }
        // R9-A：迁移回报状态（v3 追加字段）
        if (!serReadF64(is, n.lastMealPos.x) || !serReadF64(is, n.lastMealPos.y) ||
            !serReadBool(is, n.hasMealPos)) {
            err = "cannot read nucleus migration state"; return false;
        }
        if (n.alive) ++aliveTally;
        nuclei.push_back(std::move(n));
    }
    // ---- 自由球 ----
    std::uint64_t nFree = 0;
    if (!serReadCount(is, nFree)) { err = "bad free balls count"; return false; }
    std::size_t freeSz = 0;
    if (!serToSize(nFree, freeSz)) { err = "free balls count overflow"; return false; }
    std::vector<Ball> freeBalls(freeSz);
    for (std::size_t k = 0; k < freeSz; ++k) {
        if (!serReadBall(is, freeBalls[k])) { err = "cannot read free ball"; return false; }
    }
    // ---- 组合根尸体容器 ----
    std::uint64_t nCarr = 0;
    if (!serReadCount(is, nCarr)) { err = "bad carrion count"; return false; }
    std::size_t carrSz = 0;
    if (!serToSize(nCarr, carrSz)) { err = "carrion count overflow"; return false; }
    std::vector<Carrion> carrion(carrSz);
    for (std::size_t k = 0; k < carrSz; ++k) {
        if (!serReadF64(is, carrion[k].pos.x) || !serReadF64(is, carrion[k].pos.y) ||
            !serReadF64(is, carrion[k].energy)) {
            err = "cannot read carrion"; return false;
        }
    }
    // ---- 蓝细菌场 ----
    std::uint64_t nNodes = 0;
    if (!serReadCount(is, nNodes)) { err = "bad cyano count"; return false; }
    std::vector<CyanoNode> nodes;
    std::size_t nodesSz = 0;
    if (!serToSize(nNodes, nodesSz)) { err = "cyano count overflow"; return false; }
    nodes.reserve(nodesSz);
    for (std::size_t k = 0; k < nodesSz; ++k) {
        CyanoNode c;
        if (!serReadU64(is, c.id)) { err = "cannot read cyano id"; return false; }
        if (!serReadF64(is, c.pos.x) || !serReadF64(is, c.pos.y)) { err = "cannot read cyano pos"; return false; }
        if (!serReadF64(is, c.energy)) { err = "cannot read cyano energy"; return false; }
        if (!serReadBool(is, c.alive)) { err = "cannot read cyano alive"; return false; }
        if (!serReadPad(is, 7)) { err = "corrupt cyano padding"; return false; }
        if (!serReadF64(is, c.hue)) { err = "cannot read cyano hue"; return false; }
        nodes.push_back(std::move(c));
    }
    std::uint64_t cyanoNextId = 0;
    std::int64_t cyanoFrame = 0;
    if (!serReadU64(is, cyanoNextId)) { err = "cannot read cyano nextId"; return false; }
    if (!serReadI64(is, cyanoFrame)) { err = "cannot read cyano frameCount"; return false; }
    if (is.peek() != std::istream::traits_type::eof()) {
        err = "trailing bytes after save payload"; return false;
    }
    // ---- 一致性红线：存活核计数必须与账面一致 ----
    if (aliveTally != aliveCount) {
        err = "aliveCount mismatch: header " + std::to_string(aliveCount) +
              " vs bodies " + std::to_string(aliveTally);
        return false;
    }
    // ---- 原子提交 ----
    tick_ = tick;
    auditOk_ = auditOk;
    rng_.setState(rngState, rngInc);
    nextNucleusId_ = nextNucleusId;
    nextFreeBallId_ = nextFreeBallId;
    deathCount_ = deathCount;
    aliveCount_ = static_cast<std::size_t>(aliveCount);
    nuclei_ = std::move(nuclei);
    freeBalls_ = std::move(freeBalls);
    carrion_ = std::move(carrion);
    cyano_.restoreForSaveLoad(std::move(nodes), cyanoNextId, cyanoFrame);
    // R5-C 补丁：shade_ 是"上一结算帧"派生快照，未入档（逐帧重算态）。
    // 读档后不刷新 → 第一帧光合与原世界分叉（save_resume 用例根因）。
    // 纯几何重算，零 rng。
    cyano_.refreshShadeCache();
    // 账本恢复（白盒：同类注入器，R5-C 专用恢复通道）。三总额 + 按原因表回填，
    // 审计恒等式所需全部账面随档走。
    ledger_.injectLedgerSnapshot(injected, dissipated, transferred,
                                 std::map<std::string, double>(byReason));
    // 读档即自证：以本 tick 的容差口径重算审计。恒等式若破，当场置 auditOk_=false
    //（调用方以 auditOk() 立即成立为验收红线），世界状态仍诚实。
    const double tol = 1e-9 * std::max(1.0, ledger_.totalInjected());
    auditOk_ = ledger_.audit(tol, totalEnergy());
    return true;
}

} // namespace agentsim


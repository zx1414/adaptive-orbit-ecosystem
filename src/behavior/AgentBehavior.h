// agentsim - behavior/AgentBehavior.h
// 行为层：唯一可写点。执行移动、球控制（球远失控）、能量结算与死亡（R1-B：吃食物
// 分支已删除——核的唯一食物是蓝细菌，捕食记账接口只保留 absorbPrey），
// 并实现 T08 三动作：产球 deploy / 回收 recycle / 繁殖 reproduce（全部走 ledger，
// 账目 reason 枚举：locomotion / respiration / deploy / recycle / recycle-loss / reproduce-loss / ballDrift / death / capOverflow / detach；S10 红线在 reproduce 内
// 强制；C1：繁殖是零和转移，不做一级注入、不记 "reproduce" 耗散）。
// 红线：除本层外任何模块不得直接改写实体状态；一切能量变化必须经过 AgentLedger。
//
// ---- R2-A（基因组接线）：基因是行为参数的唯一权威 ----
// 24 参数 Genome 挂在 Nucleus 上，行为层逐项消费（无死基因，见各函数注释）：
//   移动限速 = genome.maxSpeed；施力半径 = genome.forceRange；施力代价 = genome.forceCostK；
//   轨道半径 = genome.orbitRadius[t]；径向弹簧 = genome.affinity[t]；
//   近距排斥 = genome.repelStrength[t]；回收倍率 = genome.absorbPreference[t]；
//   繁殖门槛 = genome.energyThreshold；繁殖变异 = genomeMutate(coeff=cfg.mutationCoefficient)。
// BehaviorParams 里的 maxSpeed/forceRadius/forceMax/forceCostK/cyanoDigestion 退役为
// 兼容字段：仅当 genome 值非法（NaN/<=0）时作 fallback，正常路径不消费。
// 对核社会参数（nucleus*）与点名施力（forceSlot/mod[4]）的生效路径见
// socialAccelerations / applyNamedForce；每帧生效值缓存在 Nucleus.eff* 供调试/trace。
// 捕食消化冷却 Nucleus.digestCooldown 由组合根逐帧递减（行为层出生时清零）。
#pragma once
#include <vector>

#include "../core/Genome.h"
#include "../core/PCG32.h"
#include "../core/Types.h"
#include "../core/Vec2.h"
#include "../environment/AgentEnvironment.h"
#include "../ledger/AgentLedger.h"

// 决策层由并行工作流提供（签名约定见 SPEC）：只需 ActionVector 参与行为执行。
#include "decision/IDecisionPolicy.h"

namespace agentsim {

// 环境层地形只读接口（T43）：apply 末位可选参数；nullptr = 无地形（默认，行为不变）。
class Terrain;

// 账本约定：环境生产者池实体 id=0。R1-B（蓝细菌取代食物）后 Food 实体已删除，
// 该池完全归于蓝细菌：蓝细菌能量（初始/光合）以它为主体一级注入账本；捕食是
// 蓝细菌节点→核 的零和 transfer（流量记账，不改注入/耗散总额）。名字保留
// kFoodPoolId 是为旧行为层引用（Simulation 组合根）不破编译——语义见 Simulation.cpp。
constexpr EntityId kFoodPoolId = 0;

// 球的类型（S18）：Resource 贡献菌落容量；Scout 只影响感知（S17，别处实现）；
// Shield 初版只占类型位，无行为差异。
enum class BallType { Resource = 0, Scout = 1, Shield = 2 };

// 环绕小球：围绕核做轨道运动，dist(球,核) 即轨道半径 radius。
struct Ball {
    EntityId id = 0;
    double angle = 0.0;   // 相对核的轨道角（弧度，[0,2pi)）
    double radius = 10.0; // 轨道半径（=球心到核的最小环距）
    double energy = 0.0;  // 球储能（改它必须走 ledger，守恒红线）
    double lastOmega = 0.0; // 上一帧实际角速度（失控漂移时唯一保留的状态）
    BallType type = BallType::Resource; // 球类型：仅 Resource 贡献菌落容量（S18）
    double cap = 60.0;  // 单球仓库容量（仅 Resource 球计入 colonyCap；Scout/Shield 不计）
    // ---- S19：球的世界状态（自由球语义地基，追加在结构末尾，不动既有字段）----
    Vec2 pos{};            // 世界坐标（wrap 后；隶属球由 apply 每帧按轨道极坐标重算）
    Vec2 vel{};            // 世界速度（自由球漂移用；隶属球保持 {0,0}）
    EntityId ownerId = 0;  // 宿主核 id；0 = 自由球
    // ---- R7-B：积分车道标记（结构体只加字段，本卡红线允许）----
    // false（默认）= 旧行为层轨道模型（apply 内极坐标推进，S18/S19/S20 语义，
    // 全部既有单测钉死）；true = R7-B 世界积分车道（组合根 ballWorldStep 权威
    // 推进 pos/vel，v2.3.1 力学）。部署/蓝细菌产球默认 false（回归逐位安全）；
    // R7-B 归属结算的换主/失主/收编把球置 true（见 transferBall/reassignBall）。
    // 序列化随 Ball 全量走（Simulation.cpp serWriteBall/serReadBall，存档续跑
    // 车道不丢失）。
    bool r7Lane = false;
};
// ---- R7-B：球力场参数集（v2.3.1 World 力学参数的项目侧落点）----
// 默认值 = v2.3.1 env_config 标定（行为层独立可测）；组合根用 cfg.ball
//（BallTuning，R6-C 已建）逐字段覆盖（构造期 + applyConfig，见 Simulation.cpp）。
// bounce_restitution / wrap_boundary 不在此：本项目世界是环面，边界恒 wrap
//（任务卡指定 wrap 优先，Config 字段仅存档语义）。
struct BallFieldParams {
    double damping = 0.90;               // ball_damping（v231 0.90）
    double radialK = 0.03;               // radial_k（v231 0.03）
    double tangentialK = 2.0;            // tangential_k（v231 2.0）
    double nearRepelRadius = 25.0;       // near_repel_radius（v231 25）
    double ballRepelRadius = 10.0;       // ball_ball_repel_radius（v231 10）
    double ballRepelK = 1.2;             // ball_ball_repel_k（v231 1.2）
    double sameOwnerAttractRadius = 40.0;// ball_same_owner_attract_radius（v231 40）
    double sameOwnerAttractK = 0.15;     // ball_same_owner_attract_k（v231 0.15）
    double maxSpeedShield = 25.0;        // ball_max_speed_shield（v231 25）
    double maxSpeedResource = 50.0;      // ball_max_speed_worker（v231 50）
    double maxSpeedScout = 80.0;         // ball_max_speed_scout（v231 80）
    double followCoupling = 15.0;        // follow_coupling（v231 15）
    double followBoost = 1.5;            // follow_boost（v231 1.5）
    double detachRadius = 400.0;         // detach_radius（v231 400）
    double lossCost = 5.0;               // ball_loss_cost（v231 5.0）
};

// ---- R2-A §8：尸体营养包（营养循环的地基）----
// 核死亡时能量分两路：70% 当场 dissipate("death")（热力学损失），
// 30% 装进 Carrion（位置 + 能量）暂存，由组合根分配给最近的蓝细菌节点
//（分解者：尸体 → 植物）。能量始终在账（Carrion 计入 sumLiving 的责任在组合根
// 的 totalEnergy），分配走 ledger.transfer（零和搬运）。
struct Carrion {
    Vec2 pos{};        // 死亡位置（环面坐标）
    double energy = 0.0; // 待分配能量（30% 遗产）
};

// 核：行为层所有的实体状态。
struct Nucleus {
    EntityId id = 0;
    Vec2 pos{};
    Vec2 vel{};
    double energy = 0.0;
    double cap = 0.0;     // 当前菌落容量（S18 起 apply 末按 colonyCap 结算并硬截断）
    std::vector<Ball> balls;
    double speed = 0.0;   // 上一帧实际速度（双重干扰的强度因子）
    bool alive = true;
    // S19：自然脱离交接缓冲——apply 把待脱离球移入（能量原样保留，ownerId 置 0），
    // 组合根每帧取走并清空；未取走期间能量仍记在这些球实体上。
    std::vector<Ball> detached_;
    // ---- R2-A：可遗传基因组（24 参数 + hue）。----
    // 唯一写点 = 行为层（spawnNuclei 出生引导的 genomeRandomize 也经组合根调用、
    // 落在本结构上；变异只发生在 reproduce 内）。基因不是运行时可变状态：
    // 决策层的 mod[4] 只调制本帧生效值（Nucleus.eff*），绝不回写 genome。
    Genome genome{};
    // R2-A：本帧生效社会参数（基因 × 决策调制 eff = base*(1+0.4*mod)），每帧由
    // socialAccelerations 入口重算；0 = 本帧未算（无核物理或核已死）。供调试/trace。
    double effSight = 0.0;    // genome.nucleusSightRadius 的本帧生效值
    double effSpace = 0.0;    // genome.nucleusPersonalSpace 的本帧生效值
    double effRepel = 0.0;    // genome.nucleusRepelStrength 的本帧生效值
    double effCohesion = 0.0; // genome.nucleusCohesion 的本帧生效值
    // R2-A：捕食消化冷却（帧）。>0 期间本核不捕食；每帧由组合根递减
    //（digestCooldown = ceil(1/(0.5+cyanoDigestion))，cyanoDigestion 越高冷却越短）。
    // 子代出生时清零。冷却值本身不耗能——它是"处理时间"约束，不是能量账。
    int digestCooldown = 0;
    // R2-A §8：死亡营养包暂存。核死亡瞬间由 apply 填充（30% 核体能量 + 死亡位置），
    // 组合根每帧取走并清空（分配给最近的蓝细菌节点，走 ledger.transfer 零和搬运）。
    // 未取走期间能量记在此处，totalEnergy 口径必须把它计入（sumLiving 红线）。
    Carrion carrion_{};
    // R9-A：上次成功捕食的位置（世界坐标）。用于迁移回报的距离判定：
    // 本口猎物距上口 > migrationRadius 时转化率获补偿。默认 = 出生位置；
    // 未捕食过时与 pos 相同 → 距离 0 < 半径 → 无补偿（保守起点）。
    Vec2 lastMealPos{};
    bool hasMealPos = false;
};


// 行为参数（初版占位默认值，可由上层覆盖）。
// 注：T08 三动作的参数（产球/回收/繁殖阈值与账目比例）按任务约定不改 Config.h，
// 全部以常量 + 注释形式收敛在 AgentBehavior.cpp 匿名命名空间中。
struct BehaviorParams {
    double moveK = 60.0;            // 方向→速度增益（R2-A：兼作转化率公式的加速度输入）
    // R2-A：maxSpeed 已由 genome.maxSpeed 接管（每核独立，Init 30~100）。
    // 本字段退役为 fallback：仅当某核 genome.maxSpeed 非法（NaN/<=0）时按旧值兜底，
    // 正常路径行为层不消费（保留是为了旧调用方编译兼容）。
    double maxSpeed = 40.0;
    double locomotionCost = 0.0002; // 每单位速度平方的耗能（D2 调参：0.0008→0.0002）
    double speedCostExponent = 2.0; // 代谢指数 β：loco ∝ speed^β（R11-P1-3；2.0 = 旧 speed*speed 逐位恒等）
    double damping = 1.0;           // 核速度阻尼（R11-P1-3；vel *= damping 每帧，1.0 = 无阻尼恒等）
    double reproductionCostRatio = 1.0; // 繁殖成本 = 门槛×ratio（R11-P1-3；1.0 = 旧扣满门槛恒等）
    double respirationBase = 0.008; // 被动呼吸基础项（D2 调参：0.01→0.008）
    double respirationK = 0.0004;   // 被动呼吸 × speed^2 项
    double respirationTempK = 1.5;  // 温度因子强度：呼吸 × (1 + respirationTempK * distortion)
    double writeNoiseK = 0.25;      // 写通道扰动强度（随速度单调递增）
    // 球远失控的参考半径。判据：gain = 1/(1+(d/h)^2)，gain < 0.2 <=> d > 2*h。
    // 球半径 r = 10 + 0.5*energy，实测球能峰值约 280~305（r≈150~162）。
    // 取 h = 60 => 球能 > 220（r > 120）即进入失控分支：可达且留有安全余量。
    // （原值 150 需要 r > 300，即球能 > 580，远超实测量级，导致 S7 分支恒定不可达。）
    double handoffRadius = 60.0;
    // ---- R1-B：捕食参数（唯一食物 = 蓝细菌节点）----
    // cyanoPredationRadius：核每帧在其内找**最近**蓝细菌节点（确定性，无随机门槛）。
    // R9-A：迁移回报（见 Nucleus::lastMealPos 注释）。0 = 关闭（逐位回归）。
    double migrationBonus = 0.0;
    double migrationRadius = 120.0;
    // 取值沿用旧 eatRadius 的生态标定（14，25→14 的 D2 调参结论直接平移）。
    double cyanoPredationRadius = 14.0;
    // R2-A：cyanoDigestion 已由 genome.cyanoDigestion 接管（遗传 0.1~1.0，且同时
    // 决定消化冷却时长）。本字段退役为 fallback（genome 值非法时兜底），正常不消费。
    double cyanoDigestion = 1.0;

    // ---- S18：球作仓库（colonyCap）----
    // 核原生能量上限（菌落不产球时的容量底座）。取值 200 与组合根出生引导的
    // n.cap = 200.0 同源：Simulation::spawnNuclei 现改为 n.cap = bp_.nativeCap，
    // 魔数只此一处定义。
    double nativeCap = 200.0;
    // ---- S19/S20：自由球拾取与自然脱离 ----
    double adoptRadius = 18.0;   // 拾取自由球的半径（环面距离 <= 此值才可收编）
    double detachEnergy = 120.0; // 球能量超过此值才允许自然脱离（避免小球乱飞）
    // ---- S21/R2-A：核施力 ----
    // R2-A：施力半径/代价已由基因接管（genome.forceRange 10~400 / genome.forceCostK
    // 1e-4~1.0）。bp.forceRadius / bp.forceCostK 退役为 fallback（genome 值非法时），
    // 正常路径不消费。
    double forceRadius = 40.0; // （退役 fallback）可施力的最大环距
    // forceMax：单次施力加速度上限。基因域没有对应参数（AMEND-1 只定义
    // forceRange/forceCostK），保留为世界物理参数：|a| 超过它按它钳（防止单帧瞬移）。
    double forceMax    = 8.0;
    double forceCostK  = 0.01; // （退役 fallback）代价系数：cost = k * magnitude^2
};

class AgentBehavior {
public:
    // ---- S18：球作仓库 ----
    // 菌落容量 = 核原生上限 + 所有 Resource 球的 cap 之和。
    // Scout/Shield 球不贡献容量（Scout 只影响感知，见 S17；Shield 初版只留类型）。
    // 纯函数：不改任何状态，可单测直测。
    static double colonyCap(const Nucleus& n, const BehaviorParams& bp);

    // 对单个核执行一帧行为：写扰动→移动→耗能→球控制→T08 三动作→容量结算（S18）→死亡判定。
//（R1-B：进食分支已删除——捕食是 Simulation 组合根确定性扫描 + absorbPrey 记账，见下。）
    // rng 为自传 PCG32（写通道扰动 + T08 随机触发采样），固定种子双跑一致。
    static void apply(Nucleus& n, ActionVector action, AgentEnvironment& env, AgentLedger& ledger,
                      double dt, const BehaviorParams& bp, PCG32& rng,
                      const Terrain* terrain = nullptr,
                      double tempDistortion = 0.0,
                      Vec2 externalAcc = Vec2{});
// externalAcc：R2-A 对核物理的社会加速度（两阶段积分：组合根先对全体核算加速度，
// 再经本参数统一积分进合速度，消除"谁先动谁占便宜"的顺序依赖）。默认 {0,0} = 无
// 社会力（旧调用方零改动，逐位回归）。叠加后仍受 genome.maxSpeed 限幅。
// tempDistortion：该核所在点的温度失真度 ∈ [0,1]（0 = 完全适宜），喂给呼吸耗能温度因子。

    // 球控制增益：1/(1+(d/r)^2)。d=0 时为 1（全量跟随），d>>r 时按 (r/d)^2 衰减。
    static double ballControlGain(double distance, double handoffRadius);

    // ---- T08 三动作（公开静态，供单测直测；apply 内部按规则触发）----
    // 产球：能量 > deployCost 时扣 deployCost 并经 transfer 注入种子能量（C3a），
    //       新建 Ball{angle 随机, radius 10, energy=种子}，上限 maxBalls；reason="deploy"。
    //       S19：新球同时初始化世界状态 pos（核位置+轨道极坐标，按 worldW/H wrap）、
    //       vel={0,0}、ownerId=n.id。worldW/H 末位默认参数（=256），旧调用点零改动。
    // type 末位默认 Resource（旧调用点零改动）。S17 接线需要 Scout 球：
    // 没有 Scout 时 sense() 的 scoutCount 恒为 0，感知半径永远只有 1/4，
    // 等于"侦察球是唯一感知来源"这条规格从未在生态里生效。
    static bool deployBall(Nucleus& n, AgentLedger& ledger, PCG32& rng,
                           double worldW = 256.0, double worldH = 256.0,
                           BallType type = BallType::Resource);
    // ---- S19/S20：自由球脱离/拾取（公开静态，供组合根与单测直测）----
    // 脱离：把 n.balls[idx] 摘下变成自由球移入 freeBalls，ownerId 置 0。
    //       能量完全不耗散（S19 A 语义：脱离球保留全部能量），账面零操作。
    //       返回是否执行（idx 越界返回 false）。
    static bool detachBall(Nucleus& n, std::vector<Ball>& freeBalls, std::size_t idx);
    // 拾取：自由球（ownerId==0）被核收编进轨道。torusDist 为调用方预先算好的
    //       环面距离（本层不依赖 environment）；非自由/超距/球数满/核已死拒绝。
    //       成功则反算 b.angle=atan2(dy,dx)、b.radius=torusDist、ownerId=n.id，
    //       push 进 n.balls。能量原样保留（零和，账面零操作）。
    static bool adoptBall(Nucleus& n, Ball& b, double torusDist, const BehaviorParams& bp);
    // 回收：球数 > 0 时销毁一个球，ball.energy * recycleReturn 还给核，
    //       其余记 dissipate("recycle-loss")；销毁扣账 reason="recycle"。
    static bool recycleBall(Nucleus& n, AgentLedger& ledger);
    // 繁殖（R2-A 重写）：能量 > genome.energyThreshold 且核数 < maxNuclei 时
    //       扣 genome.energyThreshold 产一个子核。**繁殖必变异**：
    //       1) 检查 n.energy > n.genome.energyThreshold
    //       2) 扣 energyThreshold（亲代 -k）
    //       3) child = n（复制全部字段含 genome）
    //       4) child.genome = 父代拷贝后 genomeMutate(child.genome, rng, mutationCoefficient,
    //          ranges)——变异只作用于子代，亲代基因逐位不动
    //       5) child.digestCooldown = 0
    //       6) child.energy = childFraction * k（零和转移，耗散 (1-childFraction)*k）
    //       7) 子代位置 = 亲代位置 + 随机偏移（本函数内置；调用方无需再安置）
    //       RNG 消耗序（固定，同 seed 双跑逐字节一致）：
    //         genomeMutate 恰好 50 次 nextDouble（25 个 Box-Muller 高斯，Genome.cpp 契约）
    //         -> 出生偏移 2 次 nextDouble（先 dx 后 dy）
    //         总计恰好 52 次；拒绝路径（不育/超上限）0 次（先检查后消耗）。
    //       零和转移（C1）：亲代 -k、子代 +f*k、耗散 (1-f)*k（reason="reproduce-loss"）。
    //       S10 红线：子代初能 = f*k <= k = 亲代实付。
    static bool reproduce(Nucleus& n, std::vector<Nucleus>& population, AgentLedger& ledger,
                          EntityId childId, PCG32& rng,
                          const GenomeRanges& ranges = GenomeRanges{},
                          double mutationCoefficient = 0.25,
                          double worldW = 256.0, double worldH = 256.0,
                          const BehaviorParams& bp = BehaviorParams());
    // ---- S21/R2-A：核对球施加加速度（公开静态，供组合根与单测直测）----
    // b.vel += normalized(dir) * magnitude；条件：核活着、torusDist <= n.genome.forceRange、
    // magnitude 钳到 [0, bp.forceMax]；代价 cost = n.genome.forceCostK * magnitude^2
    //（钳后值），从 n.energy 扣并 ledger.dissipate("applyForce", cost)；余额不足时按
    // 比例缩小 magnitude（核能量永不为负）。可对任何球施力（含 ownerId==0 的自由球
    // ——"核推动自由球"即此语义）。torusDist 由调用方传入（本层不依赖 environment）。
    // 返回是否真的施加了力（magnitude > 0 且距离合法）。
    // 消费基因：forceRange（距离校验）/ forceCostK（代价）。不消耗 RNG。
    static bool applyForce(Nucleus& n, Ball& b, Vec2 dir, double magnitude,
                           double torusDist, AgentLedger& ledger, const BehaviorParams& bp);

    // ---- R2-A：点名施力（V2-SPEC-AMEND-1 §2 的行为层落地）----
    // 决策层在 ActionVector 里点名（forceSlot = TargetList 下标 + forceX/forceY 加速度
    // 矢量意图），行为层负责全部校验与结算（决策层只表达意图，绝不写实体）：
    //   1) forceSlot < 0 -> 本帧不施力（不花能量的唯一方式），返回 false，零状态变化；
    //   2) slot >= targets.count -> 无效点名（感知层未提供该目标），拒绝；
    //   3) 目标球必须真的在 n.genome.forceRange 内（targets[].dist 是感知时刻快照，
    //      这里按 ForceTarget.dx/dy 位移重算环距校验，防"感知后球已漂走"）；
    //   4) 意图矢量 (forceX,forceY) 长度钳到 [0, min(genome.maxAccel, bp.forceMax)]
    //     ——maxAccel 既是移动加速度上限也是施力上限（同一基因两处生效，卡面要求）；
    //   5) cost = genome.forceCostK * |a|^2；付不起则按 a = a * sqrt(energy/cost) 缩小，
    //      核能量永不为负；
    //   6) ledger.dissipate("applyForce", cost) 后 b.vel += a（行为层唯一写点）。
    // b 引用由调用方按 ForceTarget.id 从球容器取出（id 不存在则拒绝——本函数不持有
    // 球容器）。torusDist 传 targets.items[slot].dist。
    // 消费基因：forceRange（校验）/ forceCostK（代价）/ maxAccel（上限）。
    // 不消耗 RNG（纯确定性：意图 -> 校验 -> 结算）。返回是否真的施了力。
    static bool applyNamedForce(Nucleus& n, Ball& b, const ActionVector& action,
                                const TargetList& targets, int slot,
                                double torusDist, AgentLedger& ledger,
                                const BehaviorParams& bp);

    // ---- R2-A：对核物理（社会参数生效位，AMEND-1 §1）----
    // 两阶段积分的"算加速度"阶段：i 对每个邻居 j 按**各自**的本帧生效参数评估
    //   d = torusDistance(i,j)
    //   d < effSpace_i          -> 斥力：effRepel_i * (1 - d/effSpace_i)，沿 j->i 方向
    //   else d < effSight_i 且 effCohesion_i > 0 -> 聚拢：effCohesion_i * (1 - d/effSight_i)，
    //                                               沿 i->j 方向
    // 硬分离在 Simulation::step 里单独 pass（对称各退一半），不在此处。
    // eff_* 由本函数入口按 eff = base * (1 + kSocialModLimit*clamp(mod)) 重算并缓存到
    // n.eff*（决策层 mod[4] 调制在这里落地；基因本身绝不被改写）。outAcc 是本帧加速度
    // accumulator，主循环随后统一积分（避免顺序依赖）。
    // 只读邻居数组；不改位置；不耗能（社会性物理是免费表型）。纯静态可单测直测。
    static void socialAccelerations(Nucleus& n, const AgentEnvironment& env,
                                    const std::vector<Nucleus>& population,
                                    const ActionVector& action,
                                    Vec2& outAcc);



    // ---- R7-B：球力场（v2.3.1 updateBallDynamics 公式照抄；本卡实现）----
    // 坐标约定：调用方传入的位移向量必须是**已按世界尺寸取最短缠绕**的环面位移
    //（torusDeltaWrapped），本层不做 fmod（本层不依赖 environment 几何）。
    // dt 为积分步长（生产路径恒 1.0，与 v231 逐帧同式）；力 ×dt、阻尼在调用方。
    //
    // ballFieldAccel：单个核 src 对球 b 的力场加速度（径向弹簧 + 切向驱动 +
    // 近距排斥 + 跟随耦合）。disp = src->b 环面位移；odisp = 宿主->b 环面位移，
    // 仅当 src 就是宿主时非 {0,0}（调用方约定；此时跟随耦合弹簧生效，范围
    // detachRadius，超出为 {0,0} 即不生效）。基因 = 源核 src 的
    // affinity/orbitRadius/repelStrength[type]（R2-A：基因即旧版 nucleus params）。
    // 轨道目标口径（与 v231 的唯一偏差，见任务卡对照表）：本项目沿用 R2-A 既有
    // 负载半径公式 targetR = (10 + 0.5*E) * (0.5 + orbitRadius[t]/240)——v231 的
    // 基因原值域（本项目 Init 30~200）与 R6-C BallTuning 标定（near_repel 8 /
    // detach 60 / ball_repel 6）不在同一尺度，直接照抄会大面积脱附；负载半径
    // 口径与全部力场参数标定自洽，且默认基因 120 -> 偏置 1.0 与旧 C3b 逐位同源。
    static Vec2 ballFieldAccel(const Nucleus& src, const Ball& b, Vec2 disp,
                               Vec2 odisp, double dt, const BallFieldParams& p);
    // ballWorldIntegrate：单球积分步（v231:367-390：vel+=acc → 阻尼 → 类型限速 →
    // 跟随加速限速 → pos+=vel → 边界。本项目恒 wrap，bounce 参数不消费）。
    // owner = 宿主核（自由球传 nullptr：无跟随加速）；返回本帧生效限速（观测）。
    // Shield 代谢减免（本卡 §6）不在此——呼吸是核 apply 的账目项，由宿主 apply
    // 按 n.balls 里的 Shield 计数消费（行为层内聚，无需组合根参与）。
    static double ballWorldIntegrate(Ball& b, Vec2 acc, double dt, Nucleus* owner,
                                     double worldW, double worldH,
                                     const BallFieldParams& p);
    // ballPairAccel：球 o 对球 b 的成对作用加速度（短程排斥 / 同宿主吸引）。
    // disp = o->b 环面位移。同宿主判定用 ownerId（0 = 自由球不吸引）。与 v231
    // 逐对双计数语义一致（每对双方各调一次）。
    static Vec2 ballPairAccel(const Ball& b, const Ball& o, Vec2 disp,
                              double dt, const BallFieldParams& p);
    // ballMaxSpeed：球的本帧限速 = max(类型上限, 宿主当前速度 × followBoost)
    //（v2.3.1:370-383；n 为宿主核）。类型上限 Shield/Resource/Scout 三档。
    static double ballMaxSpeed(const Nucleus& owner, const Ball& b,
                               const BallFieldParams& p);
    // transferBall：R7-B 换主**直接交接**（不经 detached_/自由池中转——新宿主
    // 当帧立即持有，防交接缓冲把球当脱离球误送自由池）。能量原样、账面零操作
    //（与 detachBall 同为零和搬运）。成功 = 球从 from.balls 摘除并挂入 to.balls
    //（ownerId = to.id）。
    static bool transferBall(Nucleus& from, std::size_t idx, Nucleus& to);
    // reassignBall：R7-B 归属结算单步（v2.3.1 updateOwnership 语义；行为层唯一
    // 写点）。best = 调用方扫出的"detachRadius 内最近存活核"下标（population
    // 下标；无 = SIZE_MAX）。宿主不变返回 false（零状态）；否则对**原主**扣
    // lossCost（限额支出，能量永不为负，dissipate "ball_loss"），换主走
    // transferBall、失主走 detachBall（进调用方 freeBalls，能量原样）。
    // 返回 true = n.balls[idx] 已被摘除（调用方须原地下标重查）。
    static bool reassignBall(Nucleus& n, std::size_t idx, std::size_t best,
                             std::vector<Nucleus>& population,
                             std::vector<Ball>& freeBalls, AgentLedger& ledger,
                             const BallFieldParams& p);

    // ---- S22 捕食接线：核吃掉蓝细菌的肉 ----
    // 为什么需要这个包装：CyanoField::graze() 是环境层的东西，行为层不能 include
    // environment 的实体类（分层红线），而"把猎物能量加到自己身上"必须由行为层做
    // （behavior-is-only-writer 门禁）。所以拆成两半：
    //   1) 组合根（Simulation）调 cyano_.graze() 从蓝细菌扣能量，拿到 taken；
    //   2) 组合根调本函数，把 taken 记到核的账上（唯一写点仍在行为层）。
// R1-B：旧的"queryFood/consumeFood 进食分支"已从 apply() 删除，本函数是核
// 唯一的外部能量入口（除 ledger 注入）。
    // 能量流向：蓝细菌 → 核，零和搬运（ledger.transfer），不产生也不消耗。
    static void absorbPrey(Nucleus& n, EntityId preyId, double amount, AgentLedger& ledger);
};

} // namespace agentsim

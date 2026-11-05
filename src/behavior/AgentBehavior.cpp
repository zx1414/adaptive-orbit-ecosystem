// agentsim - behavior/AgentBehavior.cpp
#include "AgentBehavior.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "../environment/Terrain.h"

namespace agentsim {
namespace {

// ---- T08 三动作参数（按任务约定不改 Config.h，常量 + 注释收敛于此）----
// 产球：能量须 > deployCost；每核球数上限；新球轨道半径固定 10。
constexpr double kDeployCost = 8.0;
constexpr int kMaxBalls = 3;
constexpr double kBallRadius = 10.0;
// C3a：产球种子能量——新球诞生时从核侧经 ledger.transfer 注入的初始储能
//（取 kReproCost*0.5；kReproCost 声明见下方繁殖段，此处按值同步）。
constexpr double kBallSeedEnergy = 10.0;
// 回收：销毁球时把 ball.energy * recycleReturn 还给核，其余耗散（recycle-loss）。
constexpr double kRecycleReturn = 0.5;
// 繁殖（R2-A）：子核初始能量取 genome.energyThreshold * childFraction（<= 门槛，
// S10 红线：子代初能不得超过亲代为繁殖消耗的能量），余下部分耗散。繁殖门槛本身
// 已基因化（genome.energyThreshold，Init 80~200），kReproCost/kMaxNuclei 常数删除
// ——种群上限走 ColonyConfig::maxNuclei（性能护栏），承载力由能量自然决定。
constexpr double kChildEnergyFraction = 0.5;
// R2-A：社会参数调制上限（AMEND-1 §1：eff = base*(1 + kModLimit*mod)，kModLimit=0.4）。
constexpr double kSocialModLimit = 0.4;
// R2-A：出生偏移半径上限（子代出生在亲代附近 [0,kBirthOffset) 随机偏移，防重叠出生）。
constexpr double kBirthOffset = 12.0;
// R2-A §8：死亡能量分配——70% 当场耗散（热力学损失），30% 进尸体营养包（Carrion）
// 由组合根分配给最近的蓝细菌节点。ECOLOGY-DESIGN §三.2 的营养循环地基。
constexpr double kDeathCarrionFraction = 0.3;
// T08 内部触发规则：能量阈值 + 少量随机（走自传 RNG，确定性）。优先级：繁殖 > 产球 > 回收。
constexpr double kDeployTriggerFrac = 0.6;  // 能量超过 0.6*cap 后开始考虑产球
constexpr double kReproTriggerFrac = 0.9;   // 能量超过 0.9*cap 后开始考虑繁殖
constexpr double kRecycleEnergyFrac = 0.25; // 能量低于 0.25*cap 时回收球换能
constexpr double kActionRollP = 0.10;       // 阈值满足时每 tick 的触发概率（少量随机）

// ---- S18：球作仓库（colonyCap / capOverflow）----
// Resource 球的默认仓库容量。deployBall 的派生 id 带有 0xB000 球特征位，球默认
// type = BallType::Resource（字段默认值，deploy 内不显式设 type），故产出的球
// 天然按 60/球 计入菌落容量。
constexpr double kBallCycleCap = 60.0;
// 编译期护栏：Ball 的默认容量与本文档常量同源——deploy 产出的球（默认
// type=Resource）必然按 60/球 计入菌落容量。两处漂移会在编译期报错。
static_assert(Ball{}.cap == kBallCycleCap,
              "Ball default cap must stay in sync with kBallCycleCap");

constexpr double kTwoPi = 6.28318530717958647692;

// ---- R2-A：基因值守卫 ----
// 基因是"每核行为参数的唯一权威"，但 Genome 是 POD：默认构造/夹具直改都可能给出
// NaN 或 <=0 的非法值。所有消费点统一走这里：非法值回退到 BehaviorParams 的旧
// 常数（退役 fallback），合法值原样返回。这样保证：
//   1) 基因正常时行为由基因决定（无死基因）；
//   2) 垃圾基因不产生 NaN 传播（NaN 防御纪律与 clampd 同款）。
inline double geneOr(double gene, double fallback) {
    return (gene >= 0.0 && gene <= 1e300) ? gene : fallback; // NaN/负/inf 落 fallback
}

} // namespace

double AgentBehavior::ballControlGain(double distance, double handoffRadius) {
    // 控制增益 1/(1+(d/r)^2)：球离核越远，同一指令产生的角速度越小。
    // d=0 时为 1（全量跟随），d=r 时 0.5，d>>r 时按 (r/d)^2 衰减。
    // 物理含义：控制信号强度随距离平方弥散，这就是"球远了控制失调"的根源。
    if (handoffRadius <= 0.0) {
        // 参考半径非正时的退化标度：按 d=1 归一。
        return 1.0 / (1.0 + distance * distance);
    }
    const double q = distance / handoffRadius;
    return 1.0 / (1.0 + q * q);
}
// ---- S18：球作仓库 ----
// 菌落容量 = 核原生上限（bp.nativeCap）+ Σ(own Resource 球.cap)。
// Scout/Shield 球不贡献容量：Scout 只影响感知（S17），Shield 初版只占类型位。
// 纯函数：只读 n.balls，不改任何状态；非 Resource 球即使带 cap 也不计入。
double AgentBehavior::colonyCap(const Nucleus& n, const BehaviorParams& bp) {
    double sum = bp.nativeCap;
    for (const Ball& b : n.balls) {
        if (b.type == BallType::Resource) sum += b.cap;
    }
    return sum;
}
// ---- T08 产球 deploy ----
// 语义：耗能产生一个小球。能量 > deployCost 且球数未达上限时，扣 deployCost（账目
// reason="deploy"，全额耗散），再从核侧经 ledger.transfer 注入种子能量 kBallSeedEnergy
//（C3a：球自诞生起即有非零储能，recycle 返还不再恒 0；零和搬运，不改注入/耗散总额），
// 新建 Ball{angle 随机, radius 10, energy=seed}。返回是否执行。
bool AgentBehavior::deployBall(Nucleus& n, AgentLedger& ledger, PCG32& rng,
                               double worldW, double worldH, BallType type) {
    if (!n.alive) return false;
    if (n.energy <= kDeployCost) return false;             // 严格大于才花得起
    if (static_cast<int>(n.balls.size()) >= kMaxBalls) return false;
    n.energy -= kDeployCost;                               // 唯一写点：行为层
    ledger.dissipate("deploy", kDeployCost);               // 账实相等（零和）
    Ball b;
    // 球 id 必须全局唯一且**不复用**。
    //
    // 旧写法 b.id = (n.id << 16) | 0xB000 | n.balls.size()：
    // 用「当前球数」当低位，回收一个球让 size() 变小后，再产球就会撞上
    // 刚被回收那颗的 id。实测 20000 tick 轨迹里出现 2 帧「同一颗球同时挂在
    // 两个核名下」（tick 5997 的 id 1945600）——按 id 追踪球会串账，
    // 前端也会把两个实体画成一个（红队 R7 P2-11）。
    //
    // 改为：宿主前缀保证可追溯 + 进程级单调计数保证唯一。
    // 计数器是 static，进程内单调，不依赖容器大小，因此复用不可能发生。
    // 确定性不受影响：同一 seed 下产球的次数与顺序完全一致，计数器走过的值也一致。
    static EntityId s_nextBallSerial = 0;
    ++s_nextBallSerial;
    b.id = (n.id << 16) | 0xB000ULL | (s_nextBallSerial & 0x0FFFULL);
    b.angle = rng.nextDouble() * kTwoPi;                   // 随机轨道角（自传 RNG）
    b.radius = kBallRadius;            // 初值；apply 每帧按球能单调重算（C3b）
    b.energy = 0.0;
    b.lastOmega = 0.0;
    // C3a：产球即从核侧转移种子能量到球（零和搬运，走账本）。
    // 核扣完 deployCost 后余额不足种子额时不注（球保持 0 能），绝不变负。
    if (n.energy > kBallSeedEnergy) {
        n.energy -= kBallSeedEnergy;                     // 唯一写点：行为层
        b.energy = kBallSeedEnergy;
        ledger.transfer(n.id, b.id, kBallSeedEnergy);
    }
    // R2-A：诞生半径 = 负载半径 × 基因偏置（0.5 + orbitRadius[type]/240；默认基因
    // 120 → 偏置 1.0，与旧 C3b 公式逐位一致）。
    {
        const int ti = static_cast<int>(type);
        const double orbRaw = (ti >= 0 && ti < kBallTypeCount)
                                  ? n.genome.orbitRadius[ti] : 120.0;
        const double orb = geneOr(orbRaw, 120.0);
        b.radius = (kBallRadius + 0.5 * (b.energy > 0.0 ? b.energy : 0.0)) *
                   (0.5 + orb / 240.0);
    }
    // S19：初始化世界状态。pos = 核位置 + 轨道极坐标偏移（按调用方给的世界尺寸 wrap；
    // worldW/H <= 0 时 wrap 退化原样返回）。vel 静止；ownerId 记宿主核（非自由球）。
    const double ca = std::cos(b.angle);
    const double sa = std::sin(b.angle);
    b.pos = wrap(Vec2{n.pos.x + b.radius * ca, n.pos.y + b.radius * sa}, worldW, worldH);
    b.vel = Vec2{0.0, 0.0};
    b.ownerId = n.id;
    b.type = type;  // S17：调用方决定产 Resource 还是 Scout
    b.r7Lane = true; // R7-B：新球进入世界积分车道（世界坐标权威口径，任务卡 §7）
    n.balls.push_back(b);
    return true;
}

// ---- S19 自然脱离 detachBall ----
// 语义：把 n.balls[idx] 摘下变成自由球移入 freeBalls。能量完全不耗散（S19 A 语义：
// 脱离球保留全部能量，逐位不变），账面零操作（不 dissipate、不 transfer），
// 零和恒等式自动保持——能量只是换了宿主容器。ownerId 置 0，pos/vel 原样带走。
bool AgentBehavior::detachBall(Nucleus& n, std::vector<Ball>& freeBalls, std::size_t idx) {
    if (idx >= n.balls.size()) return false;
    Ball b = n.balls[idx];                                 // 值拷贝：能量逐位不变
    b.ownerId = 0;                                         // 0 = 自由球
    freeBalls.push_back(std::move(b));
    n.balls.erase(n.balls.begin() + static_cast<std::ptrdiff_t>(idx));
    return true;
}

// ---- S20 拾取 adoptBall ----
// 语义：自由球（ownerId==0）被核收编进轨道。torusDist 由调用方预先算好（环面
// 最小距离，本层不依赖 environment）；须 <= bp.adoptRadius 且核球数未满。
// 成功则反算轨道几何：radius = torusDist，angle = atan2(dy,dx)（由 b.pos 与 n.pos
// 的差向量反推；环面接缝两侧拾取时该差向量可能指错侧——拾取半径 18 远小于世界
// 半宽，接缝带极窄，且下一帧 apply 会按新 angle 重建轨道位置，不影响守恒），
// ownerId 置为新核 id。能量原样保留（零和搬运之外更无账目：账面零操作）。
bool AgentBehavior::adoptBall(Nucleus& n, Ball& b, double torusDist, const BehaviorParams& bp) {
    if (!n.alive) return false;
    if (b.ownerId != 0) return false;                      // 只收自由球
    if (!(torusDist >= 0.0) || torusDist > bp.adoptRadius) return false; // NaN/负值/超距一律拒绝
    if (static_cast<int>(n.balls.size()) >= kMaxBalls) return false;
    b.ownerId = n.id;
    // 【S19/S20 语义修正】radius 是**负载基因**（由球能决定：10 + 0.5E），
    // 不是"拾取瞬间的偶然距离"。
    //
    // 旧写法 b.radius = torusDist 造成严重耦合缺陷：自由球必须在 adoptRadius(18)
    // 内才会被拾取，于是每个被拾取的球半径都被重置为 <18；而"失控"判据是
    // gain = 1/(1+(radius/60)^2) < 0.2（要求 radius > 120）。
    // 结果：一个 E=225.9（本应 radius=123、早该失控脱离）的高能球，一被拾取
    // 半径就塌回 17，gain 回到 0.95 —— **S19 的脱离条件因此永远不可达**
    // （实测 5000 tick、23278 个球帧，gain<0.2 的帧数恒为 0，自由球计数 0）。
    //
    // 修正：拾取只接管所有权与轨道角，半径仍由能量给出。高能球被收编后依然
    // 半径很大、依然会再次失控并脱离，S19 与 S20 才能共存。
    // R2-A：收编半径同按负载 × 基因偏置（默认基因 120 → 与旧公式逐位一致）。
    {
        const int ti = static_cast<int>(b.type);
        const double orbRaw = (ti >= 0 && ti < kBallTypeCount)
                                  ? n.genome.orbitRadius[ti] : 120.0;
        const double orb = geneOr(orbRaw, 120.0);
        b.radius = (kBallRadius + 0.5 * (b.energy > 0.0 ? b.energy : 0.0)) *
                   (0.5 + orb / 240.0);
    }
    b.angle = std::fmod(std::atan2(b.pos.y - n.pos.y, b.pos.x - n.pos.x) + kTwoPi, kTwoPi);
    b.vel = Vec2{0.0, 0.0};                                // 收编即入轨，漂移速度清零
    b.r7Lane = true; // R7-B：收编球进入世界积分车道（宿主名下随力场推进）
    n.balls.push_back(b);
    return true;
}

// ---- T08 回收 recycle（R2-A：absorbPreference 基因生效位）----
// 语义：耗能将小球转化为少量能量。球数 > 0 时销毁一个球：
//   ball.energy * recycleReturn * genome.absorbPreference[type] 还给核
//   （transfer 球→核，零和搬运），其余部分记 dissipate("recycle-loss")；
//   销毁动作本身记 dissipate("recycle")。
// R2-A：absorbPreference[t]（遗传 0.1~3.0）是"回收该类型球时的能量倍率"——
// 高倍率基因型把球当可回收仓库（循环快），低倍率型吝啬回收（球留给容量）。
// 账目约束：dissipate 只对真实存在的能量记账——球能为 E，返还 rE，耗散 (1-r)E + 销毁费，
// 核侧只增 rE，账面耗散 = 账面消失量，恒等式保持。返回是否执行。
bool AgentBehavior::recycleBall(Nucleus& n, AgentLedger& ledger) {
    if (!n.alive) return false;
    if (n.balls.empty()) return false;
    const Ball b = n.balls.back();
    n.balls.pop_back();
    if (b.energy > 0.0) {
        // R2-A：回收倍率基因化（按被回收球的类型取 genome.absorbPreference[t]，
        // 非法值回退 1.0 = 旧行为）。返还率 = kRecycleReturn * preference，
        // 钳到 [0,1]：倍率再大也不能凭空造能（零和红线）。
        const int ti = static_cast<int>(b.type);
        const double prefRaw = (ti >= 0 && ti < kBallTypeCount)
                                   ? n.genome.absorbPreference[ti] : 1.0;
        const double pref = geneOr(prefRaw, 1.0);
        double give = b.energy * kRecycleReturn * pref;
        if (!(give > 0.0)) give = 0.0;
        if (give > b.energy) give = b.energy;   // 返还不得超过球内实有能量
        const double loss = b.energy - give;
        // 球→核 零和转移（不改注入/耗散总额）；球已销毁，转移目标为宿主核。
        ledger.transfer(b.id, n.id, give);
        n.energy += give;                                  // 唯一写点：行为层
        if (loss > 0.0) ledger.dissipate("recycle-loss", loss);
    }
    ledger.dissipate("recycle", 0.0); // 销毁事件记账痕迹（金额 0，不破坏零和）
    return true;
}
// ---- T08 繁殖 reproduce（R2-A 重写：基因门槛 + 必变异 + 零和转移）----
// S10 红线 / C1 零和：亲代扣 k=genome.energyThreshold，子代得 f*k，耗散 (1-f)*k。
// 变异契约（确定性红线，RNG 消耗序固定）：
//   1. 检查 n.energy > n.genome.energyThreshold（拒绝路径 0 次 RNG）
//   2. 检查种群 < maxNuclei（拒绝路径 0 次 RNG）
//   3. 亲代扣 k
//   4. child = n（全字段拷贝含 genome、digestCooldown 也复制后立即清零）
//   5. genomeMutate(child.genome, rng, mutationCoefficient, ranges)
//      —— 恰好 50 次 nextDouble（25 个 Box-Muller 高斯，Genome.cpp 顶部契约）
//   6. child.digestCooldown = 0（子代出生即可捕食，不继承亲代消化状态）
//   7. child.energy = f * k；ledger.dissipate("reproduce-loss", (1-f)*k)
//   8. 出生偏移：dx = rng 1 次、dy = rng 1 次（(2u-1)*kBirthOffset，均匀方形）
//      —— 恰好 2 次 nextDouble；wrap 由调用方世界尺寸保证
//   合计成功路径恰好 52 次 nextDouble；同 seed 双跑逐字节一致。
// 注意：变异只作用于子代基因（获得性不得遗传——拉马克红线）；亲代 genome 逐位不动。
bool AgentBehavior::reproduce(Nucleus& n, std::vector<Nucleus>& population,
                              AgentLedger& ledger, EntityId childId, PCG32& rng,
                              const GenomeRanges& ranges, double mutationCoefficient,
                              double worldW, double worldH,
                              const BehaviorParams& bp) {
    if (!n.alive) return false;
    // R2-A：繁殖门槛基因化（Init 80~200）。旧的固定 kReproCost=20 删除——
    // 门槛低=早育多子（r-策略），门槛高=晚育壮子（K-策略），这是本卡要的核心分化。
    // R11-P1-3：reproductionCostRatio 接线（ratio=1.0 时 k*1.0 与旧扣满门槛逐位相同）。
    const double threshold = geneOr(n.genome.energyThreshold, 20.0);
    const double k = threshold * bp.reproductionCostRatio;
    if (!(n.energy > k)) return false;                     // 严格大于才花得起
    // 种群护栏：ColonyConfig::maxNuclei（默认 300）经调用方传入？不——护栏由组合根
    // 在采样侧把守（aliveCount_ < maxNuclei 才调用本函数），本函数保留旧语义：
    // 只做能量与活性校验，容量账以组合根 aliveCount_ 为准（避免二次计数口径漂移）。
    n.energy -= k;                                         // 唯一写点：行为层
    Nucleus child = n;                                     // 3) 全字段拷贝（含 genome）
    child.id = childId;
    child.balls.clear();                                   // 子代不带亲代球（球是亲代资产）
    child.detached_.clear();
    // 5) 繁殖必变异：先拷贝父代，再对子代变异（coeff = cfg.mutationCoefficient，
    // 默认 0.25）。genomeMutate 收尾自带 genomeClamp + genomeNormalizeAffinity。
    genomeMutate(child.genome, rng, mutationCoefficient, ranges);
    // 6) 消化冷却归零。
    child.digestCooldown = 0;
    // 7) 子代初能 = f*k（S10：f*k <= k），差额耗散。
    child.energy = k * kChildEnergyFraction;
    ledger.dissipate("reproduce-loss", k - child.energy);
    // 8) 出生位置：亲代附近随机偏移（先 dx 后 dy，各恰好 1 次 nextDouble）。
    child.pos = wrap(Vec2{n.pos.x + (2.0 * rng.nextDouble() - 1.0) * kBirthOffset,
                          n.pos.y + (2.0 * rng.nextDouble() - 1.0) * kBirthOffset},
                     worldW, worldH);
    // C1：零和转移，不做 injectInitial（否则恒等式差 f*k，红队实测 DIFF=-10）。
    population.push_back(child);
    return true;
}
void AgentBehavior::apply(Nucleus& n, ActionVector action, AgentEnvironment& env, AgentLedger& ledger,
                          double dt, const BehaviorParams& bp, PCG32& rng,
                          const Terrain* terrain, double tempDistortion,
                          Vec2 externalAcc) {
    if (!n.alive) {
        return; // 死核不再有任何行为，也不再耗能。
    }
    // R2-A 基因生效总览（本函数内逐项落地，全部走 genomeOrFallback 守卫）：
    //   maxSpeed      -> 移动限速（步骤 2）
    //   maxAccel      -> 每帧合加速度上限（步骤 2.5，本帧 externalAcc+social 全部限幅）
    //   digestCooldown 由组合根递减（这里只读，不捕食——捕食在组合根）
    //   energyThreshold / mutationRate / orbitRadius / absorbPreference / repelStrength /
    //   nucleus* / forceRange / forceCostK / cyanoPreference / cyanoDigestion
    //   各自的生效位见对应函数（reproduce / applyForce / applyNamedForce /
    //   socialAccelerations / 组合根捕食段）。

    // ---- 1. 写通道扰动（"双重干扰"的写侧）----
    // amp = k * s/(1+s)：s>=0 时对速度 s 单调递增且上界为 k。
    // 低速指令几乎纯净，高速时方向向量被均匀抖动污染——扰动强度与速度挂钩。
    const double s = n.speed;
    const double amp = bp.writeNoiseK * s / (1.0 + s);
    if (amp > 0.0) {
        // 均匀噪声 U(-amp,+amp)：(2*rand-1)*amp。自传 PCG32 保证固定种子双跑一致。
        action.dirX += amp * (2.0 * rng.nextDouble() - 1.0);
        action.dirY += amp * (2.0 * rng.nextDouble() - 1.0);
    }

    // ---- 2. 移动：dir*moveK，限幅 genome.maxSpeed，叠加社会加速度，环面 wrap ----
    // C4: 决策层写入的冲刺意愿 boost(0..1) 在此被消费：缩放限幅
    // effectiveMax = maxSpeed * (1 + 0.5*boost)；对 NaN/越界值钳到 [0,1]。
    // R2-A：限速上限 = genome.maxSpeed（基因，Init 30~100）——旧的固定 40 退役为
    // fallback。"慢爬猛吃 vs 快跑少吃"的生态位分化从这里开始（快核耗散大、
    // 转化率被打折，但觅食半径推进快）。
    double boost = action.boost;
    if (!(boost > 0.0)) boost = 0.0; // NaN/负值一律归 0
    if (boost > 1.0) boost = 1.0;
    const double geneSpeed = geneOr(n.genome.maxSpeed, bp.maxSpeed);
    const double effectiveMax = geneSpeed * (1.0 + 0.5 * boost);
    Vec2 dir = normalized(Vec2{action.dirX, action.dirY});
    // R2-A：maxAccel 生效位（每帧加速度向量长度上限）。baseVel 是本帧速度意图，
    // 与上一帧速度的差 = 本帧实际加速度；其模长钳到 genome.maxAccel——高速核
    // 无法急转（机动性遗传代价），低速核可以急转。
    // externalAcc（对核物理的社会加速度）在同一合加速度上叠加后再统一限幅：
    // 两阶段积分的"积分"阶段，消除核间作用顺序依赖。
    const Vec2 baseVel{dir.x * bp.moveK, dir.y * bp.moveK};
    Vec2 vel{baseVel.x + externalAcc.x, baseVel.y + externalAcc.y};
    const double geneAccel = geneOr(n.genome.maxAccel, bp.moveK);
    {
        const double accelSp = length(Vec2{vel.x - n.vel.x, vel.y - n.vel.y});
        if (accelSp > geneAccel) {
            const double k = geneAccel / accelSp;
            vel.x = n.vel.x + (vel.x - n.vel.x) * k;
            vel.y = n.vel.y + (vel.y - n.vel.y) * k;
        }
    }
    const double sp = length(vel);
    if (sp > effectiveMax) {
        // 限幅：只缩不放大，超界按比例压回上界。
        const double scale = effectiveMax / sp;
        vel.x *= scale;
        vel.y *= scale;
    }
    // R11-P1-3：nucleus_damping 接线（1.0 时 *1.0 在 IEEE754 下逐位恒等）。
    vel.x *= bp.damping;
    vel.y *= bp.damping;
    n.vel = vel;
    n.speed = length(n.vel); // 记录实际速度，供下一帧读/写扰动与呼吸耗能使用
    // T43 地形接线：先算 wrap 后的目标点；terrain 非空时经 resolveMove 滑到最近
    // 合法点（被障碍挡住时沿 from→to 二分回退）。terrain==nullptr 时与原行为逐位一致。
    Vec2 newPos = env.wrap(Vec2{n.pos.x + n.vel.x * dt, n.pos.y + n.vel.y * dt});
    if (terrain != nullptr) {
        newPos = terrain->resolveMove(n.pos, newPos);
    }
    n.pos = newPos;

    // ---- 3. 移动耗能 ∝ speed^2·dt（主动行为代价）----
    // 从 n.energy 扣，等额记入 ledger 耗散账，维持零和恒等式（S3）。
    // T43：移动耗能乘地形倍率（水域 >1 更费）。terrain==nullptr 时恒乘 1.0，
    // 与原表达式逐位一致（x*1.0 在 IEEE754 下无任何位变化）。
    const double locoMul = (terrain != nullptr) ? terrain->moveCostMultiplier(n.pos) : 1.0;
    // R11-P1-3：speedCostExponent 接线（β=2.0 时 pow(s,2) 与旧 s*s 逐位相同）。
    const double loco = (bp.speedCostExponent == 2.0)
                            ? bp.locomotionCost * n.speed * n.speed * dt * locoMul
                            : bp.locomotionCost * std::pow(n.speed, bp.speedCostExponent) * dt * locoMul;
    if (loco > 0.0) {
        // 限额支出：余额不足时只花现有能量，实体能量永不为负，
        // 保证账面耗散与实体扣减严格相等（零和恒等式的数值前提）。
        const double spend = loco < n.energy ? loco : n.energy;
        if (spend > 0.0) {
            n.energy -= spend;
            ledger.dissipate("locomotion", spend);
        }
    }

    // ---- 4. 被动呼吸：基础项 + 速度项，再乘以温度因子 ----
    // 规格要求「呼吸耗能与速度、温度相关」：温度偏离最适最适宜（distortion 越大），
    // 维持稳态的代价越高 —— 用乘性因子 (1 + K * distortion) 表达，K = bp.respirationTempK。
    // tempDistortion 默认 0（完全适宜），此时乘 1.0，与旧表达式逐位一致。
    double d = tempDistortion;
    if (!(d >= 0.0)) d = 0.0; // NaN/负值防御
    if (d > 1.0) d = 1.0;
    const double tempFactor = 1.0 + bp.respirationTempK * d;
    // ---- R7-B §6：Shield 球减代谢（V2-SPEC B 段：旧版只给了速度上限差异，
    // 本项目规格要求真实机制）——基础呼吸项 × 1/(1 + 0.35 × shieldCount)。
    // 只作用于 respirationBase（基础维持），速度项与温度因子不动（护盾隔热
    // 不减运动代价）。仅统计宿主名下 Shield 球（失控脱离/换主后自然失效）。
    int shieldCount = 0;
    for (const Ball& sb : n.balls) {
        if (sb.type == BallType::Shield) ++shieldCount;
    }
    const double baseResp =
        (shieldCount > 0) ? bp.respirationBase / (1.0 + 0.35 * shieldCount)
                          : bp.respirationBase;
    const double resp =
        (baseResp + bp.respirationK * n.speed * n.speed) * tempFactor * dt;
    if (resp > 0.0) {
        // 限额支出：同上，防止负能量残差破坏账实相等。
        const double spend = resp < n.energy ? resp : n.energy;
        if (spend > 0.0) {
            n.energy -= spend;
            ledger.dissipate("respiration", spend);
        }
    }
    // ---- 5. 进食（R1-B 删除）----
    // 旧实现：eatRadius 内最多 2 个 Food，env.queryFood/consumeFood 全额消耗后
    // transfer(kFoodPoolId, n.id, got)，并把 60% 转给能量最少的球充能。
    // 用户拍板「蓝细菌本身是食物，取消食物球」：Food 实体整条链路已删除，
    // 核的唯一食物 = 蓝细菌节点。捕食流程移到 Simulation 组合根（确定性扫描
    // 最近节点 + CyanoParams 转化率公式），账面经 AgentBehavior::absorbPrey
    //（唯一写点仍在行为层）。行为层的 apply() 不再有任何进食分支；
    // 旧"进食所得转给球充能"路径随分支一起删除——球充能改由 T08 产球种子与
    // 后续能量策略承担（组合根捕食在 apply 之后，球充能语义由下一轮接线决策）。

    // ---- 6. 球控制：轨道角速度随控制增益缩放；球远失控则漂移 ----
    // S19：失控（gain < 0.2）且高能（b.energy > bp.detachEnergy）的球在循环内
    // 记下下标，循环后 6a 统一摘除进 n.detached_（边遍历边 erase 是 UB）。
    std::vector<std::size_t> toDetach;
    for (std::size_t bi = 0; bi < n.balls.size(); ++bi) {
        Ball& b = n.balls[bi];
        // R7-B：世界积分车道球不进轨道模型——pos/vel 由组合根 ballWorldStep 权威
        // 推进（v2.3.1 力学），本循环若再按极坐标重建会把世界积分覆盖掉。
        if (b.r7Lane) continue;
        // C3b: 轨道半径随球能量单调增长：radius = 10 + 0.5*ball.energy。
        // 充能球可逼近/超过 handoffRadius，使增益 1/(1+(d/r)^2) 真正衰减进失控分支；
        // 球心坐标仍经 env.wrap、距离仍经 env.torusDistance，wrap 后账目与几何均正确。
        // 【S19 关键修正】radius 仍按能量算（它是"负载基因"的度量，供 C3b/S7 消费），
        // 但**球的真实位置来自 b.pos 本身**，不再每帧用 radius 从核位置重算覆盖。
        //
        // 旧写法（错误）：ballPos = wrap(核位置 + radius*方向)，直接覆盖世界坐标。
        // 后果：b.pos 成为 radius 的派生量，永远落在半径 = 10+0.5E 的圆上，
        // 而 b.pos 相对核的真实环距可以与之不等（实测 tick 2258：radius 字段
        // 143.78，真实环距 117.77）。于是"gain<0.2 且高能即脱离"的判据用的是
        // 错的半径，S19 在生产路径上**从不触发**（5000 tick 自由球计数 = 0）。
        //
        // 现在：**受控球用轨道模型（位置由 radius/angle 决定），失控球用自由
        // 世界坐标模型（位置由速度积分决定）**。两者都保证 radius 字段
        // 恒等于"球到核的真实最小环距"，因此 gain 判据用的是真实几何。
        //
        // 关键点：先按**受控轨道**假设算出球会在哪、距离是多少，再据此判 gain；
        // 判完之后才决定这一帧采用哪套模型更新状态。避免"用旧位置算 dist、
        // 又用新位置算 radius"的错位（那会让 radius 永远滞后一帧，
        // 实测 19713 个球帧里有 2409 个 radius≠真实环距）。
        const double ca = std::cos(b.angle);
        const double sa = std::sin(b.angle);
        const Vec2 orbitPos =
            env.wrap(Vec2{n.pos.x + b.radius * ca, n.pos.y + b.radius * sa});
        // 失控判据用**球当前的真实环距**（b.pos 与核位置之间），而不是
        // "轨道模型假定它会在哪"。
        //
        // 这一点很关键：adoptBall 只接管"所有权 + 轨道角"，半径仍由能量给出
        // （负载基因，见 adoptBall 注释）。于是刚被收编的高能球可能出现
        // "radius=123（负载大）但真实距离只有 12（刚被捡到手边）"的状态。
        // 用 radius 判失控会把这种球判成失控；用真实距离判则是"受控"，
        // 下一帧 apply 会把它吸附到轨道上，距离自然涨到 radius，之后再失控。
        // 这样既保住了"负载基因"语义，又保证几何判据与实际位置一致。
        const double distNow = env.torusDistance(n.pos, b.pos);

        const double gain = ballControlGain(distNow, bp.handoffRadius);
        // ---- R2-A：球动力学的三个基因生效位（V2-SPEC §D 径向弹簧 + 近距排斥）----
        // targetR = 负载半径（10+0.5E，S18/S19 语义保留）× 基因偏置
        //   bias = 0.5 + genome.orbitRadius[t]/240：默认基因 120 → bias=1.0
        //   （中性：radius 逐位不动，全部 radius 断言测试保真）；生态值 30~200
        //   → bias 0.625~1.33——orbitRadius 大的核球轨道外扩。
        // affinity[t] = 径向弹簧回拉速率：radius 每帧向 targetR 松弛 0.3*affinity。
        // repelStrength[t] = 近距排斥速率：d < targetR 时每帧外推 0.3*repelStrength。
        const int ti = static_cast<int>(b.type);
        const double orbRaw = (ti >= 0 && ti < kBallTypeCount)
                                  ? n.genome.orbitRadius[ti] : 120.0;
        const double orb = geneOr(orbRaw, 120.0);
        const double affRaw = (ti >= 0 && ti < kBallTypeCount)
                                  ? n.genome.affinity[ti] : (1.0 / kBallTypeCount);
        const double aff = geneOr(affRaw, 1.0 / kBallTypeCount);
        const double repRaw = (ti >= 0 && ti < kBallTypeCount)
                                  ? n.genome.repelStrength[ti] : 1.5;
        const double rep = geneOr(repRaw, 1.5);
        const double loadR = kBallRadius +
            0.5 * (b.energy > 0.0 ? b.energy : 0.0);
        const double targetR = loadR * (0.5 + orb / 240.0);
        const double springRate = 0.3 * aff;      // 径向弹簧（回拉）
        const double repelRate = 0.3 * rep;       // 近距排斥（外推）
        if (gain >= 0.2) {
            // 受控：角速度按增益缩放（初版指令恒为 +1 rad/s 的环绕方向）。
            const double omega = gain; // 指令角速度 1.0 * gain
            b.angle = std::fmod(b.angle + omega * dt + kTwoPi, kTwoPi);
            b.lastOmega = omega;
            // R2-A：径向弹簧——radius 向 targetR 松弛（d<targetR 用排斥速率外推，
            // d>=targetR 用弹簧速率回拉），再把位置重建到新半径上。
            const double curR = env.torusDistance(n.pos, b.pos);
            double newR = curR;
            if (curR < targetR) {
                newR = curR + (targetR - curR) * std::min(1.0, repelRate * dt);
            } else {
                newR = curR + (targetR - curR) * std::min(1.0, springRate * dt);
            }
            // 位置与半径同源于同一组 (radius, angle)，逐位自洽。
            b.pos = env.wrap(Vec2{n.pos.x + newR * std::cos(b.angle),
                                  n.pos.y + newR * std::sin(b.angle)});
            b.radius = env.torusDistance(n.pos, b.pos);
        } else {
            // 球远失控：脱离指令漂移，只保留上一步角速度（惯性滑行）。
            b.angle = std::fmod(b.angle + b.lastOmega * dt + kTwoPi, kTwoPi);
            // 失控球位置由自由速度积分推进（世界坐标是第一性状态）。
            b.pos = env.wrap(Vec2{b.pos.x + b.vel.x * dt, b.pos.y + b.vel.y * dt});
            // 半径由**更新后的真实位置**反算，保证与几何一致。
            b.radius = env.torusDistance(n.pos, b.pos);
            // S19 自然脱离判据：失控且球能超过 detachEnergy 才脱离（低能球不乱飞）。
            if (b.energy > bp.detachEnergy) toDetach.push_back(bi);
        }
        // 漂移中的球缓慢散热：远程失控代表能量泄漏，按小系数耗散（走账本守恒）。
        // R4-P2：原系数 0.0001 形同虚设（20k tick 累计仅 0.107），改为 0.05 使
        // 漂移球在一个 handoff 量级的时间尺度上可感知地散热。
        const double drift = bp.locomotionCost * b.radius * 0.05 * dt;
        if (drift > 0.0 && b.energy > 0.0) {
            const double take = drift < b.energy ? drift : b.energy;
            b.energy -= take;
            ledger.dissipate("ballDrift", take);
        }
        // 【已删除】旧代码在此处又一次按 (n.pos, radius, angle) 覆盖 b.pos。
        // 那是"球位置是 radius 的派生量"这一错误模型的残留，与上面受控/失控
        // 两条分支里对 b.pos 的处理直接冲突（同一个量每帧被写两次，后写者赢），
        // 也是 S19 判据失效与 radius/真实环距不一致的第二处来源。已移除。
    }
    // ---- 6a. S19 自然脱离结算：待脱离球移入 detached_ 交接缓冲 ----
    // 能量完全不耗散、账面零操作：能量只是从 n.balls 搬到 n.detached_（ownerId 置 0）。
    // 组合根每帧取走 n.detached_ 并清空（计入其生态总能量汇总）；取走前能量仍由
    // 该缓冲持有，任何 sumLiving 口径都应把它算上（单测均如此）。倒序摘除保持下标有效。
    for (std::size_t k = toDetach.size(); k > 0; --k) {
        detachBall(n, n.detached_, toDetach[k - 1]);
        // S19 红线：脱离**不做任何账面操作**——能量仍在球内，只是从 n.balls 搬到
        // n.detached_。审计恒等式由组合根闭合：Simulation::totalEnergy() 已把
        // n.detached_ 计入生态总能量（与 n.balls 同等对待），因此不需要任何
        // dissipate/inject 桥接。曾在此处加过 dissipate("detach") 作为"闭合桥"，
        // 那是错的：能量明明还在实体里，记耗散等于凭空扣掉一份能量，
        // 违反 S19 与零和红线。已删除。
    }
    // ---- 6b. T08 触发规则（行为层内部：能量阈值 + 少量随机，走自传 RNG）----
    // 优先级：产球 > 回收。繁殖是种群级动作（需要 std::vector<Nucleus>& 容器与
    // 独立 childId），apply 只有单核视野，无法在本签名内入列子核——由
    // AgentBehavior::reproduce 公开静态接口承担（组合根/单测按种群调用）。
    // roll 无论触发与否都消费一次 RNG，保证 RNG 流与能量状态解耦（双跑一致）。
    {
        const double roll = rng.nextDouble();
        if (roll < kActionRollP) {
            const double capRef = n.cap > 0.0 ? n.cap
                                             : (geneOr(n.genome.energyThreshold, 20.0) * 4.0);
            if (n.energy > capRef * kDeployTriggerFrac) {
                // S17 接线：侦察球是菌落对外感知的**唯一**来源（sense 的 scoutCount）。
                // 如果 deployBall 永远只产 Resource，scoutCount 就恒为 0，
                // S17 等于从未在生态里生效——必须真的产 Scout 球。
                // 规则：核当前没有 Scout 球时优先补一个 Scout（保证每个核都有眼睛），
                // 已有 Scout 则继续产 Resource（囤能量仓库）。
                bool hasScout = false;
                for (const Ball& b : n.balls) {
                    if (b.type == BallType::Scout) { hasScout = true; break; }
                }
                deployBall(n, ledger, rng, env.config().world.width, env.config().world.height,
                           hasScout ? BallType::Resource : BallType::Scout);
            } else if (n.energy < capRef * kRecycleEnergyFrac && !n.balls.empty()) {
                recycleBall(n, ledger);
            }
        }
    }
    // ---- 6c. S18 容量结算：菌落容量 = 原生上限 + Resource 球仓库之和 ----
    // n.cap 每帧重算（球可出生/销毁/换类型，快照不可信）；超容部分全额耗散
    // （reason="capOverflow"），账实相等：核侧扣多少，账面耗散多少，零和恒等式不破。
    // 排在 T08 触发之后、死亡判定之前：本帧溢出的能量不留到下一帧，死核不结算容量。
    n.cap = colonyCap(n, bp);
    if (n.energy > n.cap) {
        const double excess = n.energy - n.cap;
        if (excess > 0.0) {
            n.energy -= excess;
            ledger.dissipate("capOverflow", excess);
        }
    }
    // ---- 7. 能量下限与死亡（R2-A §8：尸体营养循环）----
    if (n.energy <= 0.0) {
        // 死亡：核能量归零。负残差（数值误差）不得凭空入账——
        // 账面只对真实存在的正余额做耗散，负值部分本来就是账外数值噪声。
        // R2-A：核体能量 70% 当场耗散（热力学损失），30% 打包成 Carrion（位置+能量）
        // 由组合根分配给最近的蓝细菌节点（尸体 → 分解者 → 生产者）。球储能
        // 仍全额当场耗散（球是易散结构，不留尸体）。
        const double left = n.energy > 0.0 ? n.energy : 0.0;
        n.energy = 0.0;
        n.alive = false;
        if (left > 0.0) {
            const double carrionShare = left * kDeathCarrionFraction;
            const double lost = left - carrionShare;
            if (lost > 0.0) ledger.dissipate("death", lost);
            if (carrionShare > 0.0) {
                n.carrion_.pos = n.pos;          // 死亡位置
                n.carrion_.energy = carrionShare;
            }
        }
        // 球随核死亡一并回收：球储能按剩余全额耗散，账面守恒。
        for (Ball& b : n.balls) {
            if (b.energy > 0.0) {
                ledger.dissipate("death", b.energy);
                b.energy = 0.0;
            }
        }
        n.balls.clear();
        n.speed = 0.0;
        n.vel = Vec2{0.0, 0.0};
    }
}

// ---- S21：核对球施加加速度（能量代价的定向推力）----
// 语义：核把自己储存的能量转化为对球的推力（vel 增量），代价按 magnitude 平方计。
// 这是"核推动自由球"的机制：对任何球（含 ownerId==0）都可施力。
// torusDist 由调用方传入（调用方持有 environment；本层不依赖 env 几何）。
bool AgentBehavior::applyForce(Nucleus& n, Ball& b, Vec2 dir, double magnitude,
                               double torusDist, AgentLedger& ledger,
                               const BehaviorParams& bp) {
    if (!n.alive) return false;                       // 死核对世界无作用
    // R2-A：施力半径基因化（genome.forceRange，Init 30~150；旧固定 40 退役为 fallback）。
    const double range = geneOr(n.genome.forceRange, bp.forceRadius);
    if (!(torusDist >= 0.0) || torusDist > range) return false; // NaN/超距拒绝
    if (!(magnitude > 0.0)) return false;             // NaN/非正拒绝
    double m = magnitude;
    if (m > bp.forceMax) m = bp.forceMax;             // 单次增量钳上限（世界物理参数）
    // R2-A：施力代价基因化（genome.forceCostK，Init 0.005~0.05；旧固定 0.01 fallback）。
    const double costK = geneOr(n.genome.forceCostK, bp.forceCostK);
    double cost = costK * m * m;                      // cost = k * m^2（钳后）
    if (cost > n.energy) {
        // 余额不足：按 cost = k*m^2 反解可付得起的 m = sqrt(energy/k)，
        // 核能量永不为负。energy <= 0 直接拒绝。
        if (n.energy <= 0.0) return false;
        m = std::sqrt(n.energy / costK);
        cost = costK * m * m;                         // 用反解后的 m 重算：勿直接扣 n.energy，
                                                      // 否则减法舍入可能造成账实微差/超支
        if (!(m > 0.0)) return false;
    }
    const Vec2 d = normalized(dir);                   // 零向量安全：返回 {0,0}
    b.vel.x += d.x * m;
    b.vel.y += d.y * m;
    n.energy -= cost;
    ledger.dissipate("applyForce", cost);
    return true;
}

// ---- R2-A：点名施力（决策层点名，行为层校验+结算）----
// 【已删除】旧的"随机推球"接线（决策层没有施力意图字段时代的占位行为）：
// 施力现在是纯粹的"决策意图 -> 行为结算"管线，不再有任何系统侧随机喂力。
// 校验链（全部通过才动状态）：
//   slot >= 0 且 slot < targets.count（-1 = 本帧不施力）
//   目标球存在（id 语义由调用方保证）且环距 <= genome.forceRange
//   |意图| > 0
// 结算：
//   |a| 钳到 min(genome.maxAccel, bp.forceMax)（maxAccel 的第二生效位）
//   cost = genome.forceCostK * |a|^2；付不起按 sqrt(energy/cost) 缩 |a|（永不为负）
//   ledger.dissipate("applyForce", cost) 后 b.vel += a
bool AgentBehavior::applyNamedForce(Nucleus& n, Ball& b, const ActionVector& action,
                                    const TargetList& targets, int slot,
                                    double torusDist, AgentLedger& ledger,
                                    const BehaviorParams& bp) {
    if (!n.alive) return false;
    if (slot < 0) return false;                       // 未点名：不花能量的唯一方式
    if (slot >= kMaxForceTargets) return false;       // 槽位非法
    if (slot >= targets.count) return false;          // 感知层没给这个目标
    const ForceTarget& ft = targets.items[slot];
    if (ft.id == 0) return false;                     // 非法目标 id
    // 施力半径用基因；环距以调用方传入值为准（感知快照；组合根在施力时刻重取）。
    const double range = geneOr(n.genome.forceRange, bp.forceRadius);
    if (!(torusDist >= 0.0) || torusDist > range) return false; // NaN/超距拒绝
    // 意图矢量：forceX/forceY 未归一化，长度即期望 |a|。
    const double want = length(Vec2{action.forceX, action.forceY});
    if (!(want > 0.0)) return false;                  // 零意图 = 无效点名
    // |a| 上限：基因 maxAccel（第二生效位）与世界物理上限取小。
    const double capA = std::min(geneOr(n.genome.maxAccel, bp.moveK), bp.forceMax);
    double m = want;
    if (m > capA) m = capA;
    // 代价与支付能力（与 applyForce 同一数学，重复展开以便失败路径完全零状态）。
    const double costK = geneOr(n.genome.forceCostK, bp.forceCostK);
    double cost = costK * m * m;
    if (cost > n.energy) {
        if (n.energy <= 0.0) return false;
        m = std::sqrt(n.energy / costK);
        cost = costK * m * m;
        if (!(m > 0.0)) return false;
    }
    const Vec2 d = normalized(Vec2{action.forceX, action.forceY}); // 零向量安全
    b.vel.x += d.x * m;
    b.vel.y += d.y * m;
    n.energy -= cost;
    ledger.dissipate("applyForce", cost);
    return true;
}

// ---- R2-A：对核物理（社会参数生效位）----
// eff = base * (1 + kSocialModLimit * clamp(mod, -1, 1))，每帧重算（AMEND-1 §1）。
// 决策层 mod[4] 调制在此落地；基因本身绝不被改写（表型可塑，非拉马克）。
// 斥力：d < effSpace_i -> effRepel_i * (1 - d/effSpace_i)（沿 j->i，把 i 推离 j）。
// 聚拢：effSpace_i <= d < effSight_i 且 effCohesion_i > 0
//        -> effCohesion_i * (1 - d/effSight_i)（沿 i->j，把 i 拉向 j）。
// 无能量转移（AMEND-1：物理只做斥力+聚拢+硬分离）；不消耗 RNG；确定性（无随机项）。
// 两阶段积分：本函数只算加速度累加到 outAcc，积分统一在 apply 步骤 2。
void AgentBehavior::socialAccelerations(Nucleus& n, const AgentEnvironment& env,
                                        const std::vector<Nucleus>& population,
                                        const ActionVector& action,
                                        Vec2& outAcc) {
    // eff 重算：mod[0..3] -> sight/space/repel/cohesion，逐项钳 [-1,1]。
    double mod[4] = {action.mod[0], action.mod[1], action.mod[2], action.mod[3]};
    for (int k = 0; k < 4; ++k) {
        if (!(mod[k] > -1.0)) mod[k] = -1.0;          // NaN/越界归下界（最保守）
        if (mod[k] > 1.0) mod[k] = 1.0;
    }
    const double sight = geneOr(n.genome.nucleusSightRadius, 170.0) *
                         (1.0 + kSocialModLimit * mod[0]);
    const double space = geneOr(n.genome.nucleusPersonalSpace, 47.0) *
                         (1.0 + kSocialModLimit * mod[1]);
    const double repel = geneOr(n.genome.nucleusRepelStrength, 3.0) *
                         (1.0 + kSocialModLimit * mod[2]);
    const double cohesion = geneOr(n.genome.nucleusCohesion, 1.5) *
                            (1.0 + kSocialModLimit * mod[3]);
    n.effSight = sight;                               // 缓存本帧生效值（调试/trace 消费）
    n.effSpace = space;
    n.effRepel = repel;
    n.effCohesion = cohesion;
    if (!n.alive) return;                             // 死核不产生也不感受社会力
    // 性能（R2-A 标定，数学等价）：先算环面位移（无 sqrt），用平方距离预筛，
    // 只对真正落在斥力/聚拢区的邻居做 sqrt 与归一化——O(n²) 主循环里 sqrt 是大头
    // （300 核 × 300 邻居 × 2 次开方/tick 实测把 20k tick 拖到 8 分钟）。
    const double space2 = space * space;
    const double sight2 = sight * sight;
    for (const Nucleus& other : population) {
        if (&other == &n) continue;                   // 自排斥为零
        if (!other.alive) continue;                   // 尸体不施力
        // 性能：用 torusDeltaWrapped（无 fmod）替代 env.torusDelta。
        // 前提：n.pos / other.pos 已在 [0,w) 内——Simulation::step 每帧开头
        // 所有核位置都经 env.wrap() 归一（既有约定，见 step 的气泡/移动段）。
        // 结果与 torusDelta 逐位相同，但省掉每对 2 次 fmod 库调用：
        // 300 核 = 9 万对/帧，fmod 是这里的主要开销（实测 4000 tick 2s→23s）。
        const Vec2 dv = torusDeltaWrapped(n.pos, other.pos,
                                          env.config().world.width,
                                          env.config().world.height);
        const double d2 = dv.x * dv.x + dv.y * dv.y;
        if (!(d2 >= 0.0)) continue;                   // NaN 防御
        if (d2 < space2) {
            // 斥力：越近越强，恰好接触时衰减到 0（线性弹簧反向）。
            const double d = std::sqrt(d2);
            const double strength = repel * (1.0 - d / space);
            // 方向 j->i（推开）。
            const Vec2 dir = normalized(Vec2{-dv.x, -dv.y});
            outAcc.x += dir.x * strength;
            outAcc.y += dir.y * strength;
        } else if (d2 < sight2 && cohesion > 0.0) {
            // 聚拢：越远越弱（在视野内线性衰减到 0）。
            const double d = std::sqrt(d2);
            const double strength = cohesion * (1.0 - d / sight);
            // 方向 i->j（拉近）。
            const Vec2 dir = normalized(dv);
            outAcc.x += dir.x * strength;
            outAcc.y += dir.y * strength;
        }
    }
}

// ---- S22 捕食接线：把蓝细菌被吃掉的那部分能量记到核账上 ----
// 组合根已经用 cyano_.graze() 从蓝细菌扣掉了 amount（那一步不改账本，因为蓝细菌
// 能量本来就在 sumLiving 里，只是换了持有者）。本函数负责唯一写点：
// 核能量 += amount，并记 transfer（零和搬运，不改注入/耗散总额）。
void AgentBehavior::absorbPrey(Nucleus& n, EntityId preyId, double amount,
                               AgentLedger& ledger) {
    if (!n.alive) return;
    if (!(amount > 0.0)) return;                 // NaN/非正值直接忽略
    n.energy += amount;                          // 唯一写点：行为层
    ledger.transfer(preyId, n.id, amount);       // 蓝细菌 → 核，零和
}


// =====================================================================
// ---- R7-B：球力场模型（v2.3.1 World.cpp updateBallDynamics / updateOwnership
//      公式照抄；任务卡 R7-B）----
// 与 v231 的坐标差异：旧版世界是矩形 bounce 边界，本项目是环面——所有位移由
// 调用方（Simulation 组合根）先取最短缠绕（torusDeltaWrapped），本层只做纯算术，
// 不做 fmod（本层不依赖 environment 几何）。
// 阻尼/限速/位置积分在调用方 ballWorldIntegrate（每球一次、每帧一次），本层只算
// 加速度贡献，多个来源的 acc 求和语义与 v231 的逐项累加等价。
// =====================================================================

// R7-B 对照注（任务卡 spec §1/§2 逐条）：
//   v231:305  acc += dir * (-affinity[t] * (dist - orbitRadius[t]) * radialK)
//   此处:     acc += dir * (-aff[t] * (dist - targetR) * p.radialK * dt)
//             targetR = 负载半径 × 基因偏置（轨道目标口径的唯一偏差，见头文件
//             ballFieldAccel 注释；默认基因 120 -> 偏置 1.0，与既有 C3b/S18/S19
//             半径语义逐位同源，且与 R6-C BallTuning 标定尺度自洽）
//   v231:307  acc += tangent * (affinity[t] * tangentialK)
//   此处:     acc += tangent * (aff[t] * p.tangentialK * dt)  // tangent=(-dir.y,dir.x) 逆时针
//   v231:309  if (dist < nearRepelRadius)
//               acc += dir * (repelStrength[t] * (1 - dist/nearRepelRadius))
//   此处:     同式（× dt），repelStrength[t] = src 基因（geneOr 守卫同 R2-A）
//   v231:320  followCoupling 弹簧（归属核，范围 detachRadius，radialK*followCoupling）
//   此处:     同式（× dt）。odisp = 宿主->球位移，调用方约定：仅当 src 就是
//             b.ownerId 指向的宿主核时才给非零向量（本层无 id->核 视野）。
Vec2 AgentBehavior::ballFieldAccel(const Nucleus& src, const Ball& b, Vec2 disp,
                                   Vec2 odisp, double dt, const BallFieldParams& p) {
    Vec2 acc{0.0, 0.0};
    const int t = static_cast<int>(b.type);
    const double affRaw = (t >= 0 && t < kBallTypeCount) ? src.genome.affinity[t]
                                                         : (1.0 / kBallTypeCount);
    const double aff = geneOr(affRaw, 1.0 / kBallTypeCount);
    // 负载半径（S18 口径）：10 + 0.5*球能。R7-B 轨道目标 = 负载 × 基因偏置。
    const double loadR = kBallRadius + 0.5 * (b.energy > 0.0 ? b.energy : 0.0);
    {
        const double orbRaw = (t >= 0 && t < kBallTypeCount)
                                  ? src.genome.orbitRadius[t] : 120.0;
        const double orb = geneOr(orbRaw, 120.0);
        const double targetR = loadR * (0.5 + orb / 240.0);
        const double dist = length(disp);
        if (dist > 1e-9) {
            const Vec2 dir = Vec2{disp.x / dist, disp.y / dist};   // 径向朝外
            const Vec2 tangent = Vec2{-dir.y, dir.x};              // 逆时针
            // 径向弹簧：偏离期望半径时拉回（v231:305）。
            acc.x += dir.x * (-aff * (dist - targetR) * p.radialK * dt);
            acc.y += dir.y * (-aff * (dist - targetR) * p.radialK * dt);
            // 切向驱动：形成环绕运动（v231:307）。
            acc.x += tangent.x * (aff * p.tangentialK * dt);
            acc.y += tangent.y * (aff * p.tangentialK * dt);
            // 近距排斥：维持分层，防止小球贴核（v231:309-313）。
            if (dist < p.nearRepelRadius) {
                const double repRaw = (t >= 0 && t < kBallTypeCount)
                                          ? src.genome.repelStrength[t] : 1.5;
                const double rep = geneOr(repRaw, 1.5);
                const double strength = rep * (1.0 - dist / p.nearRepelRadius) * dt;
                acc.x += dir.x * strength;
                acc.y += dir.y * strength;
            }
        }
    }
    // 跟随耦合：归属核的加强弹簧，作用范围直至脱附半径（超出普通力场也有效），
    // 抵消核加速时的甩球效应，让快核甩不掉自己的球群（v231:318-336）。
    if (p.followCoupling > 0.0) {
        const double odSq = odisp.x * odisp.x + odisp.y * odisp.y;
        const double det2 = p.detachRadius * p.detachRadius;
        if (odSq < det2 && odSq > 1e-12) {
            const double odist = std::sqrt(odSq);
            const Vec2 odir = Vec2{odisp.x / odist, odisp.y / odist};
            const double aff2Raw = (t >= 0 && t < kBallTypeCount)
                                       ? src.genome.affinity[t] : (1.0 / kBallTypeCount);
            const double aff2 = geneOr(aff2Raw, 1.0 / kBallTypeCount);
            const double orb2Raw = (t >= 0 && t < kBallTypeCount)
                                       ? src.genome.orbitRadius[t] : 120.0;
            const double orb2 = geneOr(orb2Raw, 120.0);
            const double targetR2 = loadR * (0.5 + orb2 / 240.0);
            // v231:332：acc += odir * (-aff * (odist - orbit) * (radialK * followCoupling))
            const double k = -aff2 * (odist - targetR2) * (p.radialK * p.followCoupling) * dt;
            acc.x += odir.x * k;
            acc.y += odir.y * k;
        }
    }
    return acc;
}

// R7-B 对照注（任务卡 spec §3，v231:338-365）：
//   v231:354  dist < ballBallRepelRadius
//               -> acc += dir * (repelK * (1 - dist/radius))          // 推开
//   v231:357  else dist < sameOwnerAttractRadius 且同 owner
//               -> acc -= dir * (attractK * (1 - dist/radius))        // 拉近
//   此处同式（× dt）。disp = o->b 的环面位移（dir 沿 o->b：正值推离 o）。
//   同宿主判定用 ownerId（0 = 自由球永不吸引——v231 的 ownerId<0 分支语义平移）。
//   距离衰减的优先级与 v231 一致：排斥半径内只排斥（else-if 结构）。
Vec2 AgentBehavior::ballPairAccel(const Ball& b, const Ball& o, Vec2 disp,
                                  double dt, const BallFieldParams& p) {
    Vec2 acc{0.0, 0.0};
    const double ar = p.sameOwnerAttractRadius;
    const double rr = p.ballRepelRadius;
    const double ar2 = ar * ar;
    const double rr2 = rr * rr;
    const double distSq = disp.x * disp.x + disp.y * disp.y;
    if (distSq >= (ar2 > rr2 ? ar2 : rr2)) return acc;   // 两半径都无份：免 sqrt
    const double dist = std::sqrt(distSq);
    if (dist < 1e-9) return acc;                         // 重合球无方向（v231:352 同语义）
    const Vec2 dir = Vec2{disp.x / dist, disp.y / dist};
    if (dist < rr) {
        const double strength = p.ballRepelK * (1.0 - dist / rr) * dt;
        acc.x += dir.x * strength;
        acc.y += dir.y * strength;
    } else if (b.ownerId != 0 && b.ownerId == o.ownerId && dist < ar) {
        // ownerId==0（自由球）不吸引：0==0 的相等不构成"同宿主"（EntityId 无符号，
        // 旧式 >= 0 判断恒真，这里显式排除——v231 ownerId<0 哨兵语义的平移）。
        // v231:359：acc -= dir * (attractK * (1 - dist/radius))——负号 = 拉近。
        const double strength = p.sameOwnerAttractK * (1.0 - dist / ar) * dt;
        acc.x -= dir.x * strength;
        acc.y -= dir.y * strength;
    }
    return acc;
}

// R7-B 对照注（任务卡 spec §2/§4/§6，v231:367-390 + 本卡 §6 Shield 代谢）：
//   v231:368  vel += acc
//   v231:369  vel *= ballDamping
//   v231:370-375 maxV = 类型上限（Shield/Worker/Scout 三档）
//   v231:377-383 跟随加速：归属球 maxV = max(类型上限, 宿主速度 × followBoost)
//   v231:384-387 限速钳制；v231:388 pos += vel（本项目 × dt）；v231:389 bounce —— 本项目
//   恒 wrap（任务卡指定），wrap 由调用方用环境层 wrap 完成（本层不依赖 env）。
//   本卡 §6：Shield 宿主核代谢降低 respirationBase × 1/(1+0.35*shieldCount)
//   （V2-SPEC B 段——旧版没有这条，是本项目规格的补全）。
double AgentBehavior::ballWorldIntegrate(Ball& b, Vec2 acc, double dt,
                                         Nucleus* owner, double worldW, double worldH,
                                         const BallFieldParams& p) {
    // 积分 + 阻尼（v231:368-369，乘 dt 同式）。
    b.vel.x += acc.x * dt;
    b.vel.y += acc.y * dt;
    b.vel.x *= p.damping;
    b.vel.y *= p.damping;
    // 类型限速三档（v231:370-375）。
    double maxV = p.maxSpeedResource;
    switch (b.type) {
        case BallType::Shield:   maxV = p.maxSpeedShield;   break;
        case BallType::Resource: maxV = p.maxSpeedResource; break;
        case BallType::Scout:    maxV = p.maxSpeedScout;    break;
    }
    // 跟随加速：归属球限速 = max(类型上限, 宿主当前速度 × followBoost)（v231:376-383）。
    if (p.followBoost > 0.0 && owner != nullptr && owner->alive) {
        const double followV = owner->speed * p.followBoost;
        if (followV > maxV) maxV = followV;
    }
    const double spSq = b.vel.x * b.vel.x + b.vel.y * b.vel.y;
    if (spSq > maxV * maxV) {
        const double k = maxV / std::sqrt(spSq);
        b.vel.x *= k;
        b.vel.y *= k;
    }
    // 位置积分 + 环面 wrap（v231:388-389 的 wrap 版；bounce_restitution/wrap_boundary
    // 不消费——环面世界边界恒 wrap，任务卡指定）。
    b.pos = wrap(Vec2{b.pos.x + b.vel.x * dt, b.pos.y + b.vel.y * dt}, worldW, worldH);
    return maxV; // 观测：本帧生效限速（trace/调试用；配合 spSq 判是否触顶）
}

// R7-B：换主直接交接。能量原样、账面零操作（零和搬运，同 detachBall 语义）；
// 不经 detached_ 缓冲——新宿主当帧立即持有，防交接缓冲被自由球结算误收。
bool AgentBehavior::transferBall(Nucleus& from, std::size_t idx, Nucleus& to) {
    if (idx >= from.balls.size()) return false;
    Ball b = from.balls[idx];               // 值拷贝：能量逐位不变
    b.ownerId = to.id;
    b.r7Lane = true;                        // R7-B 换主球进入世界积分车道
    from.balls.erase(from.balls.begin() + static_cast<std::ptrdiff_t>(idx));
    to.balls.push_back(std::move(b));
    return true;
}

// R7-B：归属结算单步（v2.3.1 updateOwnership:393-427 语义，组合根确定性扫描）。
// 返回 true = n.balls[idx] 已被摘除（换主 -> 直接挂到新宿主 balls；失主 -> 调用方
// freeBalls），调用方必须原地下标重查（swap-erase）；返回 false = 宿主不变（零状态）。
bool AgentBehavior::reassignBall(Nucleus& n, std::size_t idx, std::size_t best,
                                 std::vector<Nucleus>& population,
                                 std::vector<Ball>& freeBalls, AgentLedger& ledger,
                                 const BallFieldParams& p) {
    if (idx >= n.balls.size()) return false;
    if (best < population.size() && population[best].id == n.id) return false; // 宿主不变
    // R7-B 容量红线：kMaxBalls=3 是宿主承载上限（T08/S20 同一硬顶）。换主目标
    // 已满时不交接也不惩罚——球留在原宿主名下（下一帧归属扫描重试），零状态。
    if (best < population.size() &&
        static_cast<int>(population[best].balls.size()) >= kMaxBalls) {
        return false;
    }
    const Ball snapshot = n.balls[idx];     // 摘除前快照（防引用失效）
    Nucleus* prevOwnerPtr = nullptr;        // 原主存活判定与扣款用（摘除前抓取）
    for (Nucleus& cand : population) {
        if (cand.id == n.id) { prevOwnerPtr = &cand; break; }
    }
    const bool prevAlive = prevOwnerPtr != nullptr && prevOwnerPtr->alive;
    // 任意摘除前先把球从 n.balls 拿走（所有权交接的硬前提），再结算惩罚与去向。
    Ball b = snapshot;
    n.balls.erase(n.balls.begin() + static_cast<std::ptrdiff_t>(idx));
    // 失球惩罚（v231:417-422：上一帧有主、本帧换主/变自由，对失去方扣能量）。
    // 限额支出：原主能量永不为负（本项目账实相等的数值前提，余额不足按余额扣）。
    if (p.lossCost > 0.0 && prevAlive) {
        const double take = p.lossCost < prevOwnerPtr->energy ? p.lossCost
                                                              : prevOwnerPtr->energy;
        if (take > 0.0) {
            Nucleus& prev = *prevOwnerPtr;
            prev.energy -= take;
            ledger.dissipate("ball_loss", take);
        }
    }
    if (best < population.size()) {
        // 换主：直接交接（不经 detached_/自由池，新宿主当帧立即持有）。
        Ball moved = b;
        moved.ownerId = population[best].id;
        moved.r7Lane = true;                // R7-B 换主球进入世界积分车道
        population[best].balls.push_back(std::move(moved));
    } else {
        // 变自由：能量原样，ownerId 置 0（detachBall 同语义，就地入池）。
        b.ownerId = 0;
        b.r7Lane = true;
        freeBalls.push_back(std::move(b));
    }
    return true;
}

} // namespace agentsim


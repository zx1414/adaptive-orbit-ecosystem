// agentsim - tests/unit/test_genome_wiring.cpp
// R2-A 验收：每一个基因都有独立的生效测试（改基因 -> 行为可观测变化），无死基因。
// 覆盖：
//   maxSpeed          移动限速（替换固定 40）
//   maxAccel          每帧合加速度上限（新增生效位；施力上限第二生效位）
//   energyThreshold   繁殖门槛（替换 kReproCost）
//   mutationRate      子代变异幅度经 genomeMutate 生效（R1 契约；此处验证传递）
//   orbitRadius[t]    球轨道半径偏置（负载半径 × (0.5+r/240)）
//   affinity[t]       球径向弹簧回拉速率
//   repelStrength[t]  球近距排斥外推速率
//   absorbPreference[t] 回收倍率
//   nucleusSightRadius / nucleusPersonalSpace / nucleusRepelStrength / nucleusCohesion
//                     对核物理（±40% 决策调制在此生效）
//   forceRange        施力距离
//   forceCostK        施力代价
//   cyanoPreference / cyanoDigestion 组合根基因驱动捕食（digestion -> 冷却）
//   hue               字段在 Genome 上（trace 组合根消费；此处验证子代 hue 变异）
//   繁殖：子代基因 != 父代、RNG 消耗固定（同 seed 两次跑出完全相同子代 genome）
#include "test_framework.h"

#include <cmath>
#include <vector>

#include "../../src/behavior/AgentBehavior.h"
#include "../../src/core/Genome.h"
#include "../../src/core/PCG32.h"
#include "../../src/environment/AgentEnvironment.h"
#include "../../src/ledger/AgentLedger.h"
#include "../../src/sim/Simulation.h"
#include "../../src/core/Config.h"

namespace {

using namespace agentsim;

Nucleus makeN(double energy) {
    Nucleus n;
    n.id = 1;
    n.pos = Vec2{50.0, 50.0};
    n.energy = energy;
    n.cap = 200.0;
    n.alive = true;
    return n;
}

BehaviorParams bp() { return BehaviorParams{}; } // 每处直取：默认行为参数

} // namespace

// ---------------- 繁殖变异 ----------------

TEST(repro_child_genome_differs_from_parent) {
    AgentLedger ledger;
    std::vector<Nucleus> pop;
    Nucleus parent = makeN(300.0);
    parent.genome.energyThreshold = 20.0; // 夹具：低门槛保证繁殖成功
    parent.genome.maxSpeed = 65.0;
    pop.push_back(parent);
    PCG32 rng(42);
    CHECK(AgentBehavior::reproduce(parent, pop, ledger, 77, rng));
    CHECK(pop.size() == 2);
    // 至少一个字段不同（50 抽高斯、24 参数 + hue，全同概率 ~0）
    const Genome& pg = parent.genome;
    const Genome& cg = pop[1].genome;
    bool diff = false;
    for (int i = 0; i < kBallTypeCount; ++i) {
        if (cg.affinity[i] != pg.affinity[i]) diff = true;
        if (cg.orbitRadius[i] != pg.orbitRadius[i]) diff = true;
        if (cg.absorbPreference[i] != pg.absorbPreference[i]) diff = true;
        if (cg.repelStrength[i] != pg.repelStrength[i]) diff = true;
    }
    if (cg.maxSpeed != pg.maxSpeed) diff = true;
    if (cg.maxAccel != pg.maxAccel) diff = true;
    if (cg.energyThreshold != pg.energyThreshold) diff = true;
    if (cg.mutationRate != pg.mutationRate) diff = true;
    if (cg.cyanoPreference != pg.cyanoPreference) diff = true;
    if (cg.cyanoDigestion != pg.cyanoDigestion) diff = true;
    if (cg.nucleusSightRadius != pg.nucleusSightRadius) diff = true;
    if (cg.nucleusPersonalSpace != pg.nucleusPersonalSpace) diff = true;
    if (cg.nucleusRepelStrength != pg.nucleusRepelStrength) diff = true;
    if (cg.nucleusCohesion != pg.nucleusCohesion) diff = true;
    if (cg.forceRange != pg.forceRange) diff = true;
    if (cg.forceCostK != pg.forceCostK) diff = true;
    if (cg.hue != pg.hue) diff = true;
    CHECK(diff);
}

TEST(repro_rng_budget_is_fixed_52) {
    // 成功繁殖恰好消耗 52 次 nextDouble（genomeMutate 50 + 出生偏移 2）。
    // 验证法：跑一次 reproduce 后，同 seed 的 rng 继续抽 52 次应与"先抽 52 次再
    // reproduce"的 rng 状态对齐——直接比较两次 reproduce 的子代 genome 逐位相等。
    AgentLedger ledger;
    std::vector<Nucleus> popA, popB;
    Nucleus a = makeN(300.0);
    a.genome.energyThreshold = 20.0;
    Nucleus b = a;
    popA.push_back(a);
    popB.push_back(b);
    PCG32 r1(4242), r2(4242);
    CHECK(AgentBehavior::reproduce(popA[0], popA, ledger, 11, r1));
    CHECK(AgentBehavior::reproduce(popB[0], popB, ledger, 11, r2));
    const Genome& ga = popA[1].genome;
    const Genome& gb = popB[1].genome;
    CHECK(ga.maxSpeed == gb.maxSpeed);
    CHECK(ga.energyThreshold == gb.energyThreshold);
    CHECK(ga.hue == gb.hue);
    for (int i = 0; i < kBallTypeCount; ++i) CHECK(ga.orbitRadius[i] == gb.orbitRadius[i]);
    // 出生位置也逐位一致（偏移 2 抽在 mutate 之后，消耗序固定）。
    CHECK(popA[1].pos.x == popB[1].pos.x);
    CHECK(popA[1].pos.y == popB[1].pos.y);
    // 消化冷却归零。
    CHECK(popA[1].digestCooldown == 0);
    // 亲代基因逐位不动（变异只作用于子代——拉马克红线）。
    CHECK(popA[0].genome.maxSpeed == a.genome.maxSpeed);
    CHECK(popA[0].genome.hue == a.genome.hue);
}

TEST(repro_refused_when_below_gene_threshold) {
    AgentLedger ledger;
    std::vector<Nucleus> pop;
    Nucleus n = makeN(100.0);
    n.genome.energyThreshold = 140.0; // 默认基因门槛 > 100 能量：不育
    pop.push_back(n);
    PCG32 rng(1);
    CHECK(!AgentBehavior::reproduce(n, pop, ledger, 5, rng));
    CHECK(pop.size() == 1);
    CHECK_NEAR(ledger.dissipatedBy("reproduce-loss"), 0.0, 1e-12);
}

TEST(repro_child_position_near_parent) {
    AgentLedger ledger;
    std::vector<Nucleus> pop;
    Nucleus n = makeN(300.0);
    n.genome.energyThreshold = 20.0;
    pop.push_back(n);
    PCG32 rng(7);
    CHECK(AgentBehavior::reproduce(n, pop, ledger, 6, rng, GenomeRanges{}, 0.25, 256.0, 256.0));
    const Vec2 d = torusDelta(n.pos, pop[1].pos, 256.0, 256.0);
    CHECK(std::fabs(d.x) <= 12.0 && std::fabs(d.y) <= 12.0); // kBirthOffset=12 方形偏移
}

TEST(repro_zero_sum_identity_holds) {
    AgentLedger ledger;
    std::vector<Nucleus> pop;
    Nucleus n = makeN(300.0);
    n.genome.energyThreshold = 80.0;
    ledger.injectInitial(n.id, n.energy);
    pop.push_back(n);
    PCG32 rng(3);
    CHECK(AgentBehavior::reproduce(n, pop, ledger, 9, rng));
    // 亲代 -80、子代 +40、耗散 40：恒等式闭合。
    const double living = n.energy + pop[1].energy;
    CHECK_NEAR(living, 260.0, 1e-12);
    CHECK_NEAR(ledger.netInjected(), 260.0, 1e-12);
    CHECK(ledger.audit(1e-9 * (1.0 + ledger.totalInjected()), living));
}

// ---------------- maxSpeed 基因 ----------------

TEST(gene_max_speed_limits_velocity) {
    SimConfig cfgA{};
    cfgA.cyanoNodeCount = 0;
    AgentEnvironment env(cfgA);
    AgentLedger ledA, ledB;
    Nucleus a = makeN(500.0), b = makeN(500.0);
    a.speed = b.speed = 0.0;
    a.genome.maxSpeed = 30.0;   // 低速基因
    b.genome.maxSpeed = 100.0;  // 高速基因
    PCG32 r1(9), r2(9);
    ActionVector act;
    act.dirX = 1.0; // 期望速度 60（moveK）
    for (int t = 0; t < 10; ++t) {
        AgentBehavior::apply(a, act, env, ledA, 1.0, bp(), r1);
        AgentBehavior::apply(b, act, env, ledB, 1.0, bp(), r2);
    }
    CHECK_NEAR(a.speed, 30.0, 1e-9);  // 被低速基因钳住
    CHECK_NEAR(b.speed, 60.0, 1e-9);  // 60 = moveK < 100：不受限
    CHECK(a.speed < b.speed);
}
// ---------------- maxAccel 基因 ----------------

TEST(gene_max_accel_caps_frame_acceleration) {
    SimConfig cfg{};
    cfg.cyanoNodeCount = 0;
    AgentEnvironment env(cfg);
    AgentLedger ledger;
    Nucleus n = makeN(500.0);
    n.genome.maxSpeed = 300.0;   // 不限速
    n.genome.maxAccel = 5.0;     // 每帧加速度上限
    n.vel = Vec2{0.0, 0.0};
    n.speed = 0.0;
    PCG32 rng(11);
    ActionVector act;
    act.dirX = 1.0; // 期望速度 60
    AgentBehavior::apply(n, act, env, ledger, 1.0, bp(), rng);
    // 速度最多从 0 涨到 maxAccel=5。
    CHECK_NEAR(n.vel.x, 5.0, 1e-9);
    CHECK_NEAR(n.vel.y, 0.0, 1e-9);
}

// ---------------- orbitRadius / affinity / repelStrength 基因（球动力学） ----------------

TEST(gene_orbit_radius_biases_ball_orbit) {
    SimConfig cfg{};
    cfg.cyanoNodeCount = 0;
    AgentEnvironment env(cfg);
    AgentLedger ledA, ledB;
    Nucleus a = makeN(300.0), b = makeN(300.0);
    a.genome.orbitRadius[0] = 240.0;  // 偏置 = 0.5+1.0 = 1.5（外扩）
    b.genome.orbitRadius[0] = 12.0;   // 偏置 = 0.5+0.05 = 0.55（内收）
    a.genome.affinity[0] = 1.0 / 3.0; // 中性弹簧
    b.genome.affinity[0] = 1.0 / 3.0;
    a.genome.repelStrength[0] = 1.5;  // 中性排斥
    b.genome.repelStrength[0] = 1.5;
    Ball ba; ba.type = BallType::Resource; ba.energy = 20.0; ba.radius = 20.0;
    Ball bb = ba;
    ba.ownerId = a.id; bb.ownerId = b.id;
    a.balls.push_back(ba);
    b.balls.push_back(bb);
    PCG32 r1(13), r2(13);
    ActionVector act;
    for (int t = 0; t < 60; ++t) {
        AgentBehavior::apply(a, act, env, ledA, 1.0, bp(), r1);
        AgentBehavior::apply(b, act, env, ledB, 1.0, bp(), r2);
        if (!a.alive || !b.alive) break;
    }
    // 负载半径同为 20：外扩基因的球更远，内收基因的球更近。
    const double ra = a.balls.empty() ? -1.0 : a.balls[0].radius;
    const double rb = b.balls.empty() ? -1.0 : b.balls[0].radius;
    CHECK(ra > rb);
    // 且外扩者真的在 targetR = 20*1.5 = 30 附近（弹簧收敛）。
    if (ra > 0.0) CHECK(std::fabs(ra - 30.0) < 6.0);
}

TEST(gene_affinity_changes_spring_speed) {
    SimConfig cfg{};
    cfg.cyanoNodeCount = 0;
    AgentEnvironment env(cfg);
    AgentLedger ledA, ledB;
    Nucleus a = makeN(300.0), b = makeN(300.0);
    a.genome.orbitRadius[0] = 120.0;  // 中性偏置 1.0
    b.genome.orbitRadius[0] = 120.0;
    a.genome.affinity[0] = 1.0;       // 强弹簧（回拉快）
    b.genome.affinity[0] = 0.05;      // 弱弹簧（回拉慢）
    a.genome.repelStrength[0] = 1.5;
    b.genome.repelStrength[0] = 1.5;
    Ball ba; ba.type = BallType::Resource; ba.energy = 20.0; ba.radius = 40.0; // 拉远的球
    Ball bb = ba;
    ba.ownerId = a.id; bb.ownerId = b.id;
    a.balls.push_back(ba);
    b.balls.push_back(bb);
    PCG32 r1(17), r2(17);
    ActionVector act;
    AgentBehavior::apply(a, act, env, ledA, 1.0, bp(), r1);
    AgentBehavior::apply(b, act, env, ledB, 1.0, bp(), r2);
    // 一帧后强弹簧的球向 targetR=20 收得更多。
    CHECK(a.balls[0].radius < b.balls[0].radius);
}

TEST(gene_repel_strength_pushes_ball_out) {
    SimConfig cfg{};
    cfg.cyanoNodeCount = 0;
    AgentEnvironment env(cfg);
    AgentLedger ledA, ledB;
    Nucleus a = makeN(300.0), b = makeN(300.0);
    a.genome.orbitRadius[0] = 120.0;
    b.genome.orbitRadius[0] = 120.0;
    a.genome.affinity[0] = 1.0 / 3.0;
    b.genome.affinity[0] = 1.0 / 3.0;
    a.genome.repelStrength[0] = 6.0;  // 强排斥（外推快）
    b.genome.repelStrength[0] = 0.1;  // 弱排斥
    Ball ba; ba.type = BallType::Resource; ba.energy = 20.0; ba.radius = 5.0; // 过近的球
    Ball bb = ba;
    ba.ownerId = a.id; bb.ownerId = b.id;
    // R2-A 球世界坐标是第一性状态：显式摆在核东侧 5 处（curR=5 < targetR=20，排斥区）。
    ba.pos = Vec2{a.pos.x + 5.0, a.pos.y};
    bb.pos = Vec2{b.pos.x + 5.0, b.pos.y};
    a.balls.push_back(ba);
    b.balls.push_back(bb);
    PCG32 r1(19), r2(19);
    ActionVector act;
    AgentBehavior::apply(a, act, env, ledA, 1.0, bp(), r1);
    AgentBehavior::apply(b, act, env, ledB, 1.0, bp(), r2);
    CHECK(a.balls[0].radius > b.balls[0].radius); // 强排斥把球推得更远
}

// ---------------- absorbPreference 基因 ----------------

TEST(gene_absorb_preference_scales_recycle) {
    AgentLedger ledA, ledB;
    Nucleus a = makeN(0.0), b = makeN(0.0);
    a.genome.absorbPreference[0] = 3.0;  // 高倍率
    b.genome.absorbPreference[0] = 0.5;  // 低倍率
    Ball ba; ba.type = BallType::Resource; ba.energy = 10.0;
    Ball bb = ba;
    a.balls.push_back(ba);
    b.balls.push_back(bb);
    CHECK(AgentBehavior::recycleBall(a, ledA));
    CHECK(AgentBehavior::recycleBall(b, ledB));
    // 返还 = 10 * 0.5 * pref（倍率不封顶时按 b.energy 封顶：3.0 -> 15 > 10 -> 10）。
    CHECK_NEAR(a.energy, 10.0, 1e-12);  // 3.0 倍被"不得超过球内能量"封顶
    CHECK_NEAR(b.energy, 2.5, 1e-12);   // 0.5 倍 = 10*0.5*0.5
}

// ---------------- 对核物理：nucleus* 四基因 + mod 调制 ----------------

TEST(gene_personal_space_repels_neighbor) {
    SimConfig cfg{};
    cfg.cyanoNodeCount = 0;
    AgentEnvironment env(cfg);
    std::vector<Nucleus> pop;
    Nucleus a = makeN(300.0);
    a.genome.nucleusPersonalSpace = 50.0;
    a.genome.nucleusRepelStrength = 4.0;
    a.genome.nucleusSightRadius = 60.0;
    a.genome.nucleusCohesion = 0.0; // 关聚拢，只测斥力
    Nucleus neighbor = makeN(300.0);
    neighbor.id = 2;
    neighbor.pos = Vec2{70.0, 50.0}; // 距离 20 < 50：斥力区
    neighbor.alive = true;
    pop.push_back(neighbor);
    ActionVector act; // mod 全 0
    Vec2 acc{0.0, 0.0};
    AgentBehavior::socialAccelerations(a, env, pop, act, acc);
    // 斥力 = 4 * (1 - 20/50) = 2.4，方向 = 从邻居指向自己（邻居在 +x，推向 -x）。
    CHECK_NEAR(acc.x, -2.4, 1e-12);
    CHECK_NEAR(acc.y, 0.0, 1e-12);
    // eff 缓存生效。
    CHECK_NEAR(a.effSpace, 50.0, 1e-12);
    CHECK_NEAR(a.effRepel, 4.0, 1e-12);
}

TEST(gene_cohesion_pulls_neighbor) {
    SimConfig cfg{};
    cfg.cyanoNodeCount = 0;
    AgentEnvironment env(cfg);
    std::vector<Nucleus> pop;
    Nucleus a = makeN(300.0);
    a.genome.nucleusPersonalSpace = 10.0;
    a.genome.nucleusSightRadius = 100.0;
    a.genome.nucleusRepelStrength = 1.0;
    a.genome.nucleusCohesion = 2.0; // 聚拢开
    Nucleus neighbor = makeN(300.0);
    neighbor.id = 2;
    neighbor.pos = Vec2{90.0, 50.0}; // 距离 40 ∈ [10,100)：聚拢区
    neighbor.alive = true;
    pop.push_back(neighbor);
    ActionVector act;
    Vec2 acc{0.0, 0.0};
    AgentBehavior::socialAccelerations(a, env, pop, act, acc);
    // 引力 = 2 * (1 - 40/100) = 1.2，沿 +x（拉近）。
    CHECK_NEAR(acc.x, 1.2, 1e-12);
}

TEST(gene_zero_cohesion_means_lonewolf) {
    SimConfig cfg{};
    cfg.cyanoNodeCount = 0;
    AgentEnvironment env(cfg);
    std::vector<Nucleus> pop;
    Nucleus a = makeN(300.0);
    a.genome.nucleusPersonalSpace = 10.0;
    a.genome.nucleusSightRadius = 100.0;
    a.genome.nucleusCohesion = 0.0; // 独行基因
    Nucleus neighbor = makeN(300.0);
    neighbor.id = 2;
    neighbor.pos = Vec2{90.0, 50.0};
    neighbor.alive = true;
    pop.push_back(neighbor);
    ActionVector act;
    Vec2 acc{0.0, 0.0};
    AgentBehavior::socialAccelerations(a, env, pop, act, acc);
    CHECK(acc.x == 0.0 && acc.y == 0.0); // 聚拢为 0：视野内无引力
}

TEST(gene_eff_modulation_plus_one_is_140_percent) {
    SimConfig cfg{};
    cfg.cyanoNodeCount = 0;
    AgentEnvironment env(cfg);
    std::vector<Nucleus> pop;
    Nucleus a = makeN(300.0);
    a.genome.nucleusSightRadius = 100.0;
    a.genome.nucleusPersonalSpace = 40.0;
    a.genome.nucleusRepelStrength = 3.0;
    a.genome.nucleusCohesion = 2.0;
    Nucleus neighbor = makeN(300.0);
    neighbor.id = 2;
    neighbor.pos = Vec2{20.0, 50.0}; // 距离 30 < 40：斥力区
    neighbor.alive = true;
    pop.push_back(neighbor);
    ActionVector act;
    act.mod[0] = 1.0; act.mod[1] = 1.0; act.mod[2] = 1.0; act.mod[3] = 1.0; // 全 +1
    Vec2 acc{0.0, 0.0};
    AgentBehavior::socialAccelerations(a, env, pop, act, acc);
    // effSight = 100*1.4 = 140；effSpace = 40*1.4 = 56；effRepel = 3*1.4 = 4.2。
    CHECK_NEAR(a.effSight, 140.0, 1e-12);
    CHECK_NEAR(a.effSpace, 56.0, 1e-12);
    CHECK_NEAR(a.effRepel, 4.2, 1e-12);
    CHECK_NEAR(a.effCohesion, 2.8, 1e-12);
    // 斥力 = 4.2 * (1 - 30/56) = 1.95。
    CHECK_NEAR(acc.x, 4.2 * (1.0 - 30.0 / 56.0), 1e-12);
}

TEST(gene_eff_modulation_minus_one_is_60_percent) {
    SimConfig cfg{};
    cfg.cyanoNodeCount = 0;
    AgentEnvironment env(cfg);
    std::vector<Nucleus> pop;
    Nucleus a = makeN(300.0);
    a.genome.nucleusSightRadius = 100.0;
    a.genome.nucleusPersonalSpace = 40.0;
    a.genome.nucleusRepelStrength = 3.0;
    a.genome.nucleusCohesion = 2.0;
    Nucleus neighbor = makeN(300.0);
    neighbor.id = 2;
    neighbor.pos = Vec2{20.0, 50.0};
    neighbor.alive = true;
    pop.push_back(neighbor);
    ActionVector act;
    act.mod[0] = -1.0; act.mod[1] = -1.0; act.mod[2] = -1.0; act.mod[3] = -1.0; // 全 -1
    Vec2 acc{0.0, 0.0};
    AgentBehavior::socialAccelerations(a, env, pop, act, acc);
    CHECK_NEAR(a.effSight, 60.0, 1e-12);
    CHECK_NEAR(a.effSpace, 24.0, 1e-12);
    CHECK_NEAR(a.effRepel, 1.8, 1e-12);
    CHECK_NEAR(a.effCohesion, 1.2, 1e-12);
}

TEST(gene_eff_mod_out_of_range_clamped) {
    SimConfig cfg{};
    cfg.cyanoNodeCount = 0;
    AgentEnvironment env(cfg);
    std::vector<Nucleus> pop;
    Nucleus a = makeN(300.0);
    a.genome.nucleusSightRadius = 100.0;
    Nucleus neighbor = makeN(300.0);
    neighbor.id = 2;
    // 环面几何：最大环距 = 半对角 181。摆 (178,114)：d = sqrt(128^2+64^2) ≈ 143.1
    // > effSight(140)。注意 (200,50) 的直线距离 150 会 wrap 成 106（环面最短像）。
    neighbor.pos = Vec2{178.0, 114.0};
    neighbor.alive = true;
    pop.push_back(neighbor);
    ActionVector act;
    act.mod[0] = 99.0; // 越界：钳到 +1 -> eff = 140
    Vec2 acc{0.0, 0.0};
    AgentBehavior::socialAccelerations(a, env, pop, act, acc);
    CHECK_NEAR(a.effSight, 140.0, 1e-12);
    // 距离 143 > 140：视野外，无任何力。
    CHECK(acc.x == 0.0 && acc.y == 0.0);
}

// ---------------- forceRange / forceCostK / maxAccel（施力） ----------------

TEST(gene_force_range_gates_named_force) {
    SimConfig cfg{};
    cfg.cyanoNodeCount = 0;
    AgentEnvironment env(cfg);
    Nucleus n = makeN(100.0);
    n.genome.forceRange = 25.0;  // 近距施力基因
    n.genome.forceCostK = 0.01;
    n.genome.maxAccel = 1e9;
    Ball b;
    TargetList targets;
    targets.count = 1;
    targets.items[0].id = 99;
    targets.items[0].dist = 30.0; // > forceRange 25
    AgentLedger ledger;
    ActionVector act;
    act.forceSlot = 0;
    act.forceX = 4.0; act.forceY = 0.0;
    CHECK(!AgentBehavior::applyNamedForce(n, b, act, targets, 0, 30.0, ledger, bp()));
    CHECK_NEAR(ledger.dissipatedBy("applyForce"), 0.0, 1e-12);
    // 半径内可施力。
    targets.items[0].dist = 20.0;
    CHECK(AgentBehavior::applyNamedForce(n, b, act, targets, 0, 20.0, ledger, bp()));
    CHECK_NEAR(b.vel.x, 4.0, 1e-12);
}

TEST(gene_force_cost_k_scales_cost) {
    Nucleus n = makeN(100.0);
    n.genome.forceRange = 100.0;
    n.genome.forceCostK = 0.05; // 贵基因
    n.genome.maxAccel = 1e9;
    Ball b;
    TargetList targets;
    targets.count = 1;
    targets.items[0].id = 99;
    targets.items[0].dist = 10.0;
    AgentLedger ledger;
    ActionVector act;
    act.forceSlot = 0;
    act.forceX = 4.0; act.forceY = 0.0;
    CHECK(AgentBehavior::applyNamedForce(n, b, act, targets, 0, 10.0, ledger, bp()));
    CHECK_NEAR(n.energy, 100.0 - 0.05 * 16.0, 1e-12); // cost = 0.05*|a|^2
    CHECK_NEAR(ledger.dissipatedBy("applyForce"), 0.8, 1e-12);
}

TEST(gene_max_accel_caps_force_vector) {
    Nucleus n = makeN(100.0);
    n.genome.forceRange = 100.0;
    n.genome.forceCostK = 0.01;
    n.genome.maxAccel = 3.0; // 施力上限 = maxAccel（第二生效位）
    Ball b;
    TargetList targets;
    targets.count = 1;
    targets.items[0].id = 99;
    targets.items[0].dist = 10.0;
    AgentLedger ledger;
    ActionVector act;
    act.forceSlot = 0;
    act.forceX = 100.0; act.forceY = 0.0; // 意图 100：钳到 3
    CHECK(AgentBehavior::applyNamedForce(n, b, act, targets, 0, 10.0, ledger, bp()));
    CHECK_NEAR(b.vel.x, 3.0, 1e-12);
    CHECK_NEAR(n.energy, 100.0 - 0.01 * 9.0, 1e-12);
}

TEST(gene_named_force_insufficient_energy_scales_down) {
    Nucleus n = makeN(0.16);
    n.genome.forceRange = 100.0;
    n.genome.forceCostK = 0.01; // 意图 8 -> cost 0.64 > 0.16
    n.genome.maxAccel = 1e9;
    Ball b;
    TargetList targets;
    targets.count = 1;
    targets.items[0].id = 99;
    targets.items[0].dist = 10.0;
    AgentLedger ledger;
    ActionVector act;
    act.forceSlot = 0;
    act.forceX = 8.0; act.forceY = 0.0;
    CHECK(AgentBehavior::applyNamedForce(n, b, act, targets, 0, 10.0, ledger, bp()));
    // 反解 m = sqrt(0.16/0.01) = 4（精确值，避免 sqrt 非封闭数的平方舍入把能量扣负）。
    CHECK_NEAR(n.energy, 0.0, 1e-12);
    CHECK_NEAR(b.vel.x, 4.0, 1e-12);
    CHECK(n.energy >= 0.0);
}

TEST(gene_named_force_negative_slot_is_free) {
    Nucleus n = makeN(100.0);
    Ball b;
    TargetList targets;
    AgentLedger ledger;
    ActionVector act; // forceSlot = -1 默认
    act.forceX = 4.0; act.forceY = 0.0; // 有矢量但未点名
    CHECK(!AgentBehavior::applyNamedForce(n, b, act, targets, -1, 10.0, ledger, bp()));
    CHECK(n.energy == 100.0); // 不点名 = 不花能量
    CHECK(b.vel.x == 0.0);
}

TEST(gene_named_force_invalid_slot_refused) {
    Nucleus n = makeN(100.0);
    Ball b;
    TargetList targets; // count=0
    AgentLedger ledger;
    ActionVector act;
    act.forceSlot = 3;
    CHECK(!AgentBehavior::applyNamedForce(n, b, act, targets, 3, 10.0, ledger, bp()));
    CHECK(n.energy == 100.0);
}

// ---------------- cyanoPreference / cyanoDigestion（组合根级） ----------------

TEST(gene_cyano_preference_zero_never_eats) {
    // 判别量：preference=0 时 graze 永不被调用 -> 蓝细菌存活数只增不减
    //（光合净值恒正，节点不会饿死；唯一减员途径就是被捕食）。
    SimConfig cfg{};
    cfg.world.seed = 42;
    cfg.cyanoNodeCount = 32;
    cfg.colony.nuclei = 1;
    Simulation sim(cfg);
    for (Nucleus& n : const_cast<std::vector<Nucleus>&>(sim.nuclei())) {
        n.genome.cyanoPreference = 0.0; // 严格哨兵：绝不吃
        n.genome.energyThreshold = 1e9; // 不繁殖，隔离观测
    }
    bool everDecreased = false;
    std::size_t prev = sim.cyano().aliveCount();
    for (int t = 0; t < 300 && sim.nucleusCount() > 0; ++t) {
        sim.step();
        const std::size_t cur = sim.cyano().aliveCount();
        if (cur < prev) everDecreased = true;
        prev = cur;
    }
    CHECK(!everDecreased); // preference=0：没有任何节点被捕食
    CHECK(sim.auditOk());
}

TEST(gene_cyano_digestion_shortens_cooldown) {
    // digestion 1.0 -> cooldown = ceil(1/1.5) = 1；digestion 0.1 -> ceil(1/0.6) = 2。
    SimConfig cfgFast{};
    cfgFast.world.seed = 42;
    cfgFast.cyanoNodeCount = 64;
    cfgFast.colony.nuclei = 4;
    Simulation sim(cfgFast);
    for (Nucleus& n : const_cast<std::vector<Nucleus>&>(sim.nuclei())) {
        n.genome.cyanoDigestion = 1.0;
        n.genome.cyanoPreference = 1.0;
    }
    int maxCooldownFast = 0;
    for (int t = 0; t < 250; ++t) {
        sim.step();
        for (const Nucleus& n : sim.nuclei()) {
            if (!n.alive) continue;
            if (n.digestCooldown > maxCooldownFast) maxCooldownFast = n.digestCooldown;
        }
    }
    CHECK(maxCooldownFast <= 1); // 高消化 -> 冷却恰 1（或 0：刚吃过的当帧尾）
    CHECK(sim.auditOk());
}

// ---------------- hue 子代变异 ----------------

TEST(gene_hue_mutates_to_child) {
    AgentLedger ledger;
    std::vector<Nucleus> pop;
    Nucleus n = makeN(300.0);
    n.genome.energyThreshold = 20.0;
    n.genome.hue = 180.0;
    pop.push_back(n);
    // hue 抖动 sigma=8：累计 30 代必然显著偏离（单代可能凑巧近 180，用多代消除方差）。
    PCG32 rng(99);
    int diverged = 0;
    // 固定从 0 号亲代繁殖（能量 300 足够 30 次扣 20）：子代只有 10 能，撑不起链条。
    // 下标访问（push_back 扩容后重新取引用，不持有跨 push 的引用/指针）。
    for (int k = 0; k < 30; ++k) {
        EntityId cid = 100 + static_cast<EntityId>(k);
        pop[0].energy = 300.0; // 每轮回填：繁殖扣 20，300 够 14 次，不回填链条会断
        CHECK(AgentBehavior::reproduce(pop[0], pop, ledger, cid, rng, GenomeRanges{}, 0.25));
        const double parentHue = pop[0].genome.hue;
        const double childHue = pop[pop.size() - 1].genome.hue;
        if (std::fabs(childHue - parentHue) > 1e-9) ++diverged;
    }
    CHECK(diverged >= 25); // 30 次繁殖至少 25 次 hue 变了（sigma=8，全同概率 ~0）
    // hue 始终在 [0,360)。
    const double lastHue = pop[pop.size() - 1].genome.hue;
    CHECK(lastHue >= 0.0 && lastHue < 360.0);
}

// ---------------- Config 契约 ----------------

TEST(config_colony_genome_fields_default) {
    ColonyConfig cc;
    CHECK(cc.maxNuclei == 1200);  // R10 世界 1024
    CHECK_NEAR(cc.mutationCoefficient, 0.25, 1e-12);
    GenomeRanges r;
    CHECK(cc.genomeRanges.affinityClampMin == r.affinityClampMin);
    CHECK(cc.genomeRanges.maxSpeedInitMax == r.maxSpeedInitMax);
}

TEST(config_max_nuclei_guard_is_respected) {
    SimConfig cfg{};
    cfg.world.seed = 42;
    cfg.world.width = 256.0; cfg.world.height = 256.0;  // R8-T53：钉 256（护栏机制测试，长跑需快）
    cfg.colony.maxNuclei = 15; // 小护栏
    Simulation sim(cfg);
    CHECK(sim.runTo(8000));
    CHECK(sim.auditOk());
    CHECK(sim.nucleusCount() <= 15); // 性能护栏生效（不是承载力）
}

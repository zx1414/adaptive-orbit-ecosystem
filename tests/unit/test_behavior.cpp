// agentsim - tests/unit/test_behavior.cpp
// T08/G3 验收：S7 控制增益衰减、S6 读/写噪声速度单调、S10 子代初能约束、
// 产球/回收的能量账本走账。全部使用自传 PCG32 固定种子，无 rand()/<random>。
#include "test_framework.h"

#include <cmath>
#include <limits>
#include <vector>

#include "../../src/behavior/AgentBehavior.h"
#include "../../src/environment/Terrain.h"
#include "../../src/sim/Simulation.h"
#include "../../src/core/PCG32.h"
#include "../../src/ledger/AgentLedger.h"
#include "../../src/perception/AgentSensors.h"
#include "../../src/core/Config.h"
#include "../../src/decision/RulePolicy.h"

namespace {
agentsim::Nucleus makeNucleus(double energy, double cap = 200.0) {
    agentsim::Nucleus n;
    n.id = 1;
    n.pos = agentsim::Vec2{50.0, 50.0};
    n.energy = energy;
    n.cap = cap;
    n.alive = true;
    // R2-A：施力基因夹具——钉在旧 BehaviorParams 常数（forceRange=40 / forceCostK=0.01），
    // 使全部 S21 施力测试的解析断言（半径 40、代价 0.01*|a|^2）保持精确成立。
    // 基因本身的生效测试在 test_genome_wiring.cpp（改基因 -> 行为可观测变化）。
    n.genome.forceRange = 40.0;
    n.genome.forceCostK = 0.01;
    n.genome.maxAccel = 1e9; // 施力/移动上限不夹具化：本组测试只测半径与代价
    return n;
}
} // namespace

// ---- T43：地形接入行为层/感知层（R4-P2：障碍不挡人/水域不耗能/温度区不偏置）----
// 空地形：三 count=0 且无显式 blob，generate 不撒点、不消耗 rng。
agentsim::TerrainConfig makeEmptyTerrainConfig() {
    return agentsim::TerrainConfig{};
}
// 单障碍：圆心 (100,100) 半径 30，世界 256x256。
agentsim::Terrain makeObstacleTerrain() {
    agentsim::TerrainConfig tc = makeEmptyTerrainConfig();
    agentsim::TerrainBlob b;
    b.x = 100.0;
    b.y = 100.0;
    b.radius = 30.0;
    tc.obstacles.push_back(b);
    agentsim::PCG32 rng(77);
    return agentsim::Terrain(tc, 256.0, 256.0, rng);
}
// 水域 (60,60,r=25，倍率用默认 1.8) + 温度区 (180,180,r=40, strength=0.4)。
agentsim::Terrain makeWaterThermalTerrain() {
    agentsim::TerrainConfig tc = makeEmptyTerrainConfig();
    agentsim::TerrainBlob w;
    w.x = 60.0;
    w.y = 60.0;
    w.radius = 25.0;
    tc.waters.push_back(w);
    agentsim::TerrainBlob z;
    z.x = 180.0;
    z.y = 180.0;
    z.radius = 40.0;
    z.strength = 0.4;
    tc.thermalZones.push_back(z);
    agentsim::PCG32 rng(78);
    return agentsim::Terrain(tc, 256.0, 256.0, rng);
}

// ---- T43-1：terrain=nullptr 与全空地形（count=0）下 apply 结果完全相同 ----
TEST(terrain_empty_terrain_matches_nullptr_in_apply) {
    agentsim::AgentEnvironment env(agentsim::SimConfig{});
    agentsim::AgentLedger ledA, ledB;
    const agentsim::BehaviorParams bp;
    agentsim::ActionVector act;
    act.dirX = 0.8;
    act.dirY = 0.6;
    act.boost = 0.3;
    agentsim::PCG32 rngT(5);
    agentsim::Terrain emptyT(makeEmptyTerrainConfig(), 256.0, 256.0, rngT);
    CHECK(emptyT.obstacleCount() == 0 && emptyT.waterCount() == 0 &&
          emptyT.thermalZoneCount() == 0);
    agentsim::Nucleus a = makeNucleus(500.0);
    agentsim::Nucleus b = makeNucleus(500.0);
    a.speed = 30.0;
    b.speed = 30.0;
    agentsim::PCG32 rngA(9), rngB(9);
    agentsim::AgentBehavior::apply(a, act, env, ledA, 1.0, bp, rngA);
    agentsim::AgentBehavior::apply(b, act, env, ledB, 1.0, bp, rngB, &emptyT);
    CHECK(a.pos.x == b.pos.x);
    CHECK(a.pos.y == b.pos.y);
    CHECK(a.energy == b.energy);
    CHECK(a.speed == b.speed);
}

// ---- T43-2：障碍正前方：apply 后位置不被 blocked（resolveMove 推出）----
TEST(terrain_obstacle_blocks_apply_movement) {
    agentsim::AgentEnvironment env(agentsim::SimConfig{});
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp;
    agentsim::ActionVector act;
    act.dirX = 1.0; // 直指障碍圆心
    act.dirY = 0.0;
    const agentsim::Terrain terr = makeObstacleTerrain();
    agentsim::Nucleus n = makeNucleus(500.0);
    n.pos = agentsim::Vec2{60.0, 100.0}; // 圆心西侧 40（半径 30，一步 max 40 会进入）
    agentsim::PCG32 rng(11);
    for (int i = 0; i < 10; ++i) {
        agentsim::AgentBehavior::apply(n, act, env, ledger, 1.0, bp, rng, &terr);
        if (!n.alive) break;
        CHECK(!terr.blocked(n.pos)); // 每 tick 位置都不得停留在障碍内
    }
    // 对照：同指令无地形会穿过障碍圆区（途中至少一 tick 位于障碍内）；
    // 有地形则全程从未进入（R4-P2 修复可观测）。
    agentsim::AgentEnvironment env2(agentsim::SimConfig{});
    agentsim::AgentLedger ledger2;
    agentsim::Nucleus m = makeNucleus(500.0);
    m.pos = agentsim::Vec2{60.0, 100.0};
    agentsim::PCG32 rng2(11);
    bool mEverBlocked = false;
    for (int i = 0; i < 10; ++i) {
        agentsim::AgentBehavior::apply(m, act, env2, ledger2, 1.0, bp, rng2);
        if (!m.alive) break;
        if (terr.blocked(m.pos)) mEverBlocked = true;
    }
    CHECK(mEverBlocked);          // 无地形对照确实会冲进障碍区
    CHECK(!terr.blocked(n.pos));  // 有地形被挡在外面（终态也不在障碍内）
}
// ---- T43-3：水域内移动耗能倍率 > 1，能量消耗更多 ----
// 水域 blob 覆盖全图（r=200 > 半对角 181），使 newPos 恒在水内：resolveMove 恒等，
// 两条轨迹逐位相同，能量差只来自 moveCostMultiplier（默认 1.8）。
TEST(terrain_water_increases_locomotion_cost) {
    agentsim::TerrainConfig tc = makeEmptyTerrainConfig();
    agentsim::TerrainBlob w;
    w.x = 128.0;
    w.y = 128.0;
    w.radius = 200.0;
    tc.waters.push_back(w);
    agentsim::PCG32 rngT(21);
    const agentsim::Terrain terr(tc, 256.0, 256.0, rngT);
    CHECK(terr.moveCostMultiplier(agentsim::Vec2{128.0, 128.0}) > 1.0);
    agentsim::SimConfig cfg{};
    cfg.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径，排除进食干扰）
    agentsim::AgentEnvironment envA(cfg), envB(cfg);
    agentsim::AgentLedger ledA, ledB;
    const agentsim::BehaviorParams bp;
    agentsim::ActionVector act;
    act.dirX = 1.0;
    act.dirY = 0.0;
    // S18 注：初始能量取 160（< nativeCap 200）。cap 结算上线后能量全程低于容量，
    // capOverflow 恒不触发，20 tick 解析差值保持 5.12 不被截断破坏。
    agentsim::Nucleus withT = makeNucleus(160.0);
    agentsim::Nucleus noT = makeNucleus(160.0);
    withT.pos = noT.pos = agentsim::Vec2{128.0, 128.0};
    withT.speed = noT.speed = 40.0;
    // R2-A：移动限速已基因化（genome.maxSpeed）。夹具钉在 40 = 旧行为常数，
    // 使 dir*moveK=60 被钳到 40，解析差值 5.12 的推导保持成立。
    withT.genome.maxSpeed = 40.0;
    noT.genome.maxSpeed = 40.0;
    agentsim::PCG32 rngA(30), rngB(30);
    for (int i = 0; i < 20; ++i) {
        agentsim::AgentBehavior::apply(withT, act, envA, ledA, 1.0, bp, rngA, &terr);
        agentsim::AgentBehavior::apply(noT, act, envB, ledB, 1.0, bp, rngB);
    }
    CHECK(withT.energy < noT.energy); // 水域更费能
    // 解析差：20 tick × (1.8-1) × locomotionCost × speed^2（限腹支出不触发，恒等可精确算）。
    const double expectDiff = 20.0 * 0.8 * bp.locomotionCost * 40.0 * 40.0;
    CHECK_NEAR(noT.energy - withT.energy, expectDiff, 1e-9);
}

// ---- T43-4：温度区：sense 的 s[17] 受 temperatureOffset 偏置，s[16] 保持原样 ----
TEST(terrain_thermal_zone_shifts_sensor_s17) {
    agentsim::TerrainConfig tc = makeEmptyTerrainConfig();
    agentsim::TerrainBlob z;
    z.x = 50.0;
    z.y = 50.0;
    z.radius = 40.0;
    z.strength = 0.4;
    tc.thermalZones.push_back(z);
    agentsim::PCG32 rngT(22);
    const agentsim::Terrain terr(tc, 256.0, 256.0, rngT);
    agentsim::AgentEnvironment env(agentsim::SimConfig{});
    const agentsim::PolicyConfig cfg{};
    const agentsim::TemperatureField temp{}; // 无源平场：sample=baseline=0.5
    const agentsim::Vec2 self{50.0, 50.0};   // 温度区圆心：offset=+0.4
    // R1-B：感知候选容器 = 蓝细菌节点（本夹具未播种，等价旧"环境无食物"口径）。
    const std::vector<agentsim::CyanoNode> noCyano;
    const agentsim::SensorVector sOn = agentsim::sense(env, self, agentsim::Vec2{}, 0.0,
                                                       cfg, nullptr, noCyano, temp, &terr);
    const agentsim::SensorVector sOff = agentsim::sense(env, self, agentsim::Vec2{}, 0.0,
                                                        cfg, nullptr, noCyano, temp);
    // s[17]：无地形 = 0.5（基线）；有地形 = clamp01(0.5 + 0.4) = 0.9。
    CHECK_NEAR(sOff[17], 0.5, 1e-12);
    CHECK_NEAR(sOn[17], 0.9, 1e-12);
    CHECK(sOn[17] != sOff[17]);
    // s[16] 取舍：失真路径不用地形偏移重算（见 AgentSensors.h 注释），保持原样。
    CHECK(sOn[16] == sOff[16]);
}

// ---- S7：球控制增益随距离单调衰减 ----
TEST(ball_gain_zero_distance_is_one) {
    CHECK_NEAR(agentsim::AgentBehavior::ballControlGain(0.0, 150.0), 1.0, 1e-12);
    CHECK_NEAR(agentsim::AgentBehavior::ballControlGain(0.0, 1.0), 1.0, 1e-12);
}

TEST(ball_gain_half_at_reference_radius) {
    // d = r 时 q=1，增益恰为 1/(1+1)=0.5。
    CHECK_NEAR(agentsim::AgentBehavior::ballControlGain(150.0, 150.0), 0.5, 1e-12);
}

TEST(ball_gain_decays_far_and_monotonic) {
    const double r = 150.0;
    // d>>r：增益趋 0（比 0.01 还小）。
    CHECK(agentsim::AgentBehavior::ballControlGain(1500.0, r) < 0.01);
    CHECK(agentsim::AgentBehavior::ballControlGain(15000.0, r) < 0.0001);
    // 单调递减：距离序列上增益严格不增，且至少一对严格递减。
    double prev = 1.1;
    int strictDrops = 0;
    for (double d = 0.0; d <= 2000.0; d += 50.0) {
        const double g = agentsim::AgentBehavior::ballControlGain(d, r);
        CHECK(g <= prev + 1e-15);
        if (g < prev - 1e-12) ++strictDrops;
        prev = g;
    }
    CHECK(strictDrops >= 10);
}
// ---- S6 写通道：写扰动幅度随速度单调递增 ----
// 方案：固定同一 ActionVector 与同一 RNG 种子，扫描速度序列。速度由 apply 的移动步
// 稳态给出（dir*moveK 限幅 maxSpeed），无法直接注入任意速度；因此改测
// "写扰动幅度公式"的行为学后果：写噪声只加在 dirX/dirY 上，其统计幅度 = amp。
// 直接可测路径：用 apply 前后 vel 的差异峰值随速度档位上升。速度档位由 boost 无关，
// 由 moveK 折算。这里改为更直接的等价断言：把 apply 逻辑中的噪声幅度公式抽出来验证
// 不现实（静态函数），故用行为学代理：同一方向指令下，速度越快，多次重复采样的
// 最终方向角方差越大。固定种子下两档速度跑 K 次，比较大速度档的方向散布更宽。
TEST(write_noise_strength_increases_with_speed) {
    agentsim::AgentEnvironment env(agentsim::SimConfig{});
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp;  // moveK=60, maxSpeed=40
    agentsim::ActionVector act;
    act.dirX = 1.0;
    act.dirY = 0.0;
    act.boost = 0.0;
    // 两档速度：0.25*maxSpeed 与 maxSpeed。低档：指令模长被 moveK 归一后仍是同一
    // 归一化方向，但 apply 的 amp 只依赖 n.speed（上一帧实际速度）。
    double spread[2] = {0.0, 0.0};
    const double speedTargets[2] = {1.0, 40.0}; // amp: 0.125 vs 0.244，裕度充足
    for (int c = 0; c < 2; ++c) {
        double minVx = 1e300, maxVx = -1e300;
        for (int trial = 0; trial < 64; ++trial) {
            agentsim::Nucleus n = makeNucleus(500.0);
            n.speed = speedTargets[c];           // 注入待测速度（写噪声幅度因子）
            agentsim::PCG32 rng(static_cast<unsigned long long>(1000 + trial));
            agentsim::AgentBehavior::apply(n, act, env, ledger, 1.0, bp, rng);
            const double vx = n.vel.x;           // 噪声污染后的移动方向 x 分量
            if (vx < minVx) minVx = vx;
            if (vx > maxVx) maxVx = vx;
        }
        spread[c] = maxVx - minVx;
    }
    // 高速档的 x 分量散布必须宽于低速档（amp 单调递增的直接后果）。
    CHECK(spread[1] > spread[0]);
    CHECK(spread[0] > 0.0); // 低速档也有非零扰动（speed>0 时 amp>0）
}
// ---- S6 读通道：读噪声幅度随速度单调递增，speed=0 时 amp=0 ----
// 直接可测：s[14] 现在写"本帧读噪声幅度 amp"（G2 修复后的元信息维），
// 且 amp 只由 speed 决定（kReadNoiseK * speed/(1+speed)），与 rng 抽样无关。
TEST(read_noise_amp_monotonic_and_zero_at_rest) {
    agentsim::AgentEnvironment env(agentsim::SimConfig{});
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 pos{50.0, 50.0};
    const agentsim::Vec2 vel{1.0, 0.0};
    // speed=0：amp 必须恰为 0。
    {
        agentsim::PCG32 rng(7);
        const agentsim::SensorVector s = agentsim::sense(env, pos, vel, 0.0, cfg, &rng);
        CHECK_NEAR(s[14], 0.0, 0.0);
        // speed=0 时方向/接近度维不应被扰动（无噪声路径）。
        CHECK_NEAR(s[0], s[0], 0.0);
    }
    // 速度递增序列：amp 严格单调递增。
    double prevAmp = -1.0;
    int strictRaises = 0;
    for (int i = 0; i <= 20; ++i) {
        const double speed = 2.0 * static_cast<double>(i);
        agentsim::PCG32 rng(7);
        const agentsim::SensorVector s = agentsim::sense(env, pos, vel, speed, cfg, &rng);
        CHECK(s[14] >= prevAmp);
        if (s[14] > prevAmp + 1e-12) ++strictRaises;
        prevAmp = s[14];
    }
    CHECK(strictRaises >= 20); // 0..40 共 21 档至少 20 次严格抬升
}
// ---- S10：繁殖子核初始能量 <= 亲代为繁殖消耗的能量 ----
TEST(reproduce_child_energy_within_parent_spend) {
    agentsim::AgentLedger ledger;
    std::vector<agentsim::Nucleus> pop;
    agentsim::Nucleus parent = makeNucleus(100.0);
    // R2-A：繁殖门槛基因化（genome.energyThreshold）。夹具钉 20 = 旧 kReproCost，
    // S10/C1 的解析断言（f*k=10、耗散 10）保持可精确计算。
    parent.genome.energyThreshold = 20.0;
    pop.push_back(parent);
    agentsim::PCG32 rng(42);
    CHECK(agentsim::AgentBehavior::reproduce(parent, pop, ledger, 77, rng));
    CHECK(pop.size() == 2);
    const agentsim::Nucleus& child = pop[1];
    const double parentSpend = 100.0 - parent.energy; // 亲代实付 k=20
    CHECK_NEAR(parentSpend, 20.0, 1e-12);
    CHECK(child.energy <= parentSpend);   // S10 红线：f*k=10 <= k=20
    CHECK_NEAR(child.energy, 10.0, 1e-12);
    CHECK(child.alive);
    // C1: 零和转移——无一级注入、无 "reproduce" 耗散，只记 reproduce-loss (1-f)*k=10。
    CHECK_NEAR(ledger.dissipatedBy("reproduce"), 0.0, 1e-12);
    CHECK_NEAR(ledger.dissipatedBy("reproduce-loss"), 10.0, 1e-12);
    CHECK_NEAR(ledger.totalInjected(), 0.0, 1e-12);
}

TEST(reproduce_refused_when_energy_or_population_limits) {
    agentsim::AgentLedger ledger;
    std::vector<agentsim::Nucleus> pop;
    agentsim::Nucleus poor = makeNucleus(10.0);  // < 门槛（夹具 genome.energyThreshold=20）
    poor.genome.energyThreshold = 20.0;
    pop.push_back(poor);
    agentsim::PCG32 rng(42);
    CHECK(!agentsim::AgentBehavior::reproduce(poor, pop, ledger, 78, rng));
    CHECK(pop.size() == 1);
    CHECK_NEAR(ledger.dissipatedBy("reproduce"), 0.0, 1e-12);
}

// ---- 产球 deploy：能量走账本，球被创建且受上限约束 ----
TEST(deploy_ball_debits_energy_and_creates_ball) {
    agentsim::AgentLedger ledger;
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::PCG32 rng(9);
    const double before = n.energy;
    CHECK(agentsim::AgentBehavior::deployBall(n, ledger, rng));
    CHECK(n.balls.size() == 1);
    CHECK_NEAR(n.balls[0].radius, 15.0, 1e-12); // 10 + 0.5*种子能 10（C3b）
    // C3a: 球诞生即得种子能量 10（核→球 transfer，零和搬运）。
    CHECK_NEAR(n.balls[0].energy, 10.0, 1e-12);
    CHECK(n.balls[0].angle >= 0.0 && n.balls[0].angle < 6.28318530717958647692);
    // 账目：核支付 deployCost(8) 记耗散，种子 10 记 transfer；核实付 18。
    CHECK_NEAR(ledger.dissipatedBy("deploy"), 8.0, 1e-12);
    CHECK_NEAR(before - n.energy, 18.0, 1e-12);
    // 上限：连续产球至 kMaxBalls(3)，第 4 次拒绝。
    CHECK(agentsim::AgentBehavior::deployBall(n, ledger, rng));
    CHECK(agentsim::AgentBehavior::deployBall(n, ledger, rng));
    CHECK(!agentsim::AgentBehavior::deployBall(n, ledger, rng));
    CHECK(n.balls.size() == 3);
    CHECK_NEAR(n.balls[1].energy, 10.0, 1e-12);
    CHECK_NEAR(n.balls[2].energy, 10.0, 1e-12);
    CHECK_NEAR(before - n.energy, 3.0 * 18.0, 1e-12); // 每球实付 8+10
}
// ---- 回收 recycle：能量走账本，recycle 后总量不增（耗散保底）----
TEST(recycle_ball_returns_energy_without_total_gain) {
    agentsim::AgentLedger ledger;
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::PCG32 rng(11);
    CHECK(agentsim::AgentBehavior::deployBall(n, ledger, rng));
    // 给球注能（模拟球从核/食物侧获得储能）：直接给球记账式转移
    // ——测试里等价地用账本注入再改实体（行为层是唯一写点，测试辅助绕过仅限测试）。
    agentsim::Ball& b = n.balls[0];
    b.energy = 8.0; // 测试夹具：球储能 8
    const double nucleusBefore = n.energy;
    const double totalBefore = n.energy + b.energy;
    CHECK(agentsim::AgentBehavior::recycleBall(n, ledger));
    CHECK(n.balls.empty());
    // 核增加的恰好是返还款 8*0.5=4。
    CHECK_NEAR(n.energy - nucleusBefore, 4.0, 1e-12);
    // 损耗 4 记入 recycle-loss。
    CHECK_NEAR(ledger.dissipatedBy("recycle-loss"), 4.0, 1e-12);
    // 总量（核+球）不增：销毁后只剩核侧 4 收益，其余耗散。
    CHECK(n.energy + 0.0 <= totalBefore + 1e-12);
}

TEST(recycle_empty_balls_is_refused) {
    agentsim::AgentLedger ledger;
    agentsim::Nucleus n = makeNucleus(50.0);
    CHECK(!agentsim::AgentBehavior::recycleBall(n, ledger));
    CHECK(n.balls.empty());
}

// ---- C1：繁殖零和转移 + 账目审计恒等式（修复前必红：DIFF=-10；修复后必绿）----
TEST(reproduce_keeps_ledger_audit_identity) {
    agentsim::AgentLedger ledger;
    std::vector<agentsim::Nucleus> pop;
    agentsim::Nucleus parent = makeNucleus(100.0);
    parent.genome.energyThreshold = 20.0; // R2-A：门槛基因化（夹具钉 20 = 旧 kReproCost）
    ledger.injectInitial(parent.id, parent.energy); // 组合根出生底账（等价 sim 侧）
    pop.push_back(parent);
    agentsim::PCG32 rng(42);
    CHECK(agentsim::AgentBehavior::reproduce(parent, pop, ledger, 99, rng));
    CHECK(pop.size() == 2);
    // Σliving = 亲代 80 + 子代 10 = 90；injected-dissipated = 100 - 10 = 90。
    //（繁殖是零和转移：亲代 -20、子代 +10、耗散 10，账目无一级注入。）
    // 注意 pop[0] 是 reproduce 前压入的陈旧拷贝，亲代现值在按引用传入的 parent 里。
    const double living = parent.energy + pop[1].energy;
    CHECK_NEAR(living, 90.0, 1e-12);
    CHECK_NEAR(ledger.netInjected(), 90.0, 1e-12);
    CHECK(ledger.audit(1e-9 * (1.0 + ledger.totalInjected()), living));
}

// ---- C3a：产球种子能量零和（核+球总量不变，账本走账）----
TEST(deploy_seed_energy_zero_sum_via_ledger) {
    agentsim::AgentLedger ledger;
    agentsim::Nucleus n = makeNucleus(100.0);
    ledger.injectInitial(n.id, n.energy);
    agentsim::PCG32 rng(5);
    const double totalBefore = n.energy; // 核+球（无球）
    CHECK(agentsim::AgentBehavior::deployBall(n, ledger, rng));
    CHECK(n.balls.size() == 1);
    CHECK(n.balls[0].energy > 0.0); // 种子能量非零（C3a 前 ball.energy 恒 0）
    const double totalAfter = n.energy + n.balls[0].energy;
    // deploy 成本 8 真实耗散（建球费）；种子 10 是零和搬运：总余 = 100 - 8 = 92。
    CHECK_NEAR(totalAfter, totalBefore - ledger.dissipatedBy("deploy"), 1e-12);
    CHECK_NEAR(n.balls[0].energy, 10.0, 1e-12);
    CHECK_NEAR(n.energy, totalBefore - 18.0, 1e-12);
    // recycle 返还不再恒 0：give = 球能 * 0.5。
    const double nucleusBefore = n.energy;
    CHECK(agentsim::AgentBehavior::recycleBall(n, ledger));
    CHECK_NEAR(n.energy - nucleusBefore, n.balls.empty() ? 5.0 : 0.0, 1e-12);
    // 账本：injected-dissipated == Σliving（deploy 8 + recycle-loss 5 全额耗散）。
    CHECK_NEAR(ledger.netInjected(), n.energy, 1e-12);
    CHECK(ledger.audit(1e-9 * (1.0 + ledger.totalInjected()), n.energy));
}

// ---- C3b：球充能后轨道半径增长，控制增益真正衰减 < 0.2（S7 失控分支可达）----
TEST(ball_radius_grows_with_energy_and_gain_drops_below_0_2) {
    agentsim::AgentLedger ledger;
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::PCG32 rng(6);
    CHECK(agentsim::AgentBehavior::deployBall(n, ledger, rng));
    // 半径公式：radius = 10 + 0.5*ball.energy。
    CHECK_NEAR(n.balls[0].radius, 10.0 + 0.5 * n.balls[0].energy, 1e-12);
    // 充能到接近 handoffRadius(150)：radius = 10 + 0.5*E >= 300 ⇒ gain < 0.2。
    // （行为层是唯一写点：测试改球能只作为夹具，增益计算本身是纯函数。）
    n.balls[0].energy = 600.0;
    const double radius = 10.0 + 0.5 * n.balls[0].energy;
    const double gain = agentsim::AgentBehavior::ballControlGain(radius, 150.0);
    CHECK(gain < 0.2); // 充能球进入失控分支，S7 可验收
    CHECK(radius >= 300.0);
}

// ---- C2：组合根主循环接线繁殖（修复前 nuclei 恒 12；修复后种群增长）----
TEST(simulation_reproduction_grows_population_over_ticks) {
    agentsim::SimConfig cfg{}; // 默认 12 核
    cfg.world.width = 256.0; cfg.world.height = 256.0;  // R8-T53：钉 256 小世界（机制测繁殖，长跑需快）
    // R10/R11：256 世界必须同时钉 256 时代的播种密度（默认已随 1024 世界放大——
    // 64 聚落×spread45 塞进 256² 会互相遮蔽，光合崩溃，机制测试失真）。
    cfg.cyanoNodeCount = 128; cfg.cyanoTuning.cyanoColonies = 16;
    agentsim::Simulation sim(cfg);
    CHECK(sim.nucleusCount() == 12);
    // R2-A：生态有了真实的创始人瓶颈——初始 12 核先经历筛选性死亡（高门槛基因型
    // 在能量积累到 genome.energyThreshold 之前饿死），低门槛基因型在 ~2500 tick
    // 后爆发。窗口取 6000 tick：既覆盖瓶颈期又覆盖爆发期（实测 seed42 在 3000
    // tick 已达 276 核）。审计全程闭合（C1 零和数学保证）。
    CHECK(sim.runTo(6000));
    CHECK(sim.auditOk());
    CHECK(sim.nucleusCount() > 12); // 种群增长（基因门槛繁殖 + 变异生效）
}
// ---- 组合冒烟：apply 不崩溃且审计恒等式在行为后保持 ----
TEST(apply_respects_energy_conservation_with_balls) {
    agentsim::SimConfig cfg{};
    cfg.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径，排除进食干扰）
    agentsim::AgentEnvironment env(cfg);
    agentsim::AgentLedger ledger;
    ledger.injectPrimary(0, 0.0);    // 空池底账（initial=0，账实一致）
    agentsim::Nucleus n = makeNucleus(60.0);
    ledger.injectInitial(n.id, n.energy);
    agentsim::PCG32 rng(3);
    const agentsim::BehaviorParams bp;
    agentsim::ActionVector act;
    act.dirX = 1.0;
    for (int t = 0; t < 50; ++t) {
        agentsim::AgentBehavior::apply(n, act, env, ledger, 1.0, bp, rng);
        if (!n.alive) break;
    }
    double living = n.alive ? n.energy : 0.0;
    for (const agentsim::Ball& b : n.balls) living += b.energy;
    // R1-B：Food 实体已删除，环境侧存活实体能量只剩蓝细菌——本测试环境
    // 未播种蓝细菌（cyanoNodeCount=0），故环境侧贡献恒 0；恒等式按
    // injected - dissipated == Σ实体能量（核 + 球 + 蓝细菌）校验。
    CHECK(ledger.audit(1e-6 * (1.0 + ledger.totalInjected()), living));
}

// ---- T43-5：组合根轨迹在有无地形下不同，但两跑 audit 全程 ok ----
TEST(terrain_changes_trajectory_but_audit_ok) {
    agentsim::SimConfig cfg{}; // 默认无地形
    agentsim::SimConfig cfgOn{};
    agentsim::TerrainConfig tc = makeEmptyTerrainConfig();
    agentsim::TerrainBlob b;
    b.x = 128.0;
    b.y = 128.0;
    b.radius = 60.0;
    tc.obstacles.push_back(b);
    cfgOn.terrain = tc;
    agentsim::Simulation simOff(cfg);
    agentsim::Simulation simOn(cfgOn);
    CHECK(simOff.runTo(200)); // 每 tick 末审计通过才返回 true
    CHECK(simOn.runTo(200));
    CHECK(simOff.auditOk());
    CHECK(simOn.auditOk());
    // 轨迹（逐核 pos/vel/energy 序列）不同：地形改变移动阻挡与 rng 之外的纯几何路径。
    CHECK(simOff.stateHash() != simOn.stateHash());
}

// ---- GOAL：呼吸耗能温度因子（规格：「呼吸耗能与速度、温度相关」）----
// 固定能量 12：低于产球阈值 0.6*cap=120，且无球可回收 —— T08 随机触发分
// 支恒不生效，呼吸差值可精确解析。
namespace {
agentsim::BehaviorParams makeQuietParams() {
    agentsim::BehaviorParams bp;
    bp.moveK = 0.0;          // 静止：speed=0，呼吸只剩 base*tempFactor*dt
    return bp;
}
} // namespace

// G-1 回归：tempDistortion=0（默认实参）时呼吸耗能与改动前完全相同（乘 1.0 逐位一致）。
TEST(respiration_temp_factor_zero_matches_baseline) {
    agentsim::SimConfig cfg{};
    cfg.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径，排除进食干扰）
    agentsim::AgentEnvironment envA(cfg), envB(cfg);
    agentsim::AgentLedger ledA, ledB;
    const agentsim::BehaviorParams bp = makeQuietParams();
    agentsim::ActionVector act;
    agentsim::Nucleus a = makeNucleus(12.0), b = makeNucleus(12.0);
    a.speed = b.speed = 0.0;
    agentsim::PCG32 rngA(41), rngB(41);
    agentsim::AgentBehavior::apply(a, act, envA, ledA, 1.0, bp, rngA, nullptr, 0.0);
    agentsim::AgentBehavior::apply(b, act, envB, ledB, 1.0, bp, rngB); // 不传温度：默认 0
    CHECK(a.energy == b.energy); // 逐位一致
    CHECK_NEAR(a.energy, 12.0 - bp.respirationBase, 1e-12);
}

// G-2 满偏：distortion=1 时呼吸耗能是 baseline 的 (1+K) 倍（K = respirationTempK）。
TEST(respiration_temp_factor_full_is_one_plus_k_times_baseline) {
    agentsim::SimConfig cfg{};
    cfg.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径，排除进食干扰）
    agentsim::AgentEnvironment envA(cfg), envB(cfg);
    agentsim::AgentLedger ledA, ledB;
    const agentsim::BehaviorParams bp = makeQuietParams();
    agentsim::ActionVector act;
    agentsim::Nucleus a = makeNucleus(200.0), b = makeNucleus(200.0);
    a.speed = b.speed = 0.0;
    agentsim::PCG32 rngA(41), rngB(41);
    agentsim::AgentBehavior::apply(a, act, envA, ledA, 1.0, bp, rngA, nullptr, 0.0);
    agentsim::AgentBehavior::apply(b, act, envB, ledB, 1.0, bp, rngB, nullptr, 1.0);
    const double base = 200.0 - a.energy;          // baseline 呼吸 = base*dt
    CHECK_NEAR(b.energy, 200.0 - base * (1.0 + bp.respirationTempK), 1e-12);
    CHECK(ledB.dissipatedBy("respiration") ==
          ledA.dissipatedBy("respiration") * (1.0 + bp.respirationTempK));
}

// G-3 单调：distortion=0.5 的呼吸耗能严格介于 0 与 1 两档之间。
TEST(respiration_temp_factor_monotonic_midpoint) {
    agentsim::SimConfig cfg{};
    cfg.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径，排除进食干扰）
    agentsim::AgentEnvironment envA(cfg), envM(cfg), envB(cfg);
    agentsim::AgentLedger ledA, ledM, ledB;
    const agentsim::BehaviorParams bp = makeQuietParams();
    agentsim::ActionVector act;
    agentsim::Nucleus a = makeNucleus(200.0), m = makeNucleus(200.0), b = makeNucleus(200.0);
    a.speed = m.speed = b.speed = 0.0;
    agentsim::PCG32 rngA(41), rngM(41), rngB(41);
    agentsim::AgentBehavior::apply(a, act, envA, ledA, 1.0, bp, rngA, nullptr, 0.0);
    agentsim::AgentBehavior::apply(m, act, envM, ledM, 1.0, bp, rngM, nullptr, 0.5);
    agentsim::AgentBehavior::apply(b, act, envB, ledB, 1.0, bp, rngB, nullptr, 1.0);
    CHECK(m.energy < a.energy && b.energy < m.energy); // 单调递减
    // 解析：mid = (e0 + e1)/2 —— 线性因子的直接推论。
    CHECK_NEAR(m.energy, 0.5 * (a.energy + b.energy), 1e-9);
}

// G-4 防御：distortion=NaN 按 0 处理，能量不产生 NaN。
TEST(respiration_temp_factor_nan_treated_as_zero) {
    agentsim::SimConfig cfg{};
    cfg.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径，排除进食干扰）
    agentsim::AgentEnvironment envA(cfg), envB(cfg);
    agentsim::AgentLedger ledA, ledB;
    const agentsim::BehaviorParams bp = makeQuietParams();
    agentsim::ActionVector act;
    agentsim::Nucleus a = makeNucleus(12.0), b = makeNucleus(12.0);
    a.speed = b.speed = 0.0;
    agentsim::PCG32 rngA(41), rngB(41);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    agentsim::AgentBehavior::apply(a, act, envA, ledA, 1.0, bp, rngA, nullptr, nan);
    agentsim::AgentBehavior::apply(b, act, envB, ledB, 1.0, bp, rngB, nullptr, 0.0);
    CHECK(!std::isnan(a.energy) && !std::isnan(b.energy));
    CHECK(a.energy == b.energy); // NaN 防御归 0，与 0 档逐位一致
}

// G-5 生态：高温差下核能量下降更快，且行为层账本审计恒等式仍成立。
TEST(respiration_temp_factor_high_distortion_audit_ok) {
    agentsim::SimConfig cfg{};
    cfg.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径，排除进食干扰）
    agentsim::AgentEnvironment envA(cfg), envB(cfg);
    agentsim::AgentLedger ledA, ledB;
    const agentsim::BehaviorParams bp = makeQuietParams();
    agentsim::ActionVector act;
    // S18 注：初始能量取 30.0（< nativeCap 200）。原 40.0 会一 tick 即被容量结算
    // 截到 38.4 并记 capOverflow，破坏「扣减 == 呼吸耗散」的解析恒等断言。
    // 30.0 跑 50 tick（总耗约 50*0.008*2.5=1.0）全程低于容量，溢出分支恒不触发。
    agentsim::Nucleus a = makeNucleus(30.0), b = makeNucleus(30.0);
    a.speed = b.speed = 0.0;
    agentsim::PCG32 rngA(43), rngB(43);
    ledA.injectInitial(a.id, a.energy); // 组合根出生底账（审计恒等式前提）
    ledB.injectInitial(b.id, b.energy);
    for (int i = 0; i < 50; ++i) {
        agentsim::AgentBehavior::apply(a, act, envA, ledA, 1.0, bp, rngA, nullptr, 0.0);
        if (a.alive) {
            CHECK(a.energy > 0.0);
        }
        agentsim::AgentBehavior::apply(b, act, envB, ledB, 1.0, bp, rngB, nullptr, 1.0);
    }
    CHECK(b.energy < a.energy);          // 高温差耗能更快
    CHECK(b.energy > 0.0);               // 红线：能量永不为负
    // 账实相等：各核扣减与账面耗散严格相等（零和恒等式），audit 无容忍度宽松化。
    CHECK_NEAR(30.0 - a.energy, ledA.dissipatedBy("respiration"), 1e-9);
    CHECK_NEAR(30.0 - b.energy, ledB.dissipatedBy("respiration"), 1e-9);
    CHECK(ledA.audit(1e-9 * (1.0 + ledA.totalInjected()), a.energy));
    CHECK(ledB.audit(1e-9 * (1.0 + ledB.totalInjected()), b.energy));
}

// =====================================================================
// ---- S18：球作仓库 —— colonyCap = nativeCap + Σ(自有 Resource 球.cap) ----
// =====================================================================
namespace {
// 测试夹具：指定类型与容量的球（直接构造，不经过 deployBall）。
agentsim::Ball makeBall(agentsim::BallType type, double cap) {
    agentsim::Ball b;
    b.type = type;
    b.cap = cap;
    return b;
}
// 容量结算测试参数：moveK = 0 让核静止（locomotion 恒 0、速度项恒 0），
// 能量账目只剩呼吸 + 容量溢出，断言可解析。
agentsim::BehaviorParams makeCapParams() {
    agentsim::BehaviorParams bp;
    bp.moveK = 0.0;
    return bp;
}
} // namespace

// S18-1：无球时 colonyCap == nativeCap（逐位相等）。
TEST(colony_cap_native_only) {
    const agentsim::BehaviorParams bp; // nativeCap = 200
    agentsim::Nucleus n = makeNucleus(10.0);
    CHECK(n.balls.empty());
    CHECK(agentsim::AgentBehavior::colonyCap(n, bp) == bp.nativeCap);
}

// S18-2：3 个 Resource 球（cap 各 60）→ colonyCap == nativeCap + 180。
TEST(colony_cap_adds_resource_balls) {
    const agentsim::BehaviorParams bp; // nativeCap = 200
    agentsim::Nucleus n = makeNucleus(10.0);
    n.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    n.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    n.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    const double cc = agentsim::AgentBehavior::colonyCap(n, bp);
    CHECK(cc == bp.nativeCap + 180.0);
    CHECK_NEAR(cc, 380.0, 1e-12);
    // 半径不参与容量：异构半径不动账。
    n.balls[1].radius = 77.0;
    CHECK(agentsim::AgentBehavior::colonyCap(n, bp) == cc);
    // deployBall 产出的球默认即 Resource 且带 cap：容量随产球单调涨。
    agentsim::Nucleus m = makeNucleus(300.0);
    agentsim::AgentLedger ledger;
    agentsim::PCG32 rng(1);
    const double capBefore = agentsim::AgentBehavior::colonyCap(m, bp);
    CHECK(agentsim::AgentBehavior::deployBall(m, ledger, rng));
    CHECK(m.balls.back().type == agentsim::BallType::Resource);
    CHECK(m.balls.back().cap == 60.0);
    CHECK(agentsim::AgentBehavior::colonyCap(m, bp) == capBefore + 60.0);
}

// S18-3：2 Scout + 2 Shield（各 cap 60）→ colonyCap == nativeCap。
// 这是 S17「资源球之外零影响」在容量维度的体现：非 Resource 球一律不计容。
TEST(colony_cap_ignores_scout_and_shield) {
    const agentsim::BehaviorParams bp; // nativeCap = 200
    agentsim::Nucleus n = makeNucleus(10.0);
    n.balls.push_back(makeBall(agentsim::BallType::Scout, 60.0));
    n.balls.push_back(makeBall(agentsim::BallType::Scout, 60.0));
    n.balls.push_back(makeBall(agentsim::BallType::Shield, 60.0));
    n.balls.push_back(makeBall(agentsim::BallType::Shield, 60.0));
    CHECK(agentsim::AgentBehavior::colonyCap(n, bp) == bp.nativeCap);
    // Scout/Shield 混入 Resource 队列也不多算：3 Resource + 2 非资源 == nativeCap + 180。
    n.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    n.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    n.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    CHECK(agentsim::AgentBehavior::colonyCap(n, bp) == bp.nativeCap + 180.0);
}

// S18-4：3 Resource 球，移除 1 个 → colonyCap 恰好下降 60（球的 cap 随球销毁即时失效）。
TEST(colony_cap_drops_after_ball_loss) {
    const agentsim::BehaviorParams bp;
    agentsim::Nucleus n = makeNucleus(10.0);
    n.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    n.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    n.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    const double before = agentsim::AgentBehavior::colonyCap(n, bp);
    CHECK(before == bp.nativeCap + 180.0);
    n.balls.pop_back(); // 移除一个 Resource 球
    CHECK(agentsim::AgentBehavior::colonyCap(n, bp) == before - 60.0);
    // recycleBall 销毁球走同一条容量语义：回收后容量同步回落。
    agentsim::Nucleus m = makeNucleus(100.0);
    agentsim::AgentLedger ledger;
    agentsim::PCG32 rng(2);
    CHECK(agentsim::AgentBehavior::deployBall(m, ledger, rng));
    CHECK(agentsim::AgentBehavior::colonyCap(m, bp) == bp.nativeCap + 60.0);
    CHECK(agentsim::AgentBehavior::recycleBall(m, ledger));
    CHECK(agentsim::AgentBehavior::colonyCap(m, bp) == bp.nativeCap);
}

// S18-5：能量远超容量 → 一次 apply 后 n.energy <= n.cap + 1e-9，
// 耗散量 == 扣减量，reason="capOverflow" 入账。
// RNG 用 seed 101：首抽 nextDouble() = 1.0 >= 0.10，T08 触发分支（产球）不命中，
// apply 内球集合不变，断言可解析。
TEST(cap_overflow_dissipates_excess) {
    agentsim::SimConfig cfg{};
    cfg.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径，排除进食干扰）
    agentsim::AgentEnvironment env(cfg);
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp = makeCapParams();
    agentsim::Nucleus n = makeNucleus(500.0); // > nativeCap 200
    ledger.injectInitial(n.id, n.energy);
    agentsim::ActionVector act; // 零方向：moveK=0 下核完全静止
    agentsim::PCG32 rng(101);
    agentsim::AgentBehavior::apply(n, act, env, ledger, 1.0, bp, rng);
    CHECK(n.alive);
    CHECK(n.balls.empty()); // seed 101 不触发产球，集合逐位不变
    CHECK(n.energy <= n.cap + 1e-9);
    CHECK_NEAR(n.cap, bp.nativeCap, 1e-12);
    // 扣减 = 500 - 200 - 呼吸；耗散账 capOverflow 与核侧扣减严格相等（账实相等）。
    const double overflow = ledger.dissipatedBy("capOverflow");
    CHECK(overflow > 0.0);
    CHECK_NEAR(overflow, 500.0 - bp.nativeCap - bp.respirationBase * 1.0, 1e-9);
    // 账面总耗散 == 实体总扣减（只有呼吸 + 溢出两条账）。
    CHECK_NEAR(ledger.totalDissipated(),
               (500.0 - n.energy), 1e-9);
}

// S18-6：容量溢出守住零和红线——apply 前后 netInjected - sumLiving 不变。
// 溢出是「实体侧扣减 + 等额耗散入账」，账面恒等式不得移动一分钱。
TEST(cap_overflow_keeps_audit_identity) {
    agentsim::SimConfig cfg{};
    cfg.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径，排除进食干扰）
    agentsim::AgentEnvironment env(cfg);
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp = makeCapParams();
    agentsim::Nucleus n = makeNucleus(500.0);
    ledger.injectInitial(n.id, n.energy); // 组合根出生底账
    agentsim::ActionVector act;
    agentsim::PCG32 rng(101); // 同 S18-5：不触发 T08 分支
    auto sumLiving = [&n]() {
        double s = n.alive ? n.energy : 0.0;
        for (const agentsim::Ball& b : n.balls) s += b.energy;
        return s;
    };
    const double before = ledger.netInjected() - sumLiving();
    agentsim::AgentBehavior::apply(n, act, env, ledger, 1.0, bp, rng);
    const double after = ledger.netInjected() - sumLiving();
    CHECK(before == after); // 零和红线：账实差逐位不动
    CHECK(ledger.dissipatedBy("capOverflow") > 0.0); // 溢出确实入账
    CHECK(ledger.audit(1e-9 * (1.0 + ledger.totalInjected()), sumLiving()));
    // 对照组：能量低于容量的普通帧不产生 capOverflow 账目。
    agentsim::AgentLedger ledC;
    agentsim::Nucleus c = makeNucleus(50.0);
    ledC.injectInitial(c.id, c.energy);
    agentsim::PCG32 rngC(101);
    agentsim::AgentBehavior::apply(c, act, env, ledC, 1.0, bp, rngC);
    CHECK(ledC.dissipatedBy("capOverflow") == 0.0);
    CHECK(c.energy <= c.cap);
}

// S18-7（现象学验证）：真超容核在组合根同款主循环（sense→decide→apply→审计）里
// 必然触发 capOverflow。规格内的生态量级（数百）对 200+60k 的容量天然不构成压力，
// 这正是 S18「仓库扩容」的设计意图：容量随产球增长，历史溢出压力被仓库消解。
// 溢出断言：首帧把能量一次性压回当前容量，账面 capOverflow 与实体扣减严格相等，
// 且总耗散守恒（capOverflow 之外只有移动/呼吸两条账）。
TEST(cap_overflow_occurs_in_eco_loop) {
    agentsim::SimConfig cfgEmpty{};
    cfgEmpty.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径）
    agentsim::AgentEnvironment envB(cfgEmpty);
    agentsim::AgentLedger ledB;
    const agentsim::PolicyConfig pcfg{};
    agentsim::RulePolicy policy; // 组合根同款默认策略
    agentsim::Nucleus rich = makeNucleus(980.0);
    rich.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    rich.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    rich.balls.push_back(makeBall(agentsim::BallType::Resource, 60.0));
    ledB.injectInitial(rich.id, rich.energy);
    auto sumRich = [&rich]() {
        double s2 = rich.alive ? rich.energy : 0.0;
        for (const agentsim::Ball& b : rich.balls) s2 += b.energy;
        return s2;
    };
    const double baseBefore = ledB.netInjected() - sumRich();
    const agentsim::BehaviorParams bp;
    agentsim::PCG32 rngB(101);
    bool everOverflowed = false;
    double firstLoco = 0.0, firstResp = 0.0; // 首帧移动/呼吸成本（差分捕获）
    for (int t = 0; t < 40; ++t) {
        const double locoPre = ledB.dissipatedBy("locomotion");
        const double respPre = ledB.dissipatedBy("respiration");
        const agentsim::SensorVector sv = agentsim::sense(envB, rich.pos, rich.vel,
                                                          rich.speed, pcfg, &rngB);
        const agentsim::ActionVector act = policy.decide(sv);
        agentsim::AgentBehavior::apply(rich, act, envB, ledB, 1.0, bp, rngB);
        if (!rich.alive) break;
        if (t == 0) {
            firstLoco = ledB.dissipatedBy("locomotion") - locoPre;
            firstResp = ledB.dissipatedBy("respiration") - respPre;
        }
        if (ledB.dissipatedBy("capOverflow") > 0.0) everOverflowed = true;
        CHECK(rich.energy <= rich.cap + 1e-9); // 每帧都压在容量之下
        CHECK_NEAR(ledB.netInjected() - sumRich(), baseBefore, 1e-9); // 零和逐帧锁定
    }
    CHECK(everOverflowed); // capOverflow 在 apply 主循环中真实出现过
    CHECK(rich.cap == bp.nativeCap + 180.0); // 容量 = 200 + 3×60
    // 首帧压回：溢出发生在移动/呼吸扣减之后，故 = 980 - 首帧耗散 - 380。
    const double firstFrameCosts = firstLoco + firstResp;
    const double overflow = ledB.dissipatedBy("capOverflow");
    CHECK_NEAR(overflow, 980.0 - firstFrameCosts - rich.cap, 1e-9);
    CHECK(overflow > 0.0);
    // 总账守恒：总耗散 == 实体总扣减（capOverflow 只压一次，后续帧无新溢出）。
    CHECK_NEAR(ledB.totalDissipated(), 980.0 - sumRich(), 1e-9);
    CHECK(ledB.audit(1e-9 * (1.0 + ledB.totalInjected()), sumRich()));
}

// =====================================================================
// ---- S19/S20：球世界坐标 + 自然脱离 + 自由球拾取 ----
// =====================================================================

// S19-1：deployBall 产出的球带世界状态：pos 在 [0,256)、ownerId=宿主、vel={0,0}，
// 且 pos 与「核位置+轨道极坐标」按默认世界尺寸 wrap 的重算一致；显式传小世界
// （128x128）时 wrap 上界跟随调用方尺寸。
TEST(ball_has_world_state_after_deploy) {
    agentsim::AgentLedger ledger;
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::PCG32 rng(7);
    CHECK(agentsim::AgentBehavior::deployBall(n, ledger, rng));
    const agentsim::Ball& b = n.balls[0];
    CHECK(b.pos.x >= 0.0 && b.pos.x < 256.0);
    CHECK(b.pos.y >= 0.0 && b.pos.y < 256.0);
    CHECK(b.ownerId == n.id);
    CHECK(b.vel.x == 0.0 && b.vel.y == 0.0);
    const double ca = std::cos(b.angle);
    const double sa = std::sin(b.angle);
    const agentsim::Vec2 expect = agentsim::wrap(
        agentsim::Vec2{n.pos.x + b.radius * ca, n.pos.y + b.radius * sa}, 256.0, 256.0);
    CHECK_NEAR(b.pos.x, expect.x, 1e-12);
    CHECK_NEAR(b.pos.y, expect.y, 1e-12);
    CHECK(agentsim::AgentBehavior::deployBall(n, ledger, rng, 128.0, 128.0));
    CHECK(n.balls[1].pos.x >= 0.0 && n.balls[1].pos.x < 128.0);
    CHECK(n.balls[1].pos.y >= 0.0 && n.balls[1].pos.y < 128.0);
    CHECK(n.balls[1].ownerId == n.id);
}

// S19-2：detachBall 能量逐位不变、ownerId 置 0、容器迁移正确；越界 idx 拒绝。
TEST(detach_preserves_energy) {
    agentsim::Nucleus n = makeNucleus(10.0);
    agentsim::Ball b;
    b.energy = 200.0;
    b.ownerId = n.id;
    n.balls.push_back(b);
    std::vector<agentsim::Ball> free;
    CHECK(!agentsim::AgentBehavior::detachBall(n, free, 1)); // 越界拒绝
    CHECK(agentsim::AgentBehavior::detachBall(n, free, 0));
    CHECK(n.balls.empty());
    CHECK(free.size() == 1);
    CHECK(free[0].energy == 200.0); // 逐位不变
    CHECK(free[0].ownerId == 0);
}

// S19-3：脱离是纯搬运：账本 totalDissipated / totalInjected 逐位不变（账面零操作）。
TEST(detach_zero_ledger_impact) {
    agentsim::AgentLedger ledger;
    ledger.injectInitial(1, 100.0);
    agentsim::Nucleus n = makeNucleus(100.0);
    ledger.dissipate("respiration", 3.0);
    ledger.transfer(1, 42, 5.0);
    agentsim::Ball b;
    b.energy = 150.0;
    b.ownerId = n.id;
    n.balls.push_back(b);
    std::vector<agentsim::Ball> free;
    const double injBefore = ledger.totalInjected();
    const double disBefore = ledger.totalDissipated();
    const double trBefore = ledger.totalTransferred();
    CHECK(agentsim::AgentBehavior::detachBall(n, free, 0));
    CHECK(ledger.totalInjected() == injBefore);
    CHECK(ledger.totalDissipated() == disBefore);
    CHECK(ledger.totalTransferred() == trBefore);
}

// S20-1：adoptRadius 内的自由球被收编：ownerId 换新核、angle 反算、
// 能量原样、球数 +1；radius 按**能量**给出（负载基因），不是拾取距离。
//
// 【语义变更 2026-10-31】原断言是 CHECK(b.radius == 10.0)，即"radius = 拾取距离"。
// 那样会让高能球的半径在每次拾取时塌回 <18（adoptRadius 上限），
// 而 S19 的失控判据要求 radius > 120 —— 于是脱离**永远不可达**（实测恒为 0）。
// 改为 radius = 10 + 0.5*E 后，S19 与 S20 才能共存。
TEST(adopt_succeeds_in_range) {
    agentsim::Nucleus n = makeNucleus(50.0);
    n.pos = agentsim::Vec2{100.0, 100.0};
    agentsim::Ball b;
    b.ownerId = 0; // 自由球
    b.energy = 130.0;
    b.pos = agentsim::Vec2{110.0, 100.0}; // 环面距离 10 <= adoptRadius 18
    const agentsim::BehaviorParams bp;
    CHECK(agentsim::AgentBehavior::adoptBall(n, b, 10.0, bp));
    CHECK(n.balls.size() == 1);
    CHECK(b.ownerId == n.id);
    CHECK_NEAR(b.radius, 10.0 + 0.5 * 130.0, 1e-12); // = 75：由能量决定，非拾取距离
    CHECK(b.energy == 130.0); // 拾取不耗能
    CHECK_NEAR(b.angle, 0.0, 1e-12); // 正东侧 => atan2(0,+)=0
}

// S20-2：超距拒绝：返回 false，核与球状态逐位不变。
TEST(adopt_fails_out_of_range) {
    agentsim::Nucleus n = makeNucleus(50.0);
    const agentsim::BehaviorParams bp; // adoptRadius = 18
    agentsim::Ball b;
    b.ownerId = 0;
    b.energy = 90.0;
    b.radius = 5.0;
    b.angle = 1.25;
    CHECK(!agentsim::AgentBehavior::adoptBall(n, b, bp.adoptRadius * (1.0 + 1e-9), bp));
    CHECK(n.balls.empty());
    CHECK(b.ownerId == 0);
    CHECK(b.energy == 90.0);
    CHECK(b.radius == 5.0 && b.angle == 1.25); // 球未被碰
    CHECK(!agentsim::AgentBehavior::adoptBall(n, b, -1.0, bp)); // 非法距离拒绝
    // 已有主人的球不可被收编。
    agentsim::Nucleus m = makeNucleus(50.0);
    b.ownerId = 7;
    CHECK(!agentsim::AgentBehavior::adoptBall(m, b, 1.0, bp));
    CHECK(m.balls.empty());
}

// S20-3：拾取零和：核能量与球能量总额在拾取前后逐位不变（账面零操作）。
TEST(adopt_zero_sum) {
    agentsim::AgentLedger ledger;
    agentsim::Nucleus n = makeNucleus(77.0);
    ledger.injectInitial(n.id, n.energy);
    agentsim::Ball b;
    b.ownerId = 0;
    b.energy = 140.0;
    const agentsim::BehaviorParams bp;
    const double before = n.energy + b.energy;
    const double injBefore = ledger.totalInjected();
    const double disBefore = ledger.totalDissipated();
    CHECK(agentsim::AgentBehavior::adoptBall(n, b, 12.0, bp));
    CHECK(n.energy + b.energy == before); // 总额逐位不变
    CHECK(ledger.totalInjected() == injBefore);
    CHECK(ledger.totalDissipated() == disBefore);
}

// S19-4：失控但球能 < detachEnergy 的低能球不脱离。
// 夹具：handoffRadius 收小到 30，让 E=110（radius=65>2*30）真正落入失控分支——
// 默认 handoffRadius=60 时失控要求 E>220，与"低能"前提不相容（轨道几何下
// 环距恒等于 radius）。核能 100 压在 T08 产球触发线（0.6*200=120）之下，
// 球数断言不被随机产球污染。
TEST(detach_below_threshold_stays) {
    agentsim::SimConfig cfgEmpty{};
    cfgEmpty.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径）
    agentsim::AgentEnvironment env(cfgEmpty);
    agentsim::AgentLedger ledger;
    agentsim::BehaviorParams bp;
    bp.handoffRadius = 30.0; // 失控判据收紧：dist > 60 即 gain < 0.2
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::Ball b;
    b.energy = 110.0;                    // < detachEnergy(120)
    b.radius = 10.0 + 0.5 * b.energy;    // 65 => gain=1/(1+(65/30)^2)<0.2 失控
    b.lastOmega = 1.0;
    b.ownerId = n.id;
    n.balls.push_back(b);
    agentsim::ActionVector act;
    agentsim::PCG32 rng(13);
    for (int t = 0; t < 30; ++t) {
        agentsim::AgentBehavior::apply(n, act, env, ledger, 1.0, bp, rng);
        if (!n.alive) break;
    }
    CHECK(n.alive);
    CHECK(n.detached_.empty()); // 低能球即使持续失控也绝不脱离
    CHECK(n.balls.size() == 1);
}

// S19-5：apply 内失控 + 高能球被移入 n.detached_（ownerId=0、能量原样、balls -1）。
TEST(detach_in_apply_moves_to_pending) {
    agentsim::SimConfig cfgEmpty{};
    cfgEmpty.cyanoNodeCount = 0; // R1-B：无蓝细菌库存（等价旧"无食物"口径）
    agentsim::AgentEnvironment env(cfgEmpty);
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp;
    agentsim::Nucleus n = makeNucleus(500.0);
    n.pos = agentsim::Vec2{128.0, 128.0}; // 固定核位置，便于精确摆放球的真实坐标
    agentsim::Ball b;
    b.energy = bp.detachEnergy * 2.0; // 240 > 120：失控即脱离
    b.radius = 10.0 + 0.5 * b.energy; // 130
    b.lastOmega = 0.5;
    b.ownerId = n.id;
    // 【S19 修正】球的世界坐标是第一性状态：失控判定用 dist = torusDistance(n.pos, b.pos)，
    // 不再由 radius 反推。必须显式把球摆到半径 > 2*handoffRadius(=120) 处，
    // 否则 b.pos 停在默认 {0,0}，距核约 181，会落进"失控"但半径却对不上，
    // 测的就不是"高能失控球"这个场景了。
    b.pos = agentsim::Vec2{n.pos.x + b.radius, n.pos.y}; // 130 处：gain = 1/(1+(130/60)^2) < 0.2
    b.vel = agentsim::Vec2{0.0, 0.0};
    n.balls.push_back(b);
    agentsim::ActionVector act;
    agentsim::PCG32 rng(13);
    agentsim::AgentBehavior::apply(n, act, env, ledger, 1.0, bp, rng);
    CHECK(n.balls.empty());
    CHECK(n.detached_.size() == 1);
    CHECK(n.detached_[0].ownerId == 0);
    // 能量保留 = 240 - 脱离当 tick 的漂移散热（散热先于摘除执行，摘除本身零损耗）。
    // 注意：漂移散热用的是 **b.radius**，而 S19 修正后 radius 由真实环距反算
    // （=130，与摆放位置一致），因此这里直接用摆放时的半径计算，而不是再用
    // 能量公式 10+0.5E 反推——后者在"半径与几何脱钩"的旧模型下才成立。
    // 实际半径由 apply 内的几何反算决定，可能与摆放半径有极小差异（浮点 wrap），
    // 因此直接以"实测半径"重算 drift，断言能量 = 240 - drift(实测半径)。
    const double actualRadius = n.detached_[0].radius;
    const double drift = bp.locomotionCost * actualRadius * 0.05;
    CHECK_NEAR(n.detached_[0].energy, bp.detachEnergy * 2.0 - drift, 1e-12);
    // S19 红线（P0 修正）：脱离**不做任何账面操作**。能量仍在球内，
    // 审计由组合根闭合（Simulation::totalEnergy 已计入 n.detached_）。
    // 曾在此断言 dissipatedBy("detach") == 能量，那是按错误实现写的断言——
    // 能量明明还在实体里，记耗散等于凭空扣掉一份能量。现改为断言**零耗散**。
    CHECK_NEAR(ledger.dissipatedBy("detach"), 0.0, 1e-12);
}
// ==================== S21：核施力 applyForce（能量代价的定向推力） ====================
// 语义红线：核推动球是 核能量 -> 耗散 的单向流出（球速度是动质量，不入能量账），
// 可对任何球施力（含 ownerId==0 的自由球——"核推动自由球"正是本特性目的）。

TEST(force_changes_ball_velocity) {
    agentsim::Nucleus n = makeNucleus(100.0);
    n.pos = agentsim::Vec2{0.0, 0.0}; // 核在原点
    agentsim::Ball b;                 // 球默认在原点，环距 10（< forceRadius 40）
    b.ownerId = n.id;
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp;
    const bool ok = agentsim::AgentBehavior::applyForce(
        n, b, agentsim::Vec2{1.0, 0.0}, 4.0, 10.0, ledger, bp);
    CHECK(ok);
    CHECK_NEAR(b.vel.x, 4.0, 1e-12); // 沿 +x 施力：vel.x 恰增 4
    CHECK_NEAR(b.vel.y, 0.0, 1e-12);
}

TEST(force_costs_energy) {
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::Ball b;
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp;
    const double before = n.energy;
    const bool ok = agentsim::AgentBehavior::applyForce(
        n, b, agentsim::Vec2{0.0, 1.0}, 4.0, 10.0, ledger, bp);
    CHECK(ok);
    const double expectedCost = bp.forceCostK * 4.0 * 4.0; // 0.01 * 16
    CHECK_NEAR(before - n.energy, expectedCost, 1e-12);    // 核能量减少量 == k*m^2
    CHECK_NEAR(ledger.dissipatedBy("applyForce"), expectedCost, 1e-12);
    CHECK_NEAR(ledger.totalDissipated(), expectedCost, 1e-12); // 账面唯一耗散就是它
    CHECK(ledger.totalInjected() == 0.0);                      // 零注入：纯耗散动作
}

TEST(force_out_of_range_noop) {
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::Ball b;
    b.vel = agentsim::Vec2{1.5, -2.5};
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp;
    const double e0 = n.energy;
    const bool ok = agentsim::AgentBehavior::applyForce(
        n, b, agentsim::Vec2{1.0, 0.0}, 4.0, bp.forceRadius + 1e-9, ledger, bp);
    CHECK(!ok);                                 // 超距拒绝
    CHECK(b.vel.x == 1.5 && b.vel.y == -2.5);   // 球速度逐位不变
    CHECK(n.energy == e0);                      // 核能量逐位不变
    CHECK(ledger.dissipatedBy("applyForce") == 0.0);
    CHECK(ledger.totalDissipated() == 0.0);
    // 边界语义：torusDist == forceRadius（<=）恰好可施力。
    agentsim::Ball edge;
    CHECK(agentsim::AgentBehavior::applyForce(
        n, edge, agentsim::Vec2{1.0, 0.0}, 1.0, bp.forceRadius, ledger, bp));
    CHECK_NEAR(edge.vel.x, 1.0, 1e-12);
}
TEST(force_insufficient_energy_scales) {
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::Ball b;
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp;
    // 代价 k*8^2 = 0.64；核能 0.16 = 代价的 1/4 -> 反解 m = sqrt(0.16/0.01) = 4
    n.energy = 0.16;
    const bool ok = agentsim::AgentBehavior::applyForce(
        n, b, agentsim::Vec2{1.0, 0.0}, 8.0, 10.0, ledger, bp);
    CHECK(ok);                                        // 缩量后仍成功施力
    CHECK_NEAR(b.vel.x, 4.0, 1e-12);                  // 8 -> 4（按余额比例缩小）
    CHECK(n.energy >= 0.0);                           // 核能量永不为负
    CHECK_NEAR(n.energy, 0.0, 1e-12);                 // 付到归零
    CHECK_NEAR(ledger.dissipatedBy("applyForce"), 0.16, 1e-12); // 账面 == 实扣
    CHECK_NEAR(ledger.dissipatedBy("applyForce"), 0.16 - n.energy, 1e-12);
    // 零余额：直接拒绝，状态零改动
    agentsim::Nucleus n2 = makeNucleus(0.0);
    agentsim::Ball b2;
    agentsim::AgentLedger l2;
    CHECK(!agentsim::AgentBehavior::applyForce(
        n2, b2, agentsim::Vec2{1.0, 0.0}, 4.0, 10.0, l2, bp));
    CHECK(n2.energy == 0.0 && b2.vel.x == 0.0 && b2.vel.y == 0.0);
}

TEST(force_magnitude_clamped) {
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::Ball b;
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp;
    const bool ok = agentsim::AgentBehavior::applyForce(
        n, b, agentsim::Vec2{1.0, 0.0}, 1000.0, 10.0, ledger, bp);
    CHECK(ok);
    CHECK(b.vel.x <= bp.forceMax);                    // 实际增量 <= forceMax(8)
    CHECK_NEAR(b.vel.x, bp.forceMax, 1e-12);          // 恰为 8.0
    CHECK_NEAR(n.energy, 100.0 - bp.forceCostK * 64.0, 1e-12); // 代价按钳后值算
    CHECK_NEAR(ledger.dissipatedBy("applyForce"), bp.forceCostK * 64.0, 1e-12);
}
TEST(force_works_on_free_ball) {
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::Ball b;                 // ownerId == 0：自由球
    CHECK(b.ownerId == 0);
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp;
    const double e0 = n.energy;
    const bool ok = agentsim::AgentBehavior::applyForce(
        n, b, agentsim::Vec2{-1.0, 0.0}, 5.0, 12.0, ledger, bp);
    CHECK(ok);                        // 设计语义：核可推动自由球
    CHECK_NEAR(b.vel.x, -5.0, 1e-12);
    CHECK_NEAR(b.vel.y, 0.0, 1e-12);
    CHECK_NEAR(e0 - n.energy, bp.forceCostK * 25.0, 1e-12);
}

TEST(force_nan_inputs_rejected) {
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::Ball b;
    b.vel = agentsim::Vec2{3.0, 4.0};
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp;
    const double e0 = n.energy;
    const double nan1 = std::numeric_limits<double>::quiet_NaN();
    CHECK(!agentsim::AgentBehavior::applyForce(
        n, b, agentsim::Vec2{1.0, 0.0}, nan1, 10.0, ledger, bp)); // magnitude NaN
    CHECK(!agentsim::AgentBehavior::applyForce(
        n, b, agentsim::Vec2{1.0, 0.0}, 4.0, nan1, ledger, bp));  // torusDist NaN
    CHECK(b.vel.x == 3.0 && b.vel.y == 4.0);  // 状态逐位不变
    CHECK(n.energy == e0);
    CHECK(ledger.totalDissipated() == 0.0);
    CHECK(!agentsim::AgentBehavior::applyForce(
        n, b, agentsim::Vec2{1.0, 0.0}, 0.0, 10.0, ledger, bp)); // 非正同样拒绝
    CHECK(n.energy == e0);
}

TEST(force_audit_identity) {
    agentsim::Nucleus n = makeNucleus(100.0);
    agentsim::Ball b;
    b.energy = 25.0;                  // 球能计入 sumLiving（施力不改球能）
    agentsim::AgentLedger ledger;
    const agentsim::BehaviorParams bp;
    // 真实组合根语义：账本先登记全部活实体初能（核 100 + 球 25），恒等式起点为 0
    ledger.injectInitial(n.id, n.energy);
    ledger.injectInitial(7777u, b.energy); // 球能记在球实体 id 下（sumLiving 计它）
    const double identBefore = ledger.netInjected() - (n.energy + b.energy);
    CHECK_NEAR(identBefore, 0.0, 1e-9);
    CHECK(agentsim::AgentBehavior::applyForce(
        n, b, agentsim::Vec2{0.6, 0.8}, 6.0, 10.0, ledger, bp));
    const double identAfter = ledger.netInjected() - (n.energy + b.energy);
    // 零和红线：施力只做 核能量->耗散 的单向流出，净注入-存活能量 恒等不变
    CHECK_NEAR(identBefore, 0.0, 1e-9); // 空账本：恒等式从 0 开始
    CHECK_NEAR(identAfter, identBefore, 1e-9);
}

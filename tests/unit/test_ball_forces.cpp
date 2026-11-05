// agentsim - tests/unit/test_ball_forces.cpp
// R7-B 球力场模型单测（任务卡 R7-B，10 个用例）。
// 公式参考：旧版 v2.3.1 World.cpp updateBallDynamics(274-393) / updateOwnership(393-430)。
// 夹具约定：Nucleus 用默认 genome（affinity=1/3、orbitRadius=120、repelStrength=1.5），
//   负载半径 = 10 + 0.5*球能，轨道目标 = 负载 × (0.5 + 120/240) = 负载 × 1.0。
//   球能 10 → 轨道目标 15；近距排斥半径 25；球间排斥 10；同核吸引 40（显式开）。
#include "test_framework.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../../src/behavior/AgentBehavior.h"
#include "../../src/core/Config.h"
#include "../../src/core/PCG32.h"
#include "../../src/core/Types.h"
#include "../../src/environment/AgentEnvironment.h"
#include "../../src/ledger/AgentLedger.h"
#include "../../src/sim/Simulation.h"

namespace {

using agentsim::AgentBehavior;
using agentsim::Ball;
using agentsim::BallFieldParams;
using agentsim::BallType;
using agentsim::BehaviorParams;
using agentsim::Nucleus;
using agentsim::Vec2;

Nucleus makeNucleus(double energy, Vec2 pos = Vec2{128.0, 128.0}) {
    Nucleus n;
    n.id = 1;
    n.pos = pos;
    n.energy = energy;
    n.alive = true;
    return n;
}

Ball makeBall(double energy, Vec2 pos) {
    Ball b;
    b.energy = energy;
    b.pos = pos;
    b.ownerId = 1;
    return b;
}

BallFieldParams tuned() {
    BallFieldParams p; // v231 默认：radialK .03 / tangentialK 2 / nearRepel 25
    p.sameOwnerAttractRadius = 40.0; // 显式开吸引（组合根生产口径）
    p.sameOwnerAttractK = 0.15;
    return p;
}

} // namespace

// 1) 径向弹簧：dist > 轨道目标 受向内力，< 时向外（v231:305）。
TEST(ball_radial_spring_pulls_inward_beyond_orbit) {
    const BallFieldParams p = tuned();
    const Nucleus n = makeNucleus(100.0);
    const double aff = 1.0 / 3.0;
    // 轨道目标 target = 负载(15) × (0.5 + 120/240) = 15（默认基因、球能 10）。
    const double target = 15.0;
    // dist=30（> 目标、> 近距排斥 25）：纯弹簧，向内（x 负）。
    // odisp = {0,0}：非跟随场景（调用方约定：odisp 仅在 src 即宿主时非零）。
    const Vec2 out = AgentBehavior::ballFieldAccel(n, makeBall(10.0, Vec2{}),
                                                   Vec2{30.0, 0.0}, Vec2{0.0, 0.0}, 1.0, p);
    CHECK_NEAR(out.x, -aff * p.radialK * (30.0 - target), 1e-12);
    CHECK(out.x < 0.0);
    // dist=5（< 目标）：弹簧向外（近距排斥也向外，两力同向叠加）。
    const Vec2 in = AgentBehavior::ballFieldAccel(n, makeBall(10.0, Vec2{}),
                                                  Vec2{5.0, 0.0}, Vec2{0.0, 0.0}, 1.0, p);
    CHECK(in.x > 0.0);
    CHECK_NEAR(in.x, -aff * p.radialK * (5.0 - target) + 1.5 * (1.0 - 5.0 / 25.0), 1e-12);
    // 线性增长：dist=60 的回拉严格强于 dist=30。
    const Vec2 far_ = AgentBehavior::ballFieldAccel(n, makeBall(10.0, Vec2{}),
                                                    Vec2{60.0, 0.0}, Vec2{0.0, 0.0}, 1.0, p);
    CHECK(-far_.x > -out.x);
    CHECK_NEAR(far_.x, -aff * p.radialK * (60.0 - target), 1e-12);
}

// 2) 切向驱动方向正确（tangent = (-dir.y, dir.x)，逆时针，v231:307）。
TEST(ball_tangential_drive_counterclockwise) {
    const BallFieldParams p = tuned();
    const Nucleus n = makeNucleus(100.0);
    // 球在核正东 dist=15（= 轨道目标：径向弹簧为 0；但 15 < 近距排斥 25，
    // 排斥仍触发）。dir=(1,0)，切向=(0,1) 向北推。
    const Vec2 east = AgentBehavior::ballFieldAccel(n, makeBall(10.0, Vec2{}),
                                                    Vec2{15.0, 0.0}, Vec2{15.0, 0.0}, 1.0, p);
    CHECK_NEAR(east.x, 1.5 * (1.0 - 15.0 / 25.0), 1e-12); // 排斥 0.6 向外
    CHECK_NEAR(east.y, (1.0 / 3.0) * p.tangentialK, 1e-12); // 切向 2/3 向北（逆时针）
    // 球在核正北（dir=(0,1)）：切向 = (-dir.y, dir.x) = (-1,0) 向西推。
    const Vec2 north = AgentBehavior::ballFieldAccel(n, makeBall(10.0, Vec2{}),
                                                     Vec2{0.0, 15.0}, Vec2{0.0, 0.0}, 1.0, p);
    CHECK_NEAR(north.x, -(1.0 / 3.0) * p.tangentialK, 1e-12);
    CHECK(north.y > 0.0); // 弹簧+排斥沿 dir 向外（y 正）
}

// 3) 近距排斥随距离衰减（线性 1 - dist/R，v231:309-313）。
TEST(ball_near_repel_decays_with_distance) {
    const BallFieldParams p = tuned();
    const Nucleus n = makeNucleus(100.0);
    const double aff = 1.0 / 3.0;
    const double target = 15.0;
    // 在弹簧解析扣除后，排斥必须随 dist 单调衰减。
    double prevRepel = 1e300;
    for (double dist = 2.0; dist < 25.0; dist += 3.0) {
        const Vec2 acc = AgentBehavior::ballFieldAccel(
            n, makeBall(10.0, Vec2{}), Vec2{dist, 0.0}, Vec2{0.0, 0.0}, 1.0, p);
        const double spring = -aff * p.radialK * (dist - target); // 可为正（向内拉）
        const double repel = acc.x - spring;                       // 扣除弹簧余项 = 排斥
        CHECK_NEAR(repel, 1.5 * (1.0 - dist / 25.0), 1e-12);
        CHECK(repel < prevRepel); // 随距离单调衰减
        prevRepel = repel;
    }
    // 半径外（dist=30 > 25）：无排斥。
    const Vec2 accOut = AgentBehavior::ballFieldAccel(
        n, makeBall(10.0, Vec2{}), Vec2{30.0, 0.0}, Vec2{0.0, 0.0}, 1.0, p);
    CHECK_NEAR(accOut.x, -aff * p.radialK * (30.0 - target), 1e-12);
}

// 4) 球间排斥与同核吸引（v231:354-362）。
TEST(ball_pair_repel_and_same_owner_attract) {
    const BallFieldParams p = tuned();
    const Ball self = makeBall(10.0, Vec2{100.0, 100.0});
    // 4a) 排斥：dist=5 < ballRepelRadius=10。dir = o->b = (-1,0)（o 在东侧），
    // 推离 = 沿 dir = b 向西退（x 负），模 0.6。
    const Ball other = makeBall(10.0, Vec2{105.0, 100.0});
    const Vec2 acc = AgentBehavior::ballPairAccel(self, other, Vec2{-5.0, 0.0}, 1.0, p);
    CHECK_NEAR(acc.x, -p.ballRepelK * (1.0 - 5.0 / 10.0), 1e-12);
    CHECK(acc.x < 0.0);
    // 4b) 同核吸引：dist=20 ∈ (10, 40)、同宿主 → 拉近（-0.075）。
    const Ball mate = makeBall(10.0, Vec2{120.0, 100.0});
    const Vec2 acc2 = AgentBehavior::ballPairAccel(self, mate, Vec2{-20.0, 0.0}, 1.0, p);
    CHECK_NEAR(acc2.x, p.sameOwnerAttractK * (1.0 - 20.0 / 40.0), 1e-12); // 拉近 = +0.075（朝东的 o）
    CHECK(acc2.x > 0.0);
    // 4c) 异宿主不吸引。
    Ball stranger = makeBall(10.0, Vec2{120.0, 100.0});
    stranger.ownerId = 999;
    const Vec2 acc3 = AgentBehavior::ballPairAccel(self, stranger, Vec2{-20.0, 0.0}, 1.0, p);
    CHECK(acc3.x == 0.0 && acc3.y == 0.0);
    // 4d) 自由球（ownerId=0）互不吸引（0==0 相等不算同宿主）。
    Ball f1 = makeBall(10.0, Vec2{100.0, 100.0});
    Ball f2 = makeBall(10.0, Vec2{120.0, 100.0});
    f1.ownerId = 0;
    f2.ownerId = 0;
    const Vec2 acc4 = AgentBehavior::ballPairAccel(f1, f2, Vec2{-20.0, 0.0}, 1.0, p);
    CHECK(acc4.x == 0.0 && acc4.y == 0.0);
    // 4e) 半径外全零。
    const Ball far_ = makeBall(10.0, Vec2{200.0, 100.0});
    const Vec2 acc5 = AgentBehavior::ballPairAccel(self, far_, Vec2{-100.0, 0.0}, 1.0, p);
    CHECK(acc5.x == 0.0 && acc5.y == 0.0);
}

// 5) 类型限速三档不同（Shield < Resource < Scout，v231:370-383）。
TEST(ball_type_max_speed_tiers) {
    const BallFieldParams p = tuned();
    Nucleus n = makeNucleus(100.0);
    n.speed = 0.0;
    Ball shield = makeBall(10.0, Vec2{100.0, 100.0});
    shield.type = BallType::Shield;
    shield.vel = Vec2{1000.0, 0.0};
    Ball resource = shield;
    resource.type = BallType::Resource;
    Ball scout = shield;
    scout.type = BallType::Scout;
    const double vS = AgentBehavior::ballWorldIntegrate(shield, Vec2{0, 0}, 1.0, &n, 256.0, 256.0, p);
    const double vR = AgentBehavior::ballWorldIntegrate(resource, Vec2{0, 0}, 1.0, &n, 256.0, 256.0, p);
    const double vC = AgentBehavior::ballWorldIntegrate(scout, Vec2{0, 0}, 1.0, &n, 256.0, 256.0, p);
    CHECK(vS < vR);
    CHECK(vR < vC);
    CHECK_NEAR(vS, p.maxSpeedShield, 1e-9);
    CHECK_NEAR(vR, p.maxSpeedResource, 1e-9);
    CHECK_NEAR(vC, p.maxSpeedScout, 1e-9);
    // 跟随加速：宿主 speed=40 × boost 1.5 = 60 → Resource 球上限抬到 60。
    n.speed = 40.0;
    Ball r2 = resource;
    r2.vel = Vec2{1000.0, 0.0};
    const double vR2 = AgentBehavior::ballWorldIntegrate(r2, Vec2{0, 0}, 1.0, &n, 256.0, 256.0, p);
    CHECK_NEAR(vR2, 40.0 * p.followBoost, 1e-9);
}

// 6) followCoupling 让快核甩不掉球（强化弹簧，v231:318-336）。
TEST(ball_follow_coupling_keeps_ball_with_fast_nucleus) {
    BallFieldParams p = tuned();
    p.detachRadius = 60.0;
    const Nucleus n = makeNucleus(100.0);
    const double odist = 50.0;
    const double aff = 1.0 / 3.0;
    const double target = 15.0;
    const Ball b = makeBall(10.0, Vec2{});
    const double baseSpring = -aff * (odist - target) * p.radialK;
    const Vec2 withCoupling = AgentBehavior::ballFieldAccel(
        n, b, Vec2{odist, 0.0}, Vec2{odist, 0.0}, 1.0, p);
    CHECK_NEAR(withCoupling.x, baseSpring - aff * (odist - target) * (p.radialK * p.followCoupling), 1e-12);
    CHECK(withCoupling.x < baseSpring); // 更强的向内回拉
    // 耦合关闭：只剩基础弹簧。
    BallFieldParams pOff = p;
    pOff.followCoupling = 0.0;
    const Vec2 noCoupling = AgentBehavior::ballFieldAccel(
        n, b, Vec2{odist, 0.0}, Vec2{odist, 0.0}, 1.0, pOff);
    CHECK_NEAR(noCoupling.x, baseSpring, 1e-12);
    // 脱附半径外（odisp=0 或 odist=61 > detachRadius 60）：耦合不生效
    //（v231:326 odSq < det2 裁剪）。dist=61 > 近距排斥 25，纯弹簧：
    // spring(61) = -aff × radialK × (61 - 15) = -0.46。
    BallFieldParams pOut = p;
    pOut.followCoupling = 15.0;
    const double spring61 = -aff * p.radialK * (61.0 - target);
    const Vec2 outside = AgentBehavior::ballFieldAccel(
        n, b, Vec2{61.0, 0.0}, Vec2{0.0, 0.0}, 1.0, pOut);
    CHECK_NEAR(outside.x, spring61, 1e-12);
    // 同 dist=61 但 odisp 也给 61（> detachRadius）：耦合同样不生效。
    const Vec2 outside2 = AgentBehavior::ballFieldAccel(
        n, b, Vec2{61.0, 0.0}, Vec2{61.0, 0.0}, 1.0, pOut);
    CHECK_NEAR(outside2.x, spring61, 1e-12);
    // 对照：odisp 非 0 且在半径内（50 < 60）时耦合生效，回拉更强。
    const Vec2 inside = AgentBehavior::ballFieldAccel(
        n, b, Vec2{50.0, 0.0}, Vec2{50.0, 0.0}, 1.0, pOut);
    CHECK(inside.x < outside.x);
}

// 7) 归属换主扣 ballLossCost 且审计平衡（v231 updateOwnership + 零和红线）。
TEST(ownership_transfer_debits_ball_loss_and_audits) {
    BallFieldParams p = tuned();
    p.lossCost = 5.0;
    Nucleus from = makeNucleus(50.0);
    from.id = 1;
    Nucleus to = makeNucleus(50.0);
    to.id = 2;
    to.pos = Vec2{130.0, 128.0};
    std::vector<Nucleus> population;
    population.push_back(from);
    population.push_back(to);
    std::vector<Ball> freeBalls;
    agentsim::AgentLedger ledger;
    ledger.injectInitial(1, 50.0);
    ledger.injectInitial(2, 50.0);
    Ball b = makeBall(20.0, Vec2{129.0, 128.0}); // 离 to 更近
    population[0].balls.push_back(b);
    ledger.injectInitial(b.id, 20.0); // 球能入账（审计恒等式的存活实体侧）
    const double livingBefore = population[0].energy + population[1].energy + 20.0;
    const bool moved = AgentBehavior::reassignBall(
        population[0], 0, 1, population, freeBalls, ledger, p); // best=1（核 to）
    CHECK(moved);
    CHECK(population[0].balls.empty());
    CHECK(population[1].balls.size() == 1);
    CHECK(population[1].balls[0].ownerId == 2);
    CHECK(population[1].balls[0].energy == 20.0);
    CHECK(population[1].balls[0].r7Lane); // 换主球进世界积分车道
    CHECK(freeBalls.empty());
    CHECK_NEAR(population[0].energy, 45.0, 1e-12); // 原主扣 lossCost
    CHECK_NEAR(ledger.totalDissipated(), 5.0, 1e-12);
    const double livingAfter = population[0].energy + population[1].energy +
                               population[1].balls[0].energy;
    CHECK_NEAR(livingAfter, livingBefore - 5.0, 1e-12);
    CHECK(ledger.audit(1e-9 * (1.0 + ledger.totalInjected()), livingAfter));
}

// 7b) 失主球入自由池（能量原样）+ 限额支出（能量 < lossCost 按余额扣，永不为负）。
TEST(ownership_loss_dissipates_capped_and_balls_freed) {
    BallFieldParams p = tuned();
    p.lossCost = 5.0;
    Nucleus poor = makeNucleus(3.0);
    poor.id = 3;
    std::vector<Nucleus> population;
    population.push_back(poor);
    std::vector<Ball> freeBalls;
    agentsim::AgentLedger ledger;
    ledger.injectInitial(3, 3.0);
    Ball bb = makeBall(10.0, Vec2{0.0, 0.0});
    bb.ownerId = 3;
    population[0].balls.push_back(bb);
    const bool freed = AgentBehavior::reassignBall(population[0], 0,
                                                   static_cast<std::size_t>(-1),
                                                   population, freeBalls, ledger, p);
    CHECK(freed);
    CHECK(freeBalls.size() == 1);
    CHECK(freeBalls[0].ownerId == 0);
    CHECK(freeBalls[0].energy == 10.0); // 能量原样入池
    CHECK(freeBalls[0].r7Lane);
    CHECK_NEAR(population[0].energy, 0.0, 1e-12); // min(5, 3) = 3 全扣
    CHECK_NEAR(ledger.totalDissipated(), 3.0, 1e-12);
    // 审计：injected(3) - dissipated(3) == 存活实体能量（0 + 球 10）？——不对！
    // 球能 10 从未入账：夹具球要 injectInitial 等额入账才守恒。
    // （本测试的价值在扣款限额语义；完整性审计由生态级用例 ball_world_step_
    //   eco_integration 与既有 wiring_audit_holds_long_run 把守。）
    CHECK_NEAR(ledger.totalInjected() - ledger.totalDissipated(), 0.0, 1e-12);
}

// 8) Shield 球降低宿主核呼吸代谢（respirationBase × 1/(1+0.35*n)，V2-SPEC B）。
TEST(shield_balls_reduce_respiration) {
    agentsim::SimConfig cfgEmpty{};
    cfgEmpty.cyanoNodeCount = 0;
    agentsim::AgentEnvironment envA(cfgEmpty), envB(cfgEmpty);
    agentsim::AgentLedger ledA, ledB;
    BehaviorParams bp;
    bp.moveK = 0.0; // 静止核：呼吸只剩 respirationBase × tempFactor × dt
    // 能量 100 < 0.6×nativeCap(120)：T08 产球分支不触发（触发线之上的随机
    // 产球会改变 shieldCount，污染解析断言）。
    Nucleus a = makeNucleus(100.0), b = makeNucleus(100.0);
    Ball shield = makeBall(0.0, Vec2{128.0, 128.0});
    shield.type = BallType::Shield;
    shield.ownerId = 1;
    a.balls.push_back(shield);
    agentsim::PCG32 rngA(41), rngB(41);
    agentsim::ActionVector act;
    for (int t = 0; t < 5; ++t) {
        AgentBehavior::apply(a, act, envA, ledA, 1.0, bp, rngA, nullptr, 0.0);
        AgentBehavior::apply(b, act, envB, ledB, 1.0, bp, rngB, nullptr, 0.0);
    }
    CHECK(a.alive && b.alive);
    const double base = bp.respirationBase;
    // 两核同 seed 同动作：仅 shield 差异 -> 能量差 = 5 × base × (1 - 1/1.35)。
    CHECK_NEAR(a.energy - b.energy, 5.0 * base * (1.0 - 1.0 / 1.35), 1e-9);
    CHECK(a.energy > b.energy);
    // 解析：b（无 shield）恰好 5 × base 呼吸。
    CHECK_NEAR(b.energy, 100.0 - 5.0 * base, 1e-9);
}

// 9) wrap 积分连续性：跨界球位置 wrap、速度连续（环面语义，不反弹）。
TEST(ball_world_integrate_wraps_torus) {
    const BallFieldParams p = tuned();
    Nucleus n = makeNucleus(100.0);
    n.speed = 0.0;
    Ball b = makeBall(10.0, Vec2{255.0, 128.0});
    b.vel = Vec2{10.0, 0.0};
    b.ownerId = 0;
    (void)AgentBehavior::ballWorldIntegrate(b, Vec2{0, 0}, 1.0, nullptr, 256.0, 256.0, p);
    CHECK(b.pos.x < 256.0);
    CHECK_NEAR(b.pos.x, 255.0 + 10.0 * p.damping - 256.0, 1e-9);
    CHECK_NEAR(b.vel.x, 10.0 * p.damping, 1e-9); // 阻尼后速度连续（无反弹翻转）
    CHECK(b.vel.x > 0.0);
}

// 10) 生态级：默认配置 500 tick，审计闭合 + 世界积分车道球有界运动 + 存在 R7 车道球。
TEST(ball_world_step_eco_integration) {
    agentsim::SimConfig cfg{};
    cfg.world.seed = 42;
    cfg.world.width = 256.0; cfg.world.height = 256.0;  // R8-T53：钉 256（断言 wrap 边界，与默认世界尺寸解耦）
    agentsim::Simulation sim(cfg);
    CHECK(sim.runTo(500));
    CHECK(sim.auditOk());
    bool sawBall = false;
    for (const Nucleus& n : sim.nuclei()) {
        for (const Ball& b : n.balls) {
            if (!b.r7Lane) continue;
            sawBall = true;
            CHECK(b.pos.x >= 0.0 && b.pos.x < 256.0);
            CHECK(b.pos.y >= 0.0 && b.pos.y < 256.0);
            const double sp = std::sqrt(b.vel.x * b.vel.x + b.vel.y * b.vel.y);
            CHECK(sp <= 90.0 + 1e-6); // 三档限速上界 80 + 跟随加速余量
        }
    }
    CHECK(sawBall); // 500 tick 内 T08 必然产球（0.1/tick/核 × 12 核）
}

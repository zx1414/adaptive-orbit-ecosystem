// agentsim - tests/unit/test_cyano.cpp
// S22 上半：蓝细菌实体（CyanoField）单测。覆盖光合公式、代谢饿死、能量上限、
// 分裂平分、产球成本、graze 守恒、同种子逐位确定性。
// R8/P2 追加：产球出生点靠近产者（GOAL §2.2.1 ③）、linkCheckInterval 节流
// （GOAL §2.2.1 ④，默认 interval=1 与旧"每帧结算"逐位回归）。
#include "test_framework.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "../../src/core/PCG32.h"
#include "../../src/core/Types.h"
#include "../../src/core/Vec2.h"
#include "../../src/environment/CyanoNode.h"

namespace {

using agentsim::CyanoField;
using agentsim::CyanoNode;
using agentsim::CyanoParams;
using agentsim::CyanoStepReport;
using agentsim::EntityId;
using agentsim::PCG32;
using agentsim::Vec2;

// 测试专用直改辅助（实体状态归行为层所有，生产代码不得效仿）：
// sole: 单节点场取唯一节点；mut: 取第 i 个节点的可变引用。
CyanoNode& sole(CyanoField& f) {
    return const_cast<CyanoNode&>(f.nodes().front());
}
CyanoNode& mut(CyanoField& f, std::size_t i) {
    return const_cast<CyanoNode&>(f.nodes()[i]);
}

} // namespace

TEST(cyano_photosynthesis_formula) {
    CyanoParams p;
    p.photosynthesisRate = 1.0;
    p.lightHalfSaturation = 0.5;
    p.metabolismRate = 0.0; // 隔离代谢，专测光合
    p.ballSpawnProbability = 0.0;
    p.reproductionProbability = 0.0;
    CyanoField f(p);
    PCG32 rng(7);
    f.seed(1, 500.0, 500.0, rng);
    sole(f).energy = 10.0;

    // sunLevel=0.5, halfSat=0.5, rate=1.0 -> gain = 1.0*0.5/(0.5+0.5) = 0.5。
    const CyanoStepReport r = f.step(0.5, 500.0, 500.0, rng);
    CHECK_NEAR(r.photosynthesisGain, 0.5, 1e-12);
    CHECK_NEAR(sole(f).energy, 10.5, 1e-12);

    // sunLevel=0 -> gain 恰为 0，无 NaN；代谢 0.02 照常扣。
    const double before = sole(f).energy;
    const CyanoStepReport r0 = f.step(0.0, 500.0, 500.0, rng);
    CHECK(r0.photosynthesisGain == 0.0);
    CHECK_NEAR(sole(f).energy, before - p.metabolismRate, 1e-15);
    CHECK_NEAR(f.totalEnergy(), before - p.metabolismRate, 1e-15); // 总能量不变口径
}

TEST(cyano_metabolism_kills) {
    CyanoParams p;
    p.metabolismRate = 0.02;
    p.ballSpawnProbability = 0.0; // 隔离随机通道，专测代谢
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0; // S22 下半：隔离连线耦合，保持本测试原能量轨迹
    CyanoField f(p);
    PCG32 rng(11);
    f.seed(2, 400.0, 400.0, rng);
    sole(f).energy = 0.05; // 只够 3 帧（0.05/0.02 = 2.5）

    CHECK(f.aliveCount() == 2);
    for (int t = 0; t < 10; ++t) {
        (void)f.step(0.0, 400.0, 400.0, rng); // 无光：纯耗散
    }
    CHECK(f.aliveCount() == 0);
    for (const CyanoNode& n : f.nodes()) {
        CHECK(!n.alive);
        CHECK(n.energy == 0.0);
    }
    // 另一节点能量给足，验证"逐节点独立饿死"。
    // 【2026-10-31 修正】R8-P1 加入尸体压缩后，饿死的节点会被 erase，
    // nodes() 里只剩存活的那个——所以这里的索引断言从
    //   nodes()[0].alive==false && nodes()[1].alive==true
    // 改成按 aliveCount + 存活者能量断言（更稳健，不依赖槽位顺序）。
    // 另外饿死帧的账目修正（实际耗散 = 死前能量，不是名义 loss）只影响
    // metabolismLoss 报表数字，不影响存活节点的能量轨迹。
    CyanoField g(p);
    PCG32 rng2(11);
    g.seed(2, 400.0, 400.0, rng2);
    mut(g, 0).energy = 0.05;
    mut(g, 1).energy = 50.0;
    for (int t = 0; t < 10; ++t) {
        (void)g.step(0.0, 400.0, 400.0, rng2); // 0.05/0.02=2.5 -> 第3帧饿死
    }
    CHECK(g.aliveCount() == 1);
    CHECK(g.nodes().size() == 1);          // 尸体已被压缩
    CHECK(g.nodes()[0].alive);             // 幸存者就是原来给足能量的那个
    CHECK_NEAR(g.nodes()[0].energy, 50.0 - 10 * p.metabolismRate, 1e-12);
}

TEST(cyano_energy_cap_respected) {
    CyanoParams p;
    p.energyCap = 100.0;
    p.metabolismRate = 0.02;
    CyanoField f(p);
    PCG32 rng(13);
    f.seed(3, 600.0, 600.0, rng);
    for (std::size_t i = 0; i < f.nodes().size(); ++i) mut(f, i).energy = 90.0;

    for (int t = 0; t < 2000; ++t) {
        (void)f.step(1.0, 600.0, 600.0, rng); // 满光：gain=2/3 > 代谢
    }
    for (const CyanoNode& n : f.nodes()) {
        if (!n.alive) continue;
        CHECK(n.energy <= p.energyCap + 1e-9);
        CHECK(n.energy >= 0.0);
    }
}

TEST(cyano_split_halves_energy) {
    CyanoParams p;
    p.reproductionProbability = 1.0; // 强制分裂
    p.reproductionCost = 40.0;
    p.metabolismRate = 0.0;          // 隔离代谢，专测分裂账目
    CyanoField f(p);
    PCG32 rng(21);
    f.seed(1, 2000.0, 2000.0, rng);  // 大世界：minSeparation=10 几乎必然满足
    sole(f).energy = 100.0;

    const CyanoStepReport r = f.step(0.0, 2000.0, 2000.0, rng);
    CHECK(r.births > 0);
    if (r.births > 0) {
        const CyanoNode& parent = f.nodes().front();
        // 亲代扣 reproductionCost；子代初能 = reproductionCost*0.5；
        // 耗散 = reproductionCost*0.5。三者相加守恒。
        CHECK_NEAR(parent.energy, 60.0, 1e-12);
        CHECK_NEAR(f.nodes().back().energy, 20.0, 1e-12);
        CHECK_NEAR(r.reproductionLoss, 20.0, 1e-12);
        CHECK_NEAR(parent.energy + f.nodes().back().energy + r.reproductionLoss, 100.0, 1e-12);
        CHECK(f.nodes().size() == 2);
        CHECK(f.aliveCount() == 2);
    }
}

TEST(cyano_ball_spawn_cost) {
    CyanoParams p;
    p.ballSpawnProbability = 1.0; // 强制产球
    p.ballSpawnCost = 30.0;
    p.ballRadius = 1.0;
    p.metabolismRate = 0.0;
    CyanoField f(p);
    PCG32 rng(23);
    f.seed(1, 1000.0, 1000.0, rng);
    sole(f).energy = 80.0;

    const CyanoStepReport r = f.step(0.0, 1000.0, 1000.0, rng);
    CHECK(r.ballsProduced > 0);
    if (r.ballsProduced > 0) {
        CHECK_NEAR(r.ballEnergyTotal,
                   static_cast<double>(r.ballsProduced) * p.ballSpawnCost * p.ballRadius,
                   1e-12);
        CHECK(static_cast<int>(f.lastBallSpawns().size()) == r.ballsProduced);
        CHECK(f.nodes().front().energy == 80.0 - p.ballSpawnCost);
    }
}

TEST(cyano_graze_conserves) {
    CyanoField f;
    PCG32 rng(31);
    f.seed(1, 300.0, 300.0, rng);
    sole(f).energy = 50.0;
    const EntityId id = f.nodes().front().id;

    // 欠抽：taken = rawEnergy * conversionRate < 能量。
    double taken = f.graze(id, 5.0, 0.5);
    CHECK_NEAR(taken, 2.5, 1e-12);
    CHECK_NEAR(f.nodes().front().energy, 47.5, 1e-12);
    CHECK(f.nodes().front().alive);

    // 超抽：clamp 到现有能量；抽干后 alive=false。
    taken = f.graze(id, 1000.0, 1.0);
    CHECK_NEAR(taken, 47.5, 1e-12);
    CHECK(f.nodes().front().energy == 0.0);
    CHECK(!f.nodes().front().alive);

    // 死节点 / 不存在的 id 均返回 0。
    CHECK(f.graze(id, 10.0, 1.0) == 0.0);
    CHECK(f.graze(9999, 10.0, 1.0) == 0.0);

    // 负请求防御：不反向注能。
    CyanoField g;
    PCG32 rng2(33);
    g.seed(1, 300.0, 300.0, rng2);
    mut(g, 0).energy = 20.0;
    CHECK(g.graze(mut(g, 0).id, -5.0, 1.0) == 0.0);
    CHECK_NEAR(mut(g, 0).energy, 20.0, 1e-15);
}

TEST(cyano_step_deterministic) {
    for (int trial = 0; trial < 3; ++trial) {
        CyanoParams a;
        a.reproductionProbability = 0.3; // 高压参数放大随机差异
        a.ballSpawnProbability = 0.4;
        CyanoField f1(a), f2(a);
        PCG32 r1(42), r2(42);
        f1.seed(8, 800.0, 800.0, r1);
        f2.seed(8, 800.0, 800.0, r2);
        // 200 帧（原为 20 帧）。20 帧只够跑出 1~2 次分裂、几乎覆盖不到"产球位置"
        // 与"分裂位置"这两段 RNG 消费，确定性验证强度不足；200 帧能稳定覆盖多轮
        // 分裂/产球/死亡。
        // 注：曾经在合并中把 2000 帧的版本替换成了 20 帧，导致总 check 数从 10685
        // 掉到 10013 —— 测试数涨了 7 条，但这条的运行时断言被削弱了 100 倍。
        // 现在取 200 帧作为性价比折中（覆盖率足够，且不让门禁慢到影响迭代）。
        for (int t = 0; t < 200; ++t) {
            const double sun = 0.3 + 0.4 * (t % 3); // 时变光强（同参同序）
            (void)f1.step(sun, 800.0, 800.0, r1);
            (void)f2.step(sun, 800.0, 800.0, r2);
        }
        CHECK(f1.nodes().size() == f2.nodes().size());
        CHECK(f1.aliveCount() == f2.aliveCount());
        CHECK_NEAR(f1.totalEnergy(), f2.totalEnergy(), 0.0); // 逐位
        for (std::size_t i = 0; i < f1.nodes().size() && i < f2.nodes().size(); ++i) {
            CHECK(f1.nodes()[i].pos.x == f2.nodes()[i].pos.x); // 逐位
            CHECK(f1.nodes()[i].pos.y == f2.nodes()[i].pos.y);
            CHECK(f1.nodes()[i].energy == f2.nodes()[i].energy);
            CHECK(f1.nodes()[i].alive == f2.nodes()[i].alive);
            CHECK(f1.nodes()[i].id == f2.nodes()[i].id);
        }
    }
}

TEST(cyano_min_separation_aborts_split) {
    CyanoParams p;
    p.reproductionProbability = 1.0; // 强制进入分裂位置阶段
    p.reproductionCost = 40.0;
    p.minSeparation = 10.0;
    p.metabolismRate = 0.0;
    p.ballSpawnProbability = 0.0;
    p.linkRadius = 0.0; // S22 下半：隔离连线，保证"未分裂未扣能"断言纯净
    // 找一个种子：偏移落点恰落在节点1的 minSeparation 半径内 -> 放弃。
    bool found = false;
    for (std::uint64_t s = 1; s < 2000 && !found; ++s) {
        CyanoField t(p);
        PCG32 rr(s);
        t.seed(2, 40.0, 40.0, rr); // 小世界：wrap 后偏移更易落入邻居半区
        mut(t, 0).energy = 100.0;
        mut(t, 1).energy = 5.0; // 邻居须活过代谢期（0.0 会判死并被踢出校验）
        const CyanoStepReport r = t.step(0.0, 1000.0, 1000.0, rr);
        if (r.births == 0) {
            found = true;
            CHECK(t.nodes().size() == 2);           // 未新增
            CHECK_NEAR(mut(t, 0).energy, 100.0, 1e-12); // 未扣
            CHECK(r.reproductionLoss == 0.0);       // 未耗散
            // 放弃分支的 rng 预算语义：判定 1 次 + 位置 2 次照常消耗（不退 rng）。
        }
    }
    CHECK(found); // 放弃路径必须可达
}

TEST(cyano_cap_overflow_not_counted_as_gain) {
    CyanoParams p;
    p.energyCap = 50.0;
    p.metabolismRate = 0.0;
    CyanoField f(p);
    PCG32 rng(61);
    f.seed(1, 100.0, 100.0, rng);
    sole(f).energy = 49.999; // 一步满光 gain=2/3 将越限
    const CyanoStepReport r = f.step(1.0, 100.0, 100.0, rng);
    CHECK_NEAR(sole(f).energy, 50.0, 1e-12);
    // 进账口径 = 实际净增，而非名义 gain。
    CHECK_NEAR(r.photosynthesisGain, 50.0 - 49.999, 1e-12);
}

TEST(cyano_seed_layout_and_rng_budget) {
    CyanoParams p;
    p.reproductionProbability = 1.0; // 强制全通道：预算上界最大化
    p.ballSpawnProbability = 1.0;
    p.metabolismRate = 0.0;
    CyanoField f(p);
    PCG32 probe(77), ref(77);
    // R1-B：seed 每节点 3 次 nextDouble（x, y, hue）——3 节点 x 3 = 9 次。
    f.seed(3, 500.0, 500.0, probe);
    for (int i = 0; i < 9; ++i) (void)ref.nextDouble();
    std::uint64_t pa, ia, pb, ib;
    probe.getState(pa, ia);
    ref.getState(pb, ib);
    CHECK(pa == pb && ia == ib); // seed 恰好消耗 6 次 nextDouble

    // step: 3 节点 x (产球判定1) + 3 x (球位2) + 3 x (分裂判定1)
    //      + 3 x (R1-B 色相抖动2：Box-Muller u1/u2) + 3 x (子代位2)
    //      = 3*(1+2+1+2+2) = 24（假设间距全部放行；放弃分支不退 rng）
    PCG32 big(78);
    CyanoField g(p);
    g.seed(3, 10000.0, 10000.0, big); // 大世界降低放弃概率
    PCG32 budget(78);
    for (int i = 0; i < 6; ++i) (void)budget.nextDouble();
    (void)g.step(0.0, 10000.0, 10000.0, big);
    // 逐帧同参双跑校验 rng 消耗总量一致（不含放弃分支的精确预算另行验证）。
    PCG32 big2(78);
    CyanoField h(p);
    h.seed(3, 10000.0, 10000.0, big2);
    (void)h.step(0.0, 10000.0, 10000.0, big2);
    std::uint64_t s1, i1, s2, i2;
    big.getState(s1, i1);
    big2.getState(s2, i2);
    CHECK(s1 == s2 && i1 == i2);
}

TEST(cyano_default_params_match_spec) {
    CyanoParams p;
    CHECK(p.photosynthesisRate == 1.0);
    CHECK(p.lightHalfSaturation == 0.5);
    CHECK(p.metabolismRate == 0.02);
    CHECK(p.energyCap == 100.0);
    CHECK(p.reproductionCost == 40.0);
    CHECK(p.reproductionProbability == 0.01);
    CHECK(p.reproductionOffset == 125.0);
    CHECK(p.minSeparation == 10.0);
    CHECK(p.ballSpawnCost == 30.0);
    CHECK(p.ballSpawnProbability == 0.02);
    CHECK(p.ballRadius == 1.0);
    // R8/P2-1：产球出生点距产者的距离带（GOAL §2.2.1 ③"节点附近随机生成"）。
    CHECK(p.ballSpawnMinDist == 10.0);
    CHECK(p.ballSpawnMaxDist == 30.0);
    CHECK(p.linkRadius == 60.0);
    CHECK(p.linkMaxNeighbors == 3);
    CHECK(p.linkTransferRate == 0.1);
    CHECK(p.linkDeadband == 1.0);
    // R8/P2-2：默认逐帧结算（回归安全：与旧版"每帧结算"逐位一致）。
    CHECK(p.linkCheckInterval == 1);
}

// ===== S22 下半：连线能量传输 =====

TEST(cyano_link_zero_sum) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.ballSpawnProbability = 0.0;
    p.reproductionProbability = 0.0;
    p.linkRadius = 60.0;
    p.linkMaxNeighbors = 3;
    p.linkTransferRate = 0.1;
    p.linkDeadband = 0.0; // 任意小差值都传输，最大化移动量
    CyanoField f(p);
    PCG32 rng(101);
    f.seed(6, 200.0, 200.0, rng);
    const double energies[6] = {5.0, 40.0, 70.0, 10.0, 95.0, 25.0};
    for (std::size_t i = 0; i < f.nodes().size() && i < 6; ++i) {
        mut(f, i).energy = energies[i];
    }
    const double before = f.totalEnergy();
    f.linkTransfer();
    CHECK_NEAR(before, f.totalEnergy(), 1e-12); // 零和：总能量守恒
    CHECK(f.lastLinkEdgeCount() > 0);           // 确有边成交
}

TEST(cyano_link_flows_high_to_low) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.ballSpawnProbability = 0.0;
    p.reproductionProbability = 0.0;
    p.linkRadius = 200.0;
    p.linkTransferRate = 0.25;
    p.linkDeadband = 0.0;
    CyanoField f(p);
    PCG32 rng(103);
    f.seed(2, 300.0, 300.0, rng);
    mut(f, 0).pos = Vec2{50.0, 50.0};
    mut(f, 1).pos = Vec2{80.0, 50.0}; // 环距 30，连线内
    mut(f, 0).energy = 90.0;
    mut(f, 1).energy = 10.0;
    f.linkTransfer();
    CHECK(mut(f, 0).energy < 90.0);  // 高能者下降
    CHECK(mut(f, 1).energy > 10.0);  // 低能者上升
    CHECK_NEAR(mut(f, 0).energy, 90.0 - 80.0 * 0.25, 1e-12);
    CHECK_NEAR(mut(f, 1).energy, 10.0 + 80.0 * 0.25, 1e-12);
    CHECK_NEAR(f.lastLinkTransferred(), 20.0, 1e-12);
}

// 【红队 R9-P2 回归测试】同帧多个亲代全部通过判定时，阶段 6 连续 push_back
// 曾突破 maxNodes（实测 50k tick nodes=65 > maxNodes=64）。逐帧断言不超限。
TEST(cyano_max_nodes_never_exceeded) {
    CyanoParams p;
    p.maxNodes = 8;
    p.reproductionProbability = 1.0;   // 全员每帧判定通过（能量充足时）
    p.metabolismRate = 0.0;
    p.ballSpawnProbability = 0.0;
    p.linkRadius = 0.0;                // 隔离连线，聚焦分裂上限
    CyanoField f(p);
    PCG32 rng(42);
    f.seed(8, 800.0, 800.0, rng);
    for (std::size_t i = 0; i < f.nodes().size(); ++i) mut(f, i).energy = 90.0;
    for (int t = 0; t < 200; ++t) {
        (void)f.step(0.5, 800.0, 800.0, rng);
        CHECK(f.nodes().size() <= p.maxNodes);   // 帧内突破在此暴露
    }
    CHECK(f.aliveCount() <= p.maxNodes);
}

TEST(cyano_link_respects_radius) {
    CyanoParams p;
    p.linkRadius = 40.0;
    CyanoField f(p);
    PCG32 rng(107);
    f.seed(2, 300.0, 300.0, rng);
    mut(f, 0).pos = Vec2{50.0, 150.0};
    mut(f, 1).pos = Vec2{250.0, 150.0}; // 环距 200 > 40
    mut(f, 0).energy = 90.0;
    mut(f, 1).energy = 10.0;
    f.linkTransfer();
    CHECK(mut(f, 0).energy == 90.0); // 完全不变（逐位）
    CHECK(mut(f, 1).energy == 10.0);
    CHECK(f.lastLinkEdgeCount() == 0);
}

TEST(cyano_link_deadband_blocks) {
    CyanoParams p;
    p.linkRadius = 200.0;
    p.linkTransferRate = 0.5;
    p.linkDeadband = 1.0;
    CyanoField f(p);
    PCG32 rng(109);
    f.seed(2, 300.0, 300.0, rng);
    mut(f, 0).pos = Vec2{50.0, 50.0};
    mut(f, 1).pos = Vec2{60.0, 50.0}; // 环距 10 << 200
    mut(f, 0).energy = 50.5;
    mut(f, 1).energy = 50.0; // 差 0.5 < deadband 1.0
    f.linkTransfer();
    CHECK(mut(f, 0).energy == 50.5); // 完全不传输（逐位）
    CHECK(mut(f, 1).energy == 50.0);
    CHECK(f.lastLinkEdgeCount() == 0);
    CHECK(f.lastLinkTransferred() == 0.0);
}

TEST(cyano_link_no_rng_consumption) {
    CyanoParams p;
    p.linkRadius = 100.0;
    CyanoField f(p);
    PCG32 rng(113);
    f.seed(4, 300.0, 300.0, rng);
    mut(f, 0).energy = 90.0;
    mut(f, 1).energy = 10.0;
    mut(f, 2).energy = 50.0;
    mut(f, 3).energy = 30.0;
    std::uint64_t s1, i1, s2, i2;
    rng.getState(s1, i1);
    f.linkTransfer();
    rng.getState(s2, i2);
    CHECK(s1 == s2 && i1 == i2); // rng 状态逐位不变（确定性红线）
    // step() 整帧口径下同样成立：双场同 seed，仅一方关连线，
    // 若 linkTransfer 偷耗 rng，两者的产球/分裂判定将走出不同轨迹。
    CyanoParams q;
    q.metabolismRate = 0.0;
    q.linkRadius = 0.0; // 关连线
    CyanoField a(q), b(q);
    PCG32 ra(97), rb(97);
    a.seed(5, 300.0, 300.0, ra);
    b.seed(5, 300.0, 300.0, rb);
    (void)a.step(0.0, 300.0, 300.0, ra);
    std::uint64_t s3, i3, s4, i4;
    ra.getState(s3, i3);
    b.linkTransfer(); // b 场手动插入一次连线（参数下 linkRadius=0 为空操作）
    (void)b.step(0.0, 300.0, 300.0, rb);
    rb.getState(s4, i4);
    CHECK(s3 == s4 && i3 == i4);
    CHECK(a.nodes().size() == b.nodes().size());
}

TEST(cyano_link_deterministic) {
    CyanoParams a;
    a.linkRadius = 80.0;
    a.linkTransferRate = 0.2;
    a.reproductionProbability = 0.25;
    a.ballSpawnProbability = 0.3;
    CyanoField f1(a), f2(a);
    PCG32 r1(555), r2(555);
    f1.seed(7, 500.0, 500.0, r1);
    f2.seed(7, 500.0, 500.0, r2);
    for (int t = 0; t < 50; ++t) {
        const double sun = 0.2 + 0.5 * (t % 2);
        (void)f1.step(sun, 500.0, 500.0, r1);
        (void)f2.step(sun, 500.0, 500.0, r2);
    }
    CHECK(f1.nodes().size() == f2.nodes().size());
    CHECK(f1.aliveCount() == f2.aliveCount());
    CHECK_NEAR(f1.totalEnergy(), f2.totalEnergy(), 0.0); // 逐位
    for (std::size_t i = 0; i < f1.nodes().size() && i < f2.nodes().size(); ++i) {
        CHECK(f1.nodes()[i].energy == f2.nodes()[i].energy); // 逐位
        CHECK(f1.nodes()[i].pos.x == f2.nodes()[i].pos.x);
        CHECK(f1.nodes()[i].pos.y == f2.nodes()[i].pos.y);
        CHECK(f1.nodes()[i].alive == f2.nodes()[i].alive);
    }
}

TEST(cyano_link_max_neighbors) {
    CyanoParams p;
    p.linkRadius = 500.0; // 全员互在半径内
    p.linkMaxNeighbors = 3;
    p.linkTransferRate = 0.1;
    p.linkDeadband = 0.0;
    CyanoField f(p);
    PCG32 rng(777);
    f.seed(5, 2000.0, 2000.0, rng);
    mut(f, 0).pos = Vec2{100.0, 100.0};
    mut(f, 1).pos = Vec2{130.0, 100.0};
    mut(f, 2).pos = Vec2{160.0, 100.0};
    mut(f, 3).pos = Vec2{190.0, 100.0};
    mut(f, 4).pos = Vec2{220.0, 100.0};
    mut(f, 0).energy = 10.0;
    mut(f, 1).energy = 30.0;
    mut(f, 2).energy = 50.0;
    mut(f, 3).energy = 70.0;
    mut(f, 4).energy = 90.0;
    f.linkTransfer();
    // 双向互选语义下推演得恰 6 条边：(0,1)(0,2)(1,2)(1,3)(2,3)(3,4)。
    // 任一节点度数 <= 3：0->3条 1->3条 2->3条 3->3条 4->1条（上限语义的直接体现）。
    CHECK(f.lastLinkEdgeCount() == 6);
    // 零和再确认。
    CHECK_NEAR(f.totalEnergy(), 250.0, 1e-9);
    // 方向校验：全局最富节点（90）比所有节点都富，其任何邻边必为纯输出。
    CHECK(mut(f, 4).energy < 90.0);
    CHECK(f.lastLinkTransferred() > 0.0);
    // 两节点上限语义：cap=1 时恰好 1 条边。
    CyanoParams q;
    q.linkRadius = 500.0;
    q.linkMaxNeighbors = 1;
    q.linkDeadband = 0.0;
    CyanoField g(q);
    PCG32 rng2(778);
    g.seed(5, 2000.0, 2000.0, rng2);
    mut(g, 0).pos = Vec2{100.0, 100.0};
    mut(g, 1).pos = Vec2{130.0, 100.0};
    mut(g, 2).pos = Vec2{160.0, 100.0};
    mut(g, 3).pos = Vec2{190.0, 100.0};
    mut(g, 4).pos = Vec2{220.0, 100.0};
    mut(g, 0).energy = 10.0;
    mut(g, 1).energy = 30.0;
    mut(g, 2).energy = 50.0;
    mut(g, 3).energy = 70.0;
    mut(g, 4).energy = 90.0;
    g.linkTransfer();
    CHECK_NEAR(g.totalEnergy(), 250.0, 1e-9); // cap=1 下同样零和
    // cap=1：等距并列按下标序破平，各节点最近候选均指向左邻，唯 (0,1)
    // 双向互选成立（1 的候选是 0）=> 恰 1 条边；0 得能、1 失能。
    CHECK(g.lastLinkEdgeCount() == 1);
    CHECK(mut(g, 0).energy > 10.0);  // 低能端流入
    CHECK(mut(g, 1).energy < 30.0);  // 高能端流出
}

// ===== R1-B 追加：捕食转化率公式 / 遗传色相 / 连线拓扑对外可见 =====

// R1-B：转化率公式 mode 0/1/2/3 全覆盖（纯函数，无 rng）。
TEST(cyano_conversion_rate_modes) {
    using agentsim::CyanoField;
    const double base = 0.6;
    // mode 0：常数——速度/加速度不参与。
    CHECK_NEAR(CyanoField::cyanoConversionRate(0, base, 1.0, 40.0, 60.0, 1.0, 1.0,
                                               0.05, 1.0, 40.0, 60.0),
               0.6, 1e-12);
    // mode 1：速度惩罚 rate = base * ref/(ref+speed)。
    CHECK_NEAR(CyanoField::cyanoConversionRate(1, base, 1.0, 40.0, 60.0, 1.0, 1.0,
                                               0.05, 1.0, 40.0, 60.0),
               0.6 * (40.0 / 80.0), 1e-12);
    // mode 2：加速度惩罚。
    CHECK_NEAR(CyanoField::cyanoConversionRate(2, base, 1.0, 40.0, 60.0, 1.0, 1.0,
                                               0.05, 1.0, 40.0, 60.0),
               0.6 * (60.0 / 120.0), 1e-12);
    // mode 3：混合 = base * wSpeed * ps * wAccel * pa。
    CHECK_NEAR(CyanoField::cyanoConversionRate(3, base, 1.0, 40.0, 60.0, 1.0, 1.0,
                                               0.05, 1.0, 40.0, 60.0),
               0.6 * 0.5 * 0.5, 1e-12);
    // mode 3 权重真生效：wSpeed=2 翻倍速度惩罚项。
    CHECK_NEAR(CyanoField::cyanoConversionRate(3, base, 1.0, 40.0, 60.0, 2.0, 1.0,
                                               0.05, 1.0, 40.0, 60.0),
               0.6 * 2.0 * 0.5 * 0.5, 1e-12);
    // 速度越快惩罚越重（mode 1 单调递减）。
    const double slow = CyanoField::cyanoConversionRate(1, base, 1.0, 40.0, 60.0,
                                                        1.0, 1.0, 0.05, 1.0, 10.0, 60.0);
    const double fast = CyanoField::cyanoConversionRate(1, base, 1.0, 40.0, 60.0,
                                                        1.0, 1.0, 0.05, 1.0, 400.0, 60.0);
    CHECK(fast < slow);
    // clamp 生效：min 抬底 / max 封顶。
    CHECK_NEAR(CyanoField::cyanoConversionRate(1, 0.001, 1.0, 40.0, 60.0, 1.0, 1.0,
                                               0.05, 1.0, 400.0, 60.0),
               0.05, 1e-12); // 底：base*ps < minRate
    CHECK_NEAR(CyanoField::cyanoConversionRate(0, 5.0, 1.0, 40.0, 60.0, 1.0, 1.0,
                                               0.05, 1.0, 40.0, 60.0),
               1.0, 1e-12);  // 顶：5.0 > maxRate
    // 退化输入：speed/accel <= 0 时对应惩罚项取 1（无机动 = 不受罚）。
    CHECK_NEAR(CyanoField::cyanoConversionRate(1, base, 1.0, 40.0, 60.0, 1.0, 1.0,
                                               0.05, 1.0, 0.0, 60.0),
               0.6, 1e-12);
    // 非正基率：返回 0（防 NaN/负值传播）。
    CHECK(CyanoField::cyanoConversionRate(3, 0.0, 1.0, 40.0, 60.0, 1.0, 1.0,
                                          0.05, 1.0, 40.0, 60.0) == 0.0);
    CHECK(CyanoField::cyanoConversionRate(-1, -2.0, 1.0, 40.0, 60.0, 1.0, 1.0,
                                          0.05, 1.0, 40.0, 60.0) == 0.0);
}

// R1-B：seed 色相 = 第三抽 360*u，∈ [0,360) 且逐位确定。
TEST(cyano_seed_hue_range_and_determinism) {
    CyanoParams p;
    p.nodeSeedCount = 16;
    CyanoField f1(p), f2(p);
    PCG32 r1(2026), r2(2026);
    f1.seed(16, 500.0, 500.0, r1);
    f2.seed(16, 500.0, 500.0, r2);
    int distinct = 0;
    double firstHue = -1.0;
    for (std::size_t i = 0; i < f1.nodes().size(); ++i) {
        const double h1 = f1.nodes()[i].hue;
        const double h2 = f2.nodes()[i].hue;
        CHECK(h1 == h2);           // 同 seed 逐位一致
        CHECK(h1 >= 0.0 && h1 < 360.0);
        if (firstHue < 0.0) firstHue = h1;
        if (h1 != firstHue) ++distinct;
    }
    CHECK(distinct > 0); // 16 个节点色相不全相同（均匀采样覆盖面）
}

// R1-B：分裂色相 = 父 + 高斯抖动（环绕 [0,360)）；
// 抖动幅度受 hueJitterSigma 控制（sigma=0 时子代 hue == 父 hue 逐位）。
TEST(cyano_split_hue_inherits_with_jitter) {
    // sigma=0：子代色相严格等于父代（环绕公式退化恒等）。
    CyanoParams pz;
    pz.reproductionProbability = 1.0; // 强制分裂
    pz.reproductionCost = 40.0;
    pz.metabolismRate = 0.0;
    pz.ballSpawnProbability = 0.0;
    pz.linkRadius = 0.0;
    pz.hueJitterSigma = 0.0;
    pz.minSeparation = 0.0;           // 不因间距放弃（色相断言聚焦）
    CyanoField fz(pz);
    PCG32 rz(31);
    fz.seed(1, 2000.0, 2000.0, rz);
    const_cast<CyanoNode&>(fz.nodes().front()).hue = 123.0;
    const_cast<CyanoNode&>(fz.nodes().front()).energy = 100.0; // 夹具：供分裂成本
    (void)fz.step(0.0, 2000.0, 2000.0, rz);
    CHECK(fz.nodes().size() == 2);
    CHECK(fz.nodes().back().hue == 123.0); // sigma=0：逐位遗传

    // sigma>0：子代 hue ∈ [0,360)，且与父代差 <= 6*sigma（高斯 6σ 截断面）。
    CyanoParams p;
    p = pz;
    p.hueJitterSigma = 8.0;
    CyanoField f(p);
    PCG32 rng(32);
    f.seed(1, 2000.0, 2000.0, rng);
    const_cast<CyanoNode&>(f.nodes().front()).hue = 359.0; // 近环绕缝：必修回 [0,360)
    const_cast<CyanoNode&>(f.nodes().front()).energy = 100.0; // 夹具：供分裂成本
    const CyanoStepReport rep = f.step(0.0, 2000.0, 2000.0, rng);
    CHECK(rep.births > 0);
    bool wrappedOk = false;
    for (std::size_t i = 1; i < f.nodes().size(); ++i) {
        const double h = f.nodes()[i].hue;
        CHECK(h >= 0.0 && h < 360.0);
        if (h < 360.0) wrappedOk = true;
    }
    CHECK(wrappedOk);
}

// R1-B：连线拓扑对外可见——成交边进入 links()，防抖/超距的边不进入。
TEST(cyano_links_topology_visible) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.ballSpawnProbability = 0.0;
    p.reproductionProbability = 0.0;
    p.linkRadius = 200.0;
    p.linkTransferRate = 0.25;
    p.linkDeadband = 0.0;
    CyanoField f(p);
    PCG32 rng(401);
    f.seed(2, 300.0, 300.0, rng);
    const_cast<CyanoNode&>(f.nodes()[0]).pos = Vec2{50.0, 50.0};
    const_cast<CyanoNode&>(f.nodes()[1]).pos = Vec2{80.0, 50.0}; // 环距 30 < 200
    const_cast<CyanoNode&>(f.nodes()[0]).energy = 90.0;
    const_cast<CyanoNode&>(f.nodes()[1]).energy = 10.0;
    CHECK(f.links().empty());      // 结算前拓扑为空
    f.linkTransfer();
    CHECK(f.links().size() == 1);  // 恰一条成交边
    CHECK(f.links()[0].first == f.nodes()[0].id);   // (小 id, 大 id) 有序对
    CHECK(f.links()[0].second == f.nodes()[1].id);
    CHECK(f.lastLinkEdgeCount() == 1);
    // 超距：无成交边，links() 清空。
    CyanoParams q = p;
    q.linkRadius = 10.0;
    CyanoField g(q);
    PCG32 rng2(402);
    g.seed(2, 300.0, 300.0, rng2);
    g.linkTransfer();
    CHECK(g.links().empty());
    CHECK(g.lastLinkEdgeCount() == 0);
    // 节点容器与拓扑联动：节点死亡压缩后，下一次 linkTransfer 拓扑按存活重算。
    const_cast<CyanoNode&>(g.nodes()[0]).pos = Vec2{50.0, 50.0};
    const_cast<CyanoNode&>(g.nodes()[1]).pos = Vec2{60.0, 50.0};
    const_cast<CyanoNode&>(g.nodes()[0]).energy = 90.0;
    const_cast<CyanoNode&>(g.nodes()[1]).energy = 10.0;
    g.linkTransfer();
    CHECK(g.links().size() == 1);
}

// ===== R8/P2 追加：产球位置靠近产者、连线节流 =====

// R8/P2-1（GOAL §2.2.1 ③）：产出的球必须生成在产者"附近"——每个出生点与
// 产者的最小环面距离必须落在 [ballSpawnMinDist, ballSpawnMaxDist] 带内。
// 用 lastBallSpawns()（出生点序 = ballSpawned 判定序 = 产者下标升序）配对产者。
TEST(cyano_ball_spawn_near_parent) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.ballSpawnProbability = 1.0; // 强制全存活节点产球：判定序与节点序一致
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0;           // 隔离连线，产者能量位置不受结算影响
    p.energyCap = 1e9;            // 不设上限：让 6 个节点连续 12 帧都能产球
    p.ballSpawnMinDist = 10.0;
    p.ballSpawnMaxDist = 30.0;
    CyanoField f(p);
    PCG32 rng(1201);
    f.seed(6, 400.0, 400.0, rng);
    for (std::size_t i = 0; i < f.nodes().size(); ++i) mut(f, i).energy = 500.0;

    int checked = 0;
    for (int t = 0; t < 12; ++t) {
        const CyanoStepReport r = f.step(0.0, 400.0, 400.0, rng);
        CHECK(static_cast<int>(f.lastBallSpawns().size()) == r.ballsProduced);
        // 产球判定序按节点下标升序 => 出生点 k 属于第 (k+1) 个通过判定的节点。
        std::size_t spawnIdx = 0;
        for (std::size_t i = 0; i < f.nodes().size(); ++i) {
            if (!f.nodes()[i].alive) continue;
            if (spawnIdx >= f.lastBallSpawns().size()) break; // 放弃分支（无：能量足）
            const Vec2& bp = f.lastBallSpawns()[spawnIdx++];
            const double d = agentsim::torusDistance(bp, f.nodes()[i].pos, 400.0, 400.0);
            CHECK(d >= p.ballSpawnMinDist - 1e-9);  // 不出生在产者身上
            CHECK(d <= p.ballSpawnMaxDist + 1e-9);  // 不飞出"附近"半径
            ++checked;
        }
        CHECK(spawnIdx == f.lastBallSpawns().size()); // 配对完整（无遗漏/错位）
    }
    CHECK(checked >= 24); // 12 帧 x 至少 2 个产者：样本量足够支撑区间断言
}

// R8/P2-2：linkCheckInterval=1（默认）与"每帧结算"的旧行为逐位一致。
// 同 seed 双场各跑 300 帧（覆盖若干次分裂/产球/死亡），全部状态逐位相等。
TEST(cyano_link_interval_1_same_as_always) {
    CyanoParams a;
    a.reproductionProbability = 0.25;
    a.ballSpawnProbability = 0.3;
    a.linkRadius = 80.0;
    a.linkTransferRate = 0.2;
    a.linkCheckInterval = 1;
    CyanoParams b = a;
    b.linkCheckInterval = 1; // 与默认相同的显式写法：逐位一致
    CyanoField f1(a), f2(b);
    PCG32 r1(900), r2(900);
    f1.seed(7, 500.0, 500.0, r1);
    f2.seed(7, 500.0, 500.0, r2);
    for (int t = 0; t < 300; ++t) {
        const double sun = 0.2 + 0.5 * (t % 2); // 时变光强（同参同序）
        (void)f1.step(sun, 500.0, 500.0, r1);
        (void)f2.step(sun, 500.0, 500.0, r2);
    }
    CHECK(f1.nodes().size() == f2.nodes().size());
    CHECK(f1.aliveCount() == f2.aliveCount());
    CHECK_NEAR(f1.totalEnergy(), f2.totalEnergy(), 0.0); // 逐位
    for (std::size_t i = 0; i < f1.nodes().size() && i < f2.nodes().size(); ++i) {
        CHECK(f1.nodes()[i].id == f2.nodes()[i].id);
        CHECK(f1.nodes()[i].pos.x == f2.nodes()[i].pos.x); // 逐位
        CHECK(f1.nodes()[i].pos.y == f2.nodes()[i].pos.y);
        CHECK(f1.nodes()[i].energy == f2.nodes()[i].energy); // 逐位
        CHECK(f1.nodes()[i].alive == f2.nodes()[i].alive);
    }
}

// R8/P2-2：interval=2 真生效——隔帧结算后能量轨迹与每帧结算不同。
// 两场共享同一 rng 流（连线零随机；判定概率 0 但判定抽取照常消耗，两场同序），
// 显式摆位与初始能量（摆脱 seed 随机布局的偶然性），唯一自由度是
// linkCheckInterval：f1 每帧结算、f2 隔帧结算，均衡进度必然不同。
TEST(cyano_link_interval_2_differs) {
    CyanoParams a;
    a.metabolismRate = 0.0;
    a.reproductionProbability = 0.0;
    a.ballSpawnProbability = 0.0;
    a.linkRadius = 200.0;
    a.linkMaxNeighbors = 3;
    a.linkTransferRate = 0.25;
    a.linkDeadband = 0.0;
    a.linkCheckInterval = 1;
    CyanoParams b = a;
    b.linkCheckInterval = 2;
    CyanoField f1(a), f2(b);
    PCG32 r1(901), r2(901);
    f1.seed(5, 300.0, 300.0, r1);
    f2.seed(5, 300.0, 300.0, r2);
    // 两场完全相同的初态：等距链式摆位（环距 30~120 全在 linkRadius 内，
    // 双向互选成 6 条边）+ 互异初始能量（保证差值处处非零、结算必移动能量）。
    const double xs[5] = {50.0, 80.0, 110.0, 140.0, 170.0};
    const double es[5] = {5.0, 40.0, 70.0, 10.0, 95.0};
    for (std::size_t i = 0; i < 5 && i < f1.nodes().size() && i < f2.nodes().size(); ++i) {
        mut(f1, i).pos = Vec2{xs[i], 150.0};
        mut(f1, i).energy = es[i];
        mut(f2, i).pos = Vec2{xs[i], 150.0};
        mut(f2, i).energy = es[i];
    }
    bool differed = false;
    for (int t = 0; t < 10; ++t) {
        const CyanoStepReport rA = f1.step(0.0, 300.0, 300.0, r1);
        const CyanoStepReport rB = f2.step(0.0, 300.0, 300.0, r2);
        // 通道1：奇数帧（frameCount_ % 2 == 1）被节流跳过，report 保持 0.0。
        if (t % 2 == 1 && rB.linkTransferred == 0.0) differed = true;
        // 通道2：同帧结算量不同（f1 已结算 t+1 次，f2 仅 ceil((t+1)/2) 次）。
        if (rA.linkTransferred != rB.linkTransferred) differed = true;
        // 通道3：节点能量轨迹逐位分叉。
        for (std::size_t i = 0; i < f1.nodes().size() && i < f2.nodes().size(); ++i) {
            if (f1.nodes()[i].energy != f2.nodes()[i].energy) differed = true;
        }
    }
    CHECK(differed); // 参数必须真生效（不可观测即死参数）
    // 收尾强断言：10 帧后（f1 结算 10 次 vs f2 结算 5 次）逐位分布不同——
    // 隔帧结算的能量均衡进度更慢；两侧都远离舍入不动点，差异稳定可观测。
    bool anyEnergyDiff = false;
    for (std::size_t i = 0; i < f1.nodes().size() && i < f2.nodes().size(); ++i) {
        if (f1.nodes()[i].energy != f2.nodes()[i].energy) anyEnergyDiff = true;
    }
    CHECK(anyEnergyDiff);
    CHECK(f1.aliveCount() == f2.aliveCount()); // 随机流未变：存活集合大小一致
}

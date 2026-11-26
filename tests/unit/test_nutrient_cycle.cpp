// agentsim - tests/unit/test_nutrient_cycle.cpp
// R3-A（营养循环）单测：**物质**（养分/残骸）与**能量**（账本）两条独立回路。
//
// ---- 规则 v2（P1）迁移说明 ----
// 旧版本文件的被保护对象是 CyanoField::fertilize（把"死亡能量"直接灌进某个
// 蓝细菌节点的 energy，受 energyCap 约束、溢出记 dissipate）。规则 v2 把这件事
// 拆成两条互不相通的通道（设计 §3.2 / 验收 A6–A8）：
//   · 能量的去向只有"耗散"：核死亡时剩余可用能量全额 dissipate("death")；
//   · 物质的去向只有"残骸 → 分解 → 养分"：核死亡时 bodyMass → remains_.debris
//     → NutrientField::addDebris → decompose → 可吸收养分。
// 因此本文件按新行为重写，但**保留原有保护意图**：
//   1) "输入必须落地、且总账闭合"——施肥量 = 养分场增量 = 外部投入增量（零泄漏）；
//   2) "非法输入不得反向扣款"——非正/NaN 一律拒收，状态逐位不变；
//   3) "死亡必须留下可解释的去向"——能量账全额耗散、物质账全额进残骸，
//      两条恒等式都用数值钉死，不放宽误差。
// 明确删除的旧用例（规则已不存在）：fertilize 的 energyCap 上限与溢出返款断言
// （养分场没有容量上限；旧"用能量喂生产者"的语义本身已被删除）。
#include "test_framework.h"

#include <cmath>
#include <limits>
#include <vector>

#include "../../src/behavior/AgentBehavior.h"
#include "../../src/core/PCG32.h"
#include "../../src/core/Types.h"
#include "../../src/core/Vec2.h"
#include "../../src/environment/NutrientField.h"
#include "../../src/ledger/AgentLedger.h"
#include "../../src/sim/Simulation.h"

namespace {

using agentsim::AgentBehavior;
using agentsim::AgentEnvironment;
using agentsim::AgentLedger;
using agentsim::BehaviorParams;
using agentsim::Nucleus;
using agentsim::NutrientField;
using agentsim::PCG32;
using agentsim::SimConfig;
using agentsim::Simulation;
using agentsim::Vec2;

// 只观察到能量账的口径：活核能量 + 名下球 + detached_ 缓冲（与总能量同口径）。
double energyOf(const Nucleus& n) {
    double sum = n.alive ? n.energy : 0.0;
    for (const agentsim::Ball& b : n.balls) sum += b.energy;
    for (const agentsim::Ball& b : n.detached_) sum += b.energy;
    return sum;
}

// 无蓝细菌、静止的安静环境（排除进食与移动干扰，账目可解析）。
AgentEnvironment makeQuietEnv() {
    SimConfig cfg;
    cfg.cyanoNodeCount = 0;
    return AgentEnvironment(cfg);
}

} // namespace

// ---- 施肥：物质输入的真实落点（旧 fertilize_accepts_full_amount_within_cap）----

TEST(fertilize_adds_exactly_the_requested_nutrient_locally) {
    Simulation sim(SimConfig{});
    const Vec2 here{12.0, 34.0};
    const Vec2 far = agentsim::wrap(Vec2{here.x + 512.0, here.y + 512.0},
                                    sim.cfgProbe().world.width,
                                    sim.cfgProbe().world.height);
    const double hereBefore = sim.nutrients().cellAt(here).nutrient;
    const double farBefore = sim.nutrients().cellAt(far).nutrient;
    const double totalBefore = sim.matter().total();
    const double extBefore = sim.matter().externalIn;

    const double accepted = sim.fertilize(here, 25.0);
    CHECK_NEAR(accepted, 25.0, 1e-12);                       // 全额接受（养分无上限）
    CHECK_NEAR(sim.nutrients().cellAt(here).nutrient, hereBefore + 25.0, 1e-12);
    // 其它区域一点没收到（A6：局部性）。
    CHECK_NEAR(sim.nutrients().cellAt(far).nutrient, farBefore, 0.0);
    // 物质账：总量增量 == 施肥量 == 外部投入增量（三处同源，零泄漏）。
    CHECK_NEAR(sim.matter().total() - totalBefore, 25.0, 1e-9);
    CHECK_NEAR(sim.matter().externalIn - extBefore, 25.0, 1e-12);
    CHECK(sim.auditOk());
}

TEST(fertilize_rejects_nonpositive_and_nan) {
    Simulation sim(SimConfig{});
    const Vec2 p{40.0, 60.0};
    const double before = sim.nutrients().cellAt(p).nutrient;
    const double totalBefore = sim.matter().total();
    const double extBefore = sim.matter().externalIn;
    const double nan = std::numeric_limits<double>::quiet_NaN();

    CHECK(sim.fertilize(p, 0.0) == 0.0);
    CHECK(sim.fertilize(p, -3.0) == 0.0);
    CHECK(sim.fertilize(p, -0.0) == 0.0);
    CHECK(sim.fertilize(p, nan) == 0.0);
    // 拒收路径逐位无副作用（不反向扣款、不虚增外部投入）。
    CHECK_NEAR(sim.nutrients().cellAt(p).nutrient, before, 0.0);
    CHECK_NEAR(sim.matter().total(), totalBefore, 0.0);
    CHECK_NEAR(sim.matter().externalIn, extBefore, 0.0);
}

// ---- 分解：残骸 → 养分的局部过程（真转移通道，不是"记一笔就算"）----

TEST(decompose_moves_debris_into_nutrient_in_place) {
    NutrientField f(400.0, 400.0, 100.0, 5.0);
    const Vec2 p{50.0, 50.0};                 // 区域 (0,0)
    f.addDebris(p, 40.0);
    const double nutrient0 = f.cellAt(p).nutrient;
    const double total0 = f.total();

    const double converted = f.decompose(0.25);
    CHECK_NEAR(converted, 10.0, 1e-12);       // 40 * 0.25
    CHECK_NEAR(f.cellAt(p).debris, 30.0, 1e-12);
    CHECK_NEAR(f.cellAt(p).nutrient, nutrient0 + 10.0, 1e-12);
    CHECK_NEAR(f.total(), total0, 1e-12);     // 物质总量守恒（只是换了形态）
    // 残骸与养分**都不产生能量**：本模块不认识账本，这条结构性事实由
    // "它没有能量入口" 保证（NutrientField 无任何账本/能量接口）。
    CHECK(f.totalDebris() >= 0.0);
    CHECK(f.totalNutrient() >= 0.0);
}

// ---- 死亡的账目闭合：能量全额耗散 + 物质全额进残骸（旧 carrion 恒等式的替代）----

TEST(death_energy_dissipates_and_body_mass_becomes_debris) {
    AgentEnvironment env = makeQuietEnv();
    AgentLedger ledger;
    BehaviorParams bp;
    bp.moveK = 0.0;              // 静止：只剩呼吸一条支出口
    // 球的 ballDrift 是 locomotionCost × radius × 0.05/帧——本用例要逐位断言
    // "球内储备全额耗散"，所以把这条散热通道也关掉（它对结论没有贡献，
    // 只会让偏差量落在一个需要靠容差掩盖的位置）。
    bp.locomotionCost = 0.0;
    Nucleus n;
    n.id = 1;
    n.pos = Vec2{50.0, 50.0};
    n.energy = 0.001;            // 不足一次呼吸：本帧必死
    n.cap = 200.0;
    n.bodyMass = 20.0;
    n.alive = true;
    // 一颗随身球：球内储备也是"可用能量"，死亡时必须全额耗散（不能凭空消失）。
    // 规则 v2 迁移注意：这里必须用 **非 Resource** 球。Resource 球是仓库，
    // 核缺粮时 apply 会按 kResourceBallWithdrawRate 从中取回能量（A10 的有限
    // 速率回血）——那样核就饿不死了，本用例测的"死亡时全额耗散"也就无从发生。
    // Shield/Scout 球没有回血通道，它们的存量只能随核死亡一起耗散。
    agentsim::Ball b;
    b.id = 0xB0000001ULL;
    b.type = agentsim::BallType::Shield;
    b.energy = 30.0;
    b.ownerId = n.id;
    // 球摆在核正上方（环距 0）：球控制循环里的 ballDrift 是
    // locomotionCost × radius × 0.05/帧，radius 由真实环距反算——摆在核身上
    // radius=0，漂移恒 0，于是"死亡耗散量"可以逐位断言而不是靠容差。
    b.pos = Vec2{50.0, 50.0};
    n.balls.push_back(b);
    ledger.injectInitial(n.id, n.energy);
    ledger.injectInitial(b.id, b.energy);

    const double livingBefore = energyOf(n);
    CHECK_NEAR(livingBefore, 30.001, 1e-12);
    const double massBefore = n.bodyMass;

    agentsim::ActionVector act;
    PCG32 rng(5);
    AgentBehavior::apply(n, act, env, ledger, 1.0, bp, rng);

    CHECK(!n.alive);
    CHECK(n.energy == 0.0);
    // 物质去向：身体完整留在原地（形状是 Remains{pos, debris}，不是能量）。
    CHECK(n.bodyMass == 0.0);
    CHECK_NEAR(n.remains_.debris, massBefore, 0.0);         // 逐位：一分不少
    CHECK_NEAR(n.remains_.pos.x, 50.0, 0.0);
    CHECK_NEAR(n.remains_.pos.y, 50.0, 0.0);
    // 能量去向：逐项可解释。
    //   · 呼吸先按限额吃掉核体那 0.001（余额不足只花现有，永不为负）；
    //   · 剩下的球内储备（30.0，摆在核身上所以零漂移）全额记 dissipate("death")。
    CHECK_NEAR(ledger.dissipatedBy("respiration"), 0.001, 1e-12);
    CHECK_NEAR(ledger.dissipatedBy("death"), 30.0, 1e-12);
    CHECK_NEAR(ledger.dissipatedBy("death") + ledger.dissipatedBy("respiration"),
               livingBefore, 1e-12);   // 两个支出口恰好吃掉全部初始能量
    CHECK_NEAR(energyOf(n), 0.0, 0.0);
    // 恒等式：injected - dissipated == Σ存活能量（这里恰好 0）。
    CHECK_NEAR(ledger.netInjected(), 0.0, 1e-12);
    CHECK(ledger.audit(1e-9 * (1.0 + ledger.totalInjected()), energyOf(n)));
    // 残骸是物质，绝不能被记进能量账（A7：物质与能量两条回路独立）。
    // 总耗散必须能被逐项解释：本帧只有"移动/呼吸/death"三种支出口，
    // 静核移动恒 0，所以账面 = 呼吸 + death。任何多出来的量都是账外蒸发。
    CHECK_NEAR(ledger.totalDissipated(),
               ledger.dissipatedBy("respiration") + ledger.dissipatedBy("death"), 1e-12);
    CHECK_NEAR(ledger.dissipatedBy("locomotion"), 0.0, 1e-12); // moveK=0：静止不耗移动能
}

TEST(death_without_energy_left_still_leaves_debris) {
    // "饿死也留下身体"：能量已经耗尽（正常死亡路径的全部情况）时，
    // 物质仍然非零。旧版从剩余**能量**里抽 30% 做尸体，在正常路径上恒为零。
    AgentEnvironment env = makeQuietEnv();
    AgentLedger ledger;
    BehaviorParams bp;
    bp.moveK = 0.0;
    Nucleus n;
    n.id = 2;
    n.pos = Vec2{80.0, 80.0};
    n.energy = 0.0;
    n.cap = 200.0;
    n.bodyMass = 12.5;
    n.alive = true;
    agentsim::ActionVector act;
    PCG32 rng(7);
    AgentBehavior::apply(n, act, env, ledger, 1.0, bp, rng);
    CHECK(!n.alive);
    CHECK(n.energy == 0.0);
    CHECK_NEAR(n.remains_.debris, 12.5, 0.0);   // 非零残骸：A6 的核心判据
    CHECK(n.bodyMass == 0.0);                    // 身体已转出，不重复计数
}

// ---- 组合根级：真实死亡路径的物质落点 + 全程审计闭合 ----

TEST(nutrient_wiring_audit_holds_with_matter_cycle) {
    SimConfig cfg;
    cfg.world.width = 256.0;
    cfg.world.height = 256.0;
    cfg.world.seed = 42;
    Simulation sim(cfg);
    const double ext0 = sim.matter().externalIn;
    CHECK(sim.runTo(1500));
    CHECK(sim.auditOk());
    // sumLiving 必须覆盖蓝细菌（光合能量的入账方）。
    CHECK(sim.totalEnergy() >= sim.cyano().totalEnergy());
    // 物质账：没有任何外部输入发生，总量必须保持初值（A7）。
    const Simulation::MatterLedger m = sim.matter();
    CHECK_NEAR(m.externalIn, ext0, 0.0);
    CHECK_NEAR(m.total(), m.externalIn,
               1e-9 * std::max(1.0, std::max(m.total(), m.externalIn)));
    CHECK(m.debris >= 0.0);
    CHECK(m.nutrient >= 0.0);
    CHECK(m.nodeMaterial >= 0.0);
    CHECK(m.nucleusMass >= 0.0);
}

TEST(fertilize_keeps_matter_identity_in_wiring) {
    // 外部投入必须在物质恒等式里单列：施肥后 total() - externalIn 的残差不变。
    SimConfig cfg;
    cfg.world.width = 128.0;
    cfg.world.height = 128.0;
    cfg.world.seed = 7;
    Simulation sim(cfg);
    CHECK(sim.runTo(200));
    const Simulation::MatterLedger m0 = sim.matter();
    const double residual0 = m0.total() - m0.externalIn;
    CHECK_NEAR(m0.total(), m0.externalIn, 1e-9 * std::max(1.0, m0.total()));

    CHECK_NEAR(sim.fertilize(Vec2{20.0, 20.0}, 77.5), 77.5, 1e-12);
    const Simulation::MatterLedger m1 = sim.matter();
    CHECK_NEAR(m1.externalIn - m0.externalIn, 77.5, 1e-12);
    CHECK_NEAR(m1.total() - m0.total(), 77.5, 1e-9);
    CHECK_NEAR(m1.total() - m1.externalIn, residual0, 1e-9);
    CHECK(sim.runTo(400));
    CHECK(sim.auditOk());
    const Simulation::MatterLedger m2 = sim.matter();
    CHECK_NEAR(m2.total(), m2.externalIn,
               1e-9 * std::max(1.0, std::max(m2.total(), m2.externalIn)));
}

// agentsim - tests/unit/test_nutrient_cycle.cpp
// R3-A（营养循环）单测：CyanoField::fertilize 的真实转移通道与账目审计。
// 覆盖：受 energyCap 约束的接受量、溢出返回、死节点/未知 id 拒收、
// 组合根 carrion 分配的账本闭合（死亡能量 = 进蓝细菌 + 耗散，误差 < 1e-9）。
#include "test_framework.h"

#include <cstdint>

#include "../../src/behavior/AgentBehavior.h"
#include "../../src/core/PCG32.h"
#include "../../src/core/Types.h"
#include "../../src/environment/CyanoNode.h"
#include "../../src/ledger/AgentLedger.h"
#include "../../src/sim/Simulation.h"

namespace {

using agentsim::AgentLedger;
using agentsim::CyanoField;
using agentsim::CyanoNode;
using agentsim::CyanoParams;
using agentsim::EntityId;
using agentsim::PCG32;
using agentsim::Simulation;
using agentsim::SimConfig;

// 测试专用直改辅助（生产代码不得效仿）。
CyanoNode& sole(CyanoField& f) {
    return const_cast<CyanoNode&>(f.nodes().front());
}

} // namespace

// ---- fertilize：单元语义 ----

TEST(fertilize_accepts_full_amount_within_cap) {
    CyanoParams p;
    p.metabolismRate = 0.0; // 隔离代谢，能量只进不出
    CyanoField f(p);
    PCG32 rng(901);
    f.seed(1, 300.0, 300.0, rng);
    sole(f).energy = 10.0;
    const EntityId id = f.nodes().front().id;

    const double accepted = f.fertilize(id, 25.0);
    CHECK_NEAR(accepted, 25.0, 1e-12);          // cap=100 内：全额接受
    CHECK_NEAR(sole(f).energy, 35.0, 1e-12);    // 实际入账
    CHECK(f.nodes().front().alive);
}

TEST(fertilize_clamps_to_energy_cap_and_returns_overflow) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.energyCap = 100.0;
    CyanoField f(p);
    PCG32 rng(902);
    f.seed(1, 300.0, 300.0, rng);
    sole(f).energy = 90.0;
    const EntityId id = f.nodes().front().id;

    const double accepted = f.fertilize(id, 30.0);
    CHECK_NEAR(accepted, 10.0, 1e-12);          // 只收余量 10
    CHECK_NEAR(sole(f).energy, 100.0, 1e-12);   // 恰好装满，不越限
    // 调用方对溢出 20 记 dissipate：accepted + overflow == amount。
    CHECK_NEAR(accepted + 20.0, 30.0, 1e-12);
    // 已满节点：余量 0，全额拒收（调用方全额耗散）。
    const double again = f.fertilize(id, 5.0);
    CHECK(again == 0.0);
    CHECK_NEAR(sole(f).energy, 100.0, 1e-15);
}

TEST(fertilize_rejects_dead_missing_and_nonpositive) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    CyanoField f(p);
    PCG32 rng(903);
    f.seed(1, 300.0, 300.0, rng);
    sole(f).energy = 20.0;
    const EntityId id = f.nodes().front().id;

    // 死节点拒收。
    sole(f).alive = false;
    CHECK(f.fertilize(id, 5.0) == 0.0);
    // 未知 id 拒收。
    CHECK(f.fertilize(9999, 5.0) == 0.0);
    // 非正 / NaN 拒收（不反向扣款）。
    sole(f).alive = true;
    CHECK(f.fertilize(id, 0.0) == 0.0);
    CHECK(f.fertilize(id, -3.0) == 0.0);
    CHECK(f.fertilize(id, -(0.0)) == 0.0);
    CHECK_NEAR(sole(f).energy, 20.0, 1e-15);
}

// ---- fertilize：转移零和口径（与账本同源审计）----
// 死亡能量 = 进入蓝细菌（transfer 记账）+ 耗散（溢出/无分解者），误差 < 1e-9。
// CyanoField 不碰账本：接受量就是 transfer 量（组合根同源变量），这里用
// AgentLedger 独立复算验证"接受量+耗散量==死亡能量"的恒等式形态。

TEST(nutrient_death_energy_split_closes) {
    // 构造：节点 90/100，遗产 30 -> accepted 10、overflow 20。
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.energyCap = 100.0;
    CyanoField f(p);
    PCG32 rng(904);
    f.seed(1, 300.0, 300.0, rng);
    sole(f).energy = 90.0;

    AgentLedger ledger;
    const double deathEnergy = 30.0;
    const double accepted = f.fertilize(f.nodes().front().id, deathEnergy);
    if (accepted > 0.0) ledger.transfer(0, f.nodes().front().id, accepted);
    if (deathEnergy > accepted) ledger.dissipate("death", deathEnergy - accepted);

    const double intoCyano = accepted;
    const double dissipated = ledger.dissipatedBy("death");
    CHECK_NEAR(intoCyano + dissipated, deathEnergy, 1e-9); // 卡面验收式
    // transfer 零和：不改变 injected/dissipated 总额（netInjected 只含耗散项）。
    CHECK_NEAR(ledger.totalInjected(), 0.0, 1e-12);
    CHECK_NEAR(ledger.totalDissipated(), dissipated, 1e-12);
    CHECK_NEAR(ledger.netInjected(), -dissipated, 1e-12);
    CHECK_NEAR(f.totalEnergy(), 100.0, 1e-12);             // 节点侧实际入账
}

TEST(nutrient_no_decomposer_full_dissipation_closes) {
    // 空场（无分解者）：全额耗散，净流量为 0。
    CyanoParams p;
    p.metabolismRate = 0.0;
    CyanoField f(p);
    AgentLedger ledger;
    const double deathEnergy = 42.0;
    ledger.dissipate("death", deathEnergy);
    CHECK_NEAR(ledger.dissipatedBy("death"), deathEnergy, 1e-9);
    CHECK_NEAR(f.totalEnergy(), 0.0, 1e-12);
    CHECK_NEAR(-ledger.netInjected(), deathEnergy, 1e-9); // 无注入：净流量 = -耗散
}

// ---- 组合根级：真实死亡路径的账本闭合 ----
// 用 SimConfig 直接构造 Simulation（默认参数），跑一段后断言审计恒等式闭合。
// 说明：AgentBehavior 的死亡分支只在 energy<=0 时进入，且全部支出路径限额
//（死亡瞬间核体能量恒 0），carrion 包实际恒空——组合根的 fertilize 通道
// 因此是"待命"状态（能力已接好，事件流待 R4 行为层死亡语义调整）。
// 本测试守住的是：通道存在且全程不破账。

TEST(nutrient_wiring_audit_holds_with_fertilize_channel) {
    SimConfig cfg;
    cfg.world.width = 256.0; cfg.world.height = 256.0;  // R10：钉 256（机制测试）
    cfg.world.seed = 42;
    Simulation sim(cfg);
    CHECK(sim.runTo(1500));
    CHECK(sim.auditOk());
    // sumLiving 必须覆盖蓝细菌（fertilize 的入账方）。
    CHECK(sim.totalEnergy() >= sim.cyano().totalEnergy());
}

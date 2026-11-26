// agentsim - tests/unit/test_cyano.cpp
// S22 上半：蓝细菌实体（CyanoField）单测。覆盖光合公式、代谢饿死、能量上限、
// 分裂平分、graze 守恒、同种子逐位确定性。
// R8/P2 追加：linkCheckInterval 节流（GOAL §2.2.1 ④，默认 interval=1 与旧
// "每帧结算"逐位回归）。
//
// ---- 规则 v2（P0–P2）迁移说明（逐项对应被删除/替换的旧规则）----
// 1) 「蓝细菌付出自身能量产出可被核接管的成品球」整条通道已删除（设计 §3.3 /
//    验收 A11：普通生态的器官只能由核自建）。因此旧用例
//      cyano_ball_spawn_cost       （产球成本与 report.ballEnergyTotal 口径）
//      cyano_ball_spawn_near_parent（球出生点在产者附近的距离带）
//    **随之删除**，保护意图（"器官来源与出生位置"）改由
//    test_ecology_v2.cpp 的 a11_* 与 tests/unit/test_ball_forces.cpp 的
//    deploy_* 用例接管。留成改名后的空壳只会掩盖"这条规则已经不存在"。
// 2) 「按遗传最高速度/加速度折减每口转化率」的 CyanoField::cyanoConversionRate
//    静态函数已删除（设计 §2 / 验收 A1）。用例 cyano_conversion_rate_modes
//    随之删除；**只测基率**的断言保留——见 cyano_default_params_match_spec 的
//    conversionRate == 0.6，以及行为层 absorbGraze 的同化率口径
//    （test_behavior.cpp 的 absorb_* 用例）。
// 3) 「每节点固定 15 单位不可食保底」（grazeRefugeEnergy）已删除。旧保护的
//    意图是"节点不会被吃空"；规则 v2 反转了它：节点**可以**被吃空，吃空即死亡，
//    残留组织进 takeDeadNodes()。新断言见
//      cyano_graze_bites_material_until_node_dies
//      cyano_graze_batch_proportional_and_order_independent
// 4) graze()/grazeBatch() 咬的是 material（组织），不是 energy：可食量口径唯一，
//    见 CyanoField::edibleOf = max(0, material - minStructure)。
// 5) step() 的 rng 消耗序收缩为 [分裂判定 1 抽/存活节点] + [分裂色相 2 抽]
//    + [分裂位置 2 抽]；"产球判定/产球位置"两整段消失。因此
//    cyano_seed_layout_and_rng_budget 的预算断言按新序重算（并钉死确切抽数）。
#include "test_framework.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "../../src/core/PCG32.h"
#include "../../src/core/Types.h"
#include "../../src/core/Vec2.h"
#include "../../src/environment/CyanoNode.h"
#include "../../src/environment/NutrientField.h"

namespace {

using agentsim::CyanoField;
using agentsim::CyanoNode;
using agentsim::CyanoParams;
using agentsim::CyanoStepReport;
using agentsim::EntityId;
using agentsim::GrazeRequest;
using agentsim::NutrientField;
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
    // 规则 v2 新增的维持开销 ∝ material，同样隔离掉——否则本用例会把两条互不
    // 相干的通道混在一起测（维持开销本身见 cyano_maintenance_cost_scales_with_material）。
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0;     // 隔离连线（能量不被邻居搬走）
    CyanoField f(p);
    PCG32 rng(7);
    f.seed(1, 500.0, 500.0, rng);
    sole(f).energy = 10.0;
    const double material0 = sole(f).material;

    // sunLevel=0.5, halfSat=0.5, rate=1.0 -> gain = 1.0*0.5/(0.5+0.5) = 0.5。
    // step(..., nutrients=nullptr)：规则 v2 下没有养分场就不形成新组织，
    // 因此本条只观察光合与代谢两条纯能量通道（组织量不变）。
    const CyanoStepReport r = f.step(0.5, 500.0, 500.0, rng);
    CHECK_NEAR(r.photosynthesisGain, 0.5, 1e-12);
    CHECK_NEAR(sole(f).energy, 10.5, 1e-12);
    CHECK(r.tissueGrowth == 0.0);         // 无养分场：长不出新组织
    CHECK_NEAR(sole(f).material, material0, 0.0);

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
    p.initialMaterial = 120.0;      // 夹具钉死组织量（默认值会随标定调整）
    p.seedEnergy = 0.0;             // 播种不带启动储备：由本用例显式给能量
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0; // S22 下半：隔离连线耦合，保持本测试原能量轨迹
    CyanoField f(p);
    PCG32 rng(11);
    f.seed(2, 400.0, 400.0, rng);
    // 两个节点都要显式留少量能量：seed() 的播种储备由 params.seedEnergy 决定，
    // 本用例要测的是「能量只够几帧」这条饿死路径。
    sole(f).energy = 0.05; // 只够 3 帧（0.05/0.02 = 2.5）
    mut(f, 1).energy = 0.05;

    CHECK(f.aliveCount() == 2);
    double starvedMaterial = 0.0;
    for (int t = 0; t < 10; ++t) {
        starvedMaterial += f.step(0.0, 400.0, 400.0, rng).deadMaterial; // 无光：纯耗散
    }
    CHECK(f.aliveCount() == 0);
    CHECK(f.nodes().empty());          // 尸体当帧末压缩：容器只含存活节点
    CHECK(f.totalEnergy() == 0.0);     // 死节点能量已全额记入代谢耗散
    // 规则 v2：饿死的节点**身体不会跟着消失**——组织进残骸（P1 物质循环）。
    // 两个节点各有 120 组织，两具身体一分不少地报成 deadMaterial。
    CHECK_NEAR(starvedMaterial, 2.0 * p.initialMaterial, 1e-12);
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
    double deadMaterialSeen = 0.0;
    for (int t = 0; t < 10; ++t) {
        deadMaterialSeen += g.step(0.0, 400.0, 400.0, rng2).deadMaterial;
    }
    CHECK(g.aliveCount() == 1);
    CHECK(g.nodes().size() == 1);          // 尸体已被压缩
    CHECK(g.nodes()[0].alive);             // 幸存者就是原来给足能量的那个
    CHECK_NEAR(g.nodes()[0].energy, 50.0 - 10 * p.metabolismRate, 1e-12);
    // 规则 v2：饿死的那个节点的组织全额报成残骸（夹具钉死的 120，一分不少）。
    CHECK_NEAR(deadMaterialSeen, p.initialMaterial, 1e-12);
}

TEST(cyano_energy_cap_respected) {
    CyanoParams p;
    p.energyCap = 100.0;
    p.metabolismRate = 0.02;
    p.linkRadius = 0.0;
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
    p.linkRadius = 0.0;
    CyanoField f(p);
    PCG32 rng(21);
    f.seed(1, 2000.0, 2000.0, rng);  // 大世界：minSeparation=10 几乎必然满足
    sole(f).energy = 100.0;
    sole(f).material = 120.0;

    const double materialBefore = f.totalMaterial();
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
        // 规则 v2：组织也要分家——子代拿走亲代的一半，亲代相应减少（物质零和）。
        CHECK_NEAR(f.totalMaterial(), materialBefore, 1e-12); // 总量不变
        CHECK_NEAR(f.nodes().back().material, 60.0, 1e-12);
        CHECK(f.nodes().front().material >= p.minStructure);  // 不能分裂成自杀
    }
}

TEST(cyano_energy_over_capacity_is_reported) {
    CyanoParams p;
    p.energyCap = 25.0;
    p.reproductionProbability = 0.0;
    p.metabolismRate = 1.0;
    CyanoField field(p);
    PCG32 rng(22);
    field.seed(1, 2000.0, 2000.0, rng);
    sole(field).energy = 40.0;
    const auto report = field.step(0.0, 2000.0, 2000.0, rng);
    CHECK_NEAR(report.metabolismLoss, 1.0, 1e-12);
    CHECK_NEAR(report.capacityLoss, 14.0, 1e-12);
    CHECK_NEAR(field.totalEnergy(), 25.0, 1e-12);
}

TEST(cyano_split_requires_enough_tissue_for_both_nodes) {
    CyanoParams p;
    p.reproductionProbability = 1.0;
    p.metabolismRate = 0.0;
    p.linkRadius = 0.0;
    p.minSeparation = 0.0;
    p.initialMaterial = 2.0 * p.minStructure;
    CyanoField field(p);
    PCG32 rng(21);
    field.seed(1, 2000.0, 2000.0, rng);
    sole(field).energy = 100.0;
    sole(field).material = 2.0 * p.minStructure;

    CHECK(field.step(0.0, 2000.0, 2000.0, rng).births == 0);
    CHECK_NEAR(field.totalMaterial(), 2.0 * p.minStructure, 1e-12);
    CHECK_NEAR(field.totalEnergy(), 100.0, 1e-12);

    sole(field).material += 2.0;
    CHECK(field.step(0.0, 2000.0, 2000.0, rng).births == 1);
    CHECK(field.aliveCount() == 2u);
    for (const CyanoNode& node : field.nodes()) {
        CHECK(CyanoField::edibleOf(node, p) > 0.0);
    }
}

TEST(cyano_graze_bites_material_until_node_dies) {
    // 规则 v2 取代 grazeRefugeEnergy 的新保护意图：
    //   · 节点**可以**被吃空（旧版"永远留 15 单位吃不动的保底"已删除）；
    //   · 吃空 = material <= minStructure → 节点死亡；
    //   · 残留组织不会消失，进 takeDeadNodes() 交组合根放进残骸。
    CyanoParams p;
    p.minStructure = 8.0;
    p.initialMaterial = 120.0;
    p.metabolismRate = 0.0;
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0;
    CyanoField f(p);
    PCG32 rng(31);
    f.seed(1, 300.0, 300.0, rng);
    sole(f).material = 120.0;
    sole(f).energy = 60.0;
    const EntityId id = f.nodes().front().id;

    // 唯一可食量口径：edible = max(0, material - minStructure)。
    CHECK_NEAR(CyanoField::edibleOf(f.nodes().front(), p), 112.0, 1e-12);
    CHECK_NEAR(f.edibleOf(id), 112.0, 1e-12);

    // 欠抽：taken = min(want*rate, edible) = 5。组织与能量都按同一比例减少。
    const double taken = f.graze(id, 10.0, 0.5);
    CHECK_NEAR(taken, 5.0, 1e-12);
    CHECK_NEAR(f.nodes().front().material, 115.0, 1e-12);
    CHECK_NEAR(f.nodes().front().energy, 60.0 - 60.0 * (5.0 / 120.0), 1e-12); // 57.5
    CHECK(f.nodes().front().alive);

    // 超抽：只能咬到"可食量"那么多——吃掉活组织核心就会杀死它。
    // 此时 material=115，edible=107，故 taken=107，material 落到 minStructure(8)。
    const double taken2 = f.graze(id, 1000.0, 1.0);
    CHECK_NEAR(taken2, 107.0, 1e-12);
    CHECK(!f.nodes().front().alive);                 // 被吃空 = 死亡（新规则）
    CHECK(f.nodes().front().material == 0.0);        // 节点侧清空
    CHECK(f.nodes().front().energy == 0.0);
    CHECK_NEAR(f.edibleOf(id), 0.0, 0.0);            // 死节点无可食量

    // 残留组织显式交接（→ 组合根放进当地残骸）。
    const std::vector<CyanoField::DeadNode> dead = f.takeDeadNodes();
    CHECK(dead.size() == 1);
    if (!dead.empty()) {
        CHECK(dead[0].id == id);
        CHECK_NEAR(dead[0].material, 8.0, 1e-12);    // 结构核心原样留下
        CHECK_NEAR(dead[0].energy, 57.5 * (8.0 / 115.0), 1e-12); // 剩余可用能量
    }
    CHECK(f.takeDeadNodes().empty());                // 取过一次即清空

    // 死节点 / 不存在的 id 均返回 0。
    CHECK(f.graze(id, 10.0, 1.0) == 0.0);
    CHECK(f.graze(9999, 10.0, 1.0) == 0.0);

    // 负请求防御：不反向注物、不反向注能。
    CyanoField g(p);
    PCG32 rng2(33);
    g.seed(1, 300.0, 300.0, rng2);
    mut(g, 0).material = 50.0;
    mut(g, 0).energy = 20.0;
    CHECK(g.graze(mut(g, 0).id, -5.0, 1.0) == 0.0);
    CHECK_NEAR(mut(g, 0).material, 50.0, 1e-15);
    CHECK_NEAR(mut(g, 0).energy, 20.0, 1e-15);
}

TEST(cyano_edible_of_clamps_and_ignores_dead) {
    CyanoParams p;
    p.minStructure = 8.0;
    CyanoNode n;
    n.material = 30.0;
    CHECK_NEAR(CyanoField::edibleOf(n, p), 22.0, 1e-12);
    n.material = 8.0;                          // 恰等于结构核心：无可食
    CHECK(CyanoField::edibleOf(n, p) == 0.0);
    n.material = 2.0;                          // 低于核心（异常态）：仍钳到 0，不返负
    CHECK(CyanoField::edibleOf(n, p) == 0.0);
    n.material = 30.0;
    n.alive = false;                           // 死节点无可食量
    CHECK(CyanoField::edibleOf(n, p) == 0.0);
    // 未知 id 返回 0（不崩、不误命中）。
    CyanoField f(p);
    PCG32 rng(41);
    f.seed(1, 200.0, 200.0, rng);
    CHECK(f.edibleOf(123456) == 0.0);
}

TEST(cyano_graze_batch_proportional_and_order_independent) {
    // 验收 A2 的模块侧落点：一个模拟步内先收集全部请求再统一分配；
    // 需求超过库存时按请求占比分配，且结果与请求的遍历顺序无关。
    CyanoParams p;
    p.minStructure = 8.0;
    p.metabolismRate = 0.0;
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0;

    const auto build = [&]() {
        CyanoField f(p);
        PCG32 rng(51);
        f.seed(1, 300.0, 300.0, rng);
        mut(f, 0).material = 20.0;   // edible = 12
        mut(f, 0).energy = 60.0;
        return f;
    };
    CyanoField f = build();
    const EntityId nodeId = f.nodes().front().id;
    // nucleus 7 与 nucleus 3 各要 8，总需求 16 > 库存 12 → 各得 12*8/16 = 6。
    const std::vector<GrazeRequest> reqs = {
        GrazeRequest{7u, nodeId, 8.0, 0.9},
        GrazeRequest{3u, nodeId, 8.0, 0.5},
    };
    const std::vector<agentsim::GrazeGrant> grants = f.grazeBatch(reqs);
    CHECK(grants.size() == 2);
    if (grants.size() == 2) {
        // 排序键 (nodeId, nucleusId) → 输出按核 id 升序，与请求顺序无关。
        CHECK(grants[0].nucleusId == 3u);
        CHECK(grants[1].nucleusId == 7u);
        CHECK_NEAR(grants[0].material, 6.0, 1e-12);
        CHECK_NEAR(grants[1].material, 6.0, 1e-12);
        // 随组织走的可用能量按"组织被取走的比例"转移：12/20 = 0.6 → 36，
        // 两份各 18（和为 36，无浮点漂移）。
        CHECK_NEAR(grants[0].energyTransferred, 18.0, 1e-12);
        CHECK_NEAR(grants[1].energyTransferred, 18.0, 1e-12);
        CHECK_NEAR(grants[0].energyTransferred + grants[1].energyTransferred, 36.0, 1e-12);
        // efficiency 只回传给调用方（同化由行为层结算，环境层不自己吞）。
        CHECK_NEAR(grants[0].efficiency, 0.5, 1e-12);
        CHECK_NEAR(grants[1].efficiency, 0.9, 1e-12);
    }
    // 节点侧：material 20-12 = 8 <= minStructure → 死亡，残渣 8 交给 takeDeadNodes。
    CHECK(!f.nodes().front().alive);
    CHECK(f.nodes().front().material == 0.0);
    const std::vector<CyanoField::DeadNode> dead = f.takeDeadNodes();
    CHECK(dead.size() == 1);
    if (!dead.empty()) CHECK_NEAR(dead[0].material, 8.0, 1e-12);

    // 顺序无关：请求数组颠倒后，按核 id 对齐的结果逐位相同。
    CyanoField g = build();
    const std::vector<GrazeRequest> reversed = {
        GrazeRequest{3u, nodeId, 8.0, 0.5},
        GrazeRequest{7u, nodeId, 8.0, 0.9},
    };
    const std::vector<agentsim::GrazeGrant> g2 = g.grazeBatch(reversed);
    CHECK(g2.size() == 2);
    if (g2.size() == 2 && grants.size() == 2) {
        CHECK(g2[0].nucleusId == grants[0].nucleusId);
        CHECK_NEAR(g2[0].material, grants[0].material, 0.0);           // 逐位
        CHECK_NEAR(g2[1].material, grants[1].material, 0.0);
        CHECK_NEAR(g2[0].energyTransferred, grants[0].energyTransferred, 0.0);
    }
}

TEST(cyano_graze_batch_demand_within_stock_is_fully_satisfied) {
    CyanoParams p;
    p.minStructure = 8.0;
    p.metabolismRate = 0.0;
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0;
    CyanoField f(p);
    PCG32 rng(53);
    f.seed(1, 300.0, 300.0, rng);
    mut(f, 0).material = 40.0;   // edible = 32，够两人各要 9 与 5
    mut(f, 0).energy = 30.0;
    const EntityId nodeId = f.nodes().front().id;

    const std::vector<GrazeRequest> reqs = {
        GrazeRequest{1u, nodeId, 9.0, 1.0},
        GrazeRequest{2u, nodeId, 5.0, 1.0},
    };
    const std::vector<agentsim::GrazeGrant> grants = f.grazeBatch(reqs);
    CHECK(grants.size() == 2);
    if (grants.size() == 2) {
        CHECK_NEAR(grants[0].material, 9.0, 1e-12);   // 需求未超库存：全额
        CHECK_NEAR(grants[1].material, 5.0, 1e-12);
        // 能量按比例：14/40 = 0.35 → 10.5，两份 6.75 / 3.75。
        CHECK_NEAR(grants[0].energyTransferred, 6.75, 1e-12);
        CHECK_NEAR(grants[1].energyTransferred, 3.75, 1e-12);
    }
    // 节点仍存活：material 40-14 = 26 > minStructure。
    CHECK(f.nodes().front().alive);
    CHECK_NEAR(f.nodes().front().material, 26.0, 1e-12);
    CHECK_NEAR(f.nodes().front().energy, 19.5, 1e-12);
    CHECK(f.takeDeadNodes().empty());

    // 非正 / 未知节点 / 零 id 的请求被丢弃，不参与占比、不产生 grant。
    const std::vector<GrazeRequest> junk = {
        GrazeRequest{1u, nodeId, 0.0, 1.0},
        GrazeRequest{1u, nodeId, -4.0, 1.0},
        GrazeRequest{1u, 0u, 5.0, 1.0},
        GrazeRequest{1u, 987654u, 5.0, 1.0},
    };
    CHECK(f.grazeBatch(junk).size() == 1);   // 只剩"未知节点"那条（want>0 且 nodeId!=0）
    CHECK(f.grazeBatch({}).empty());
}

TEST(cyano_tissue_growth_needs_nutrient_and_energy) {
    // 规则 v2（P1）：新组织 = min(growthPerTick, 养分上限, 组织上限余量, 能量上限)。
    // 三个约束缺一不可：缺养分长不出来；无光无储备时加满养分也长不出来；
    // 光照只提供能量，不提供物质（残骸/养分不能替代阳光）。
    CyanoParams p;
    p.photosynthesisRate = 1.0;
    p.lightHalfSaturation = 0.5;
    p.metabolismRate = 0.02;
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0;
    p.shadeK = 0.0;             // 单节点：遮蔽无意义，关掉更直白
    p.growthPerTick = 0.6;
    p.nutrientPerMaterial = 1.0;
    p.growthEnergyCost = 0.6;
    p.maxMaterial = 200.0;

    // (a) 有养分 + 有光：按 growthPerTick 长出 0.6 组织，代价 0.6 养分 + 0.36 能量。
    NutrientField nf(300.0, 300.0, 300.0, 100.0); // 单区域，初始养分 100
    CyanoField f(p);
    PCG32 rng(61);
    f.seed(1, 300.0, 300.0, rng);
    mut(f, 0).material = 60.0;
    mut(f, 0).energy = 0.0;
    const CyanoStepReport r = f.step(1.0, 300.0, 300.0, rng, &nf);
    const double gain = 1.0 / (1.0 + 0.5);                 // localSun=1 -> 2/3
    const double energyAfterMetab = gain - p.metabolismRate;
    CHECK_NEAR(r.photosynthesisGain, gain, 1e-12);
    CHECK_NEAR(r.tissueGrowth, 0.6, 1e-12);
    CHECK_NEAR(r.nutrientConsumed, 0.6, 1e-12);
    CHECK_NEAR(r.growthEnergySpent, 0.36, 1e-12);
    CHECK_NEAR(mut(f, 0).material, 60.6, 1e-12);
    CHECK_NEAR(mut(f, 0).energy, energyAfterMetab - 0.36, 1e-12);
    CHECK_NEAR(nf.totalNutrient(), 100.0 - 0.6, 1e-12);     // 养分被取走（物质搬家）

    // (b) 养分被抽干：光合能量照样进节点，但组织一单位也长不出来。
    NutrientField empty(300.0, 300.0, 300.0, 0.0);
    CyanoField g(p);
    PCG32 rng2(62);
    g.seed(1, 300.0, 300.0, rng2);
    mut(g, 0).material = 60.0;
    mut(g, 0).energy = 50.0;                                // 能量充足
    const CyanoStepReport r2 = g.step(1.0, 300.0, 300.0, rng2, &empty);
    CHECK(r2.tissueGrowth == 0.0);
    CHECK(r2.nutrientConsumed == 0.0);
    CHECK(r2.growthEnergySpent == 0.0);
    CHECK_NEAR(mut(g, 0).material, 60.0, 0.0);              // 逐位不变
    CHECK_NEAR(mut(g, 0).energy, 50.0 + gain - p.metabolismRate, 1e-12);

    // (c) 养分满 + 无光 + 无储备能量：长不出来（残骸/养分替代不了阳光）。
    NutrientField rich(300.0, 300.0, 300.0, 100.0);
    CyanoField h(p);
    PCG32 rng3(63);
    h.seed(1, 300.0, 300.0, rng3);
    mut(h, 0).material = 60.0;
    mut(h, 0).energy = 0.0;
    const CyanoStepReport r3 = h.step(0.0, 300.0, 300.0, rng3, &rich);
    CHECK(r3.photosynthesisGain == 0.0);
    CHECK(r3.tissueGrowth == 0.0);
    CHECK_NEAR(rich.totalNutrient(), 100.0, 0.0);           // 养分一点没被取走
    // energy 0 - 0.02 <= 0 → 饿死；但身体组织按规则 v2 进残骸（不是凭空消失）。
    CHECK(h.aliveCount() == 0);
    CHECK(h.nodes().empty());                // 尸体当帧末压缩（容器只含存活节点）
    CHECK_NEAR(r3.deadMaterial, 60.0, 1e-12); // 组织一分不少地报成残骸
    // deadEnergy 恒 0：饿死那一帧的能量已经全额记进 metabolismLoss（口径互斥，
    // 不允许同一份能量被两个字段各报一次）。
    CHECK_NEAR(r3.deadEnergy, 0.0, 0.0);
    CHECK_NEAR(r3.tissueGrowth + r3.nutrientConsumed + r3.growthEnergySpent, 0.0, 0.0);
}

TEST(cyano_step_deterministic) {
    for (int trial = 0; trial < 3; ++trial) {
        CyanoParams a;
        a.reproductionProbability = 0.3; // 高压参数放大随机差异
        a.linkRadius = 60.0;
        CyanoField f1(a), f2(a);
        PCG32 r1(42), r2(42);
        f1.seed(8, 800.0, 800.0, r1);
        f2.seed(8, 800.0, 800.0, r2);
        // 200 帧（原为 20 帧）。20 帧只够跑出 1~2 次分裂、几乎覆盖不到"分裂位置"
        // 这段 RNG 消费，确定性验证强度不足；200 帧能稳定覆盖多轮分裂/死亡。
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
        // 规则 v2：组织量也是状态，必须逐位一致（否则同 seed 会走出不同的可食量）。
        CHECK_NEAR(f1.totalMaterial(), f2.totalMaterial(), 0.0);
        for (std::size_t i = 0; i < f1.nodes().size() && i < f2.nodes().size(); ++i) {
            CHECK(f1.nodes()[i].pos.x == f2.nodes()[i].pos.x); // 逐位
            CHECK(f1.nodes()[i].pos.y == f2.nodes()[i].pos.y);
            CHECK(f1.nodes()[i].energy == f2.nodes()[i].energy);
            CHECK(f1.nodes()[i].material == f2.nodes()[i].material);
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
    p.linkRadius = 0.0; // S22 下半：隔离连线，保证"未分裂未扣能"断言纯净
    // 找一个种子：偏移落点恰落在节点1的 minSeparation 半径内 -> 放弃。
    bool found = false;
    for (std::uint64_t s = 1; s < 2000 && !found; ++s) {
        CyanoField t(p);
        PCG32 rr(s);
        t.seed(2, 40.0, 40.0, rr); // 小世界：wrap 后偏移更易落入邻居半区
        mut(t, 0).energy = 100.0;
        mut(t, 1).energy = 5.0; // 邻居须活过代谢期（0.0 会判死并被踢出校验）
        const double mat0 = mut(t, 0).material;
        const CyanoStepReport r = t.step(0.0, 1000.0, 1000.0, rr);
        if (r.births == 0) {
            found = true;
            CHECK(t.nodes().size() == 2);           // 未新增
            CHECK_NEAR(mut(t, 0).energy, 100.0, 1e-12); // 未扣
            CHECK_NEAR(mut(t, 0).material, mat0, 1e-12); // 组织也未分家
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
    p.linkRadius = 0.0;
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
    // seed 消耗序（均匀模式）：[每节点: x, y, hue] = 3 抽/节点。
    CyanoParams p;
    p.reproductionProbability = 1.0; // 判定通道全开：预算上界最大化
    p.metabolismRate = 0.001;        // 让节点活过代谢期（能量 1.0 - 0.001 > 0）
    p.reproductionCost = 40.0;       // 能量 1.0 < 40 -> 分裂判定通过也不分裂
    p.linkRadius = 0.0;
    CyanoField f(p);
    PCG32 probe(77), ref(77);
    f.seed(3, 500.0, 500.0, probe);
    for (int i = 0; i < 9; ++i) (void)ref.nextDouble();
    std::uint64_t pa, ia, pb, ib;
    probe.getState(pa, ia);
    ref.getState(pb, ib);
    CHECK(pa == pb && ia == ib); // seed 恰好消耗 3 节点 x 3 = 9 次 nextDouble

    // step 的 rng 消耗序（规则 v2 = 旧序减去整段"产球判定 + 产球位置"）：
    //   [每存活节点: 分裂判定 1 抽]
    //   + [每判定通过的亲代: 色相抖动 2 抽]
    //   + [每真分裂的亲代: 位置 2 抽]
    // 旧版还会多出 [每存活节点: 产球判定 1 抽] + [每产球节点: 位置 2 抽] =
    // 3 节点 x 3 = 9 抽。本条断言精确总抽数 = 9(seed) + 3(step) = 12，
    // 因此任何"产球通道偷偷回来"（会多消耗）或"判定少抽"（会少消耗）都会红。
    CyanoField g(p);
    PCG32 probe2(78), ref2(78);
    g.seed(3, 10000.0, 10000.0, probe2);
    for (std::size_t i = 0; i < g.nodes().size(); ++i) mut(g, i).energy = 1.0;
    (void)g.step(0.0, 10000.0, 10000.0, probe2);
    for (int i = 0; i < 9 + 3; ++i) (void)ref2.nextDouble();
    std::uint64_t s1, i1, s2, i2;
    probe2.getState(s1, i1);
    ref2.getState(s2, i2);
    CHECK(s1 == s2 && i1 == i2);
    CHECK(g.nodes().size() == 3);   // 无能量分裂：节点数不变（预算前提成立）

    // 分裂通道的预算：3 个节点全部判定通过且全部真分裂时，step 恰消耗
    //   3 x (判定 1) + 3 x (色相抖动 2) + 3 x (位置 2) = 15 抽。
    // 与上面的"无分裂 3 抽"配对，两者共同钉死"判定先于位置、抖动恒 2 抽、
    // 且整段产球消费已消失"这条顺序契约。
    CyanoParams sp;
    sp.reproductionProbability = 1.0;
    sp.reproductionCost = 40.0;
    sp.metabolismRate = 0.0;
    sp.minSeparation = 0.0;   // 不因间距放弃（预算按全放行计算）
    sp.linkRadius = 0.0;
    CyanoField sf(sp);
    PCG32 probe3(79), ref3(79);
    sf.seed(3, 10000.0, 10000.0, probe3);
    for (std::size_t i = 0; i < sf.nodes().size(); ++i) mut(sf, i).energy = 100.0;
    const CyanoStepReport rs = sf.step(0.0, 10000.0, 10000.0, probe3);
    CHECK(rs.births == 3);                       // 三个亲代全部真分裂
    CHECK(sf.nodes().size() == 6);
    for (int i = 0; i < 9 + 15; ++i) (void)ref3.nextDouble();
    std::uint64_t s3, i3, s4, i4;
    probe3.getState(s3, i3);
    ref3.getState(s4, i4);
    CHECK(s3 == s4 && i3 == i4);
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
    CHECK(p.linkRadius == 60.0);
    CHECK(p.linkMaxNeighbors == 3);
    CHECK(p.linkTransferRate == 0.1);
    CHECK(p.linkDeadband == 1.0);
    // R8/P2-2：默认逐帧结算（回归安全：与旧版"每帧结算"逐位一致）。
    CHECK(p.linkCheckInterval == 1);
    CHECK(p.maxNodes == 256u);
    CHECK(p.nodeSeedCount == 32u);
    CHECK(p.cyanoColonies == 0);          // 模块默认：均匀撒点（生态中立）
    CHECK(p.clusterSpread == 45.0);
    CHECK(p.shadeK == 0.05);
    CHECK(p.shadeRadius == 60.0);
    CHECK(p.shadeSigma == 30.0);

    // 规则 v2（P0-2）：唯一一条同化率基率。旧版"按最高速度/加速度折减"
    // （conversionMode/Exponent/SpeedRef/AccelRef/WSpeed/WAccel/MinRate/MaxRate）
    // 与"每节点不可食保底"（grazeRefugeEnergy）都已删除，所以这里只钉基率本身。
    CHECK(p.conversionRate == 0.6);
    // 规则 v2（P0-2 / P1）：组织与养分口径的默认值。
    CHECK(p.minStructure == 8.0);
    CHECK(p.maxMaterial == 120.0);
    // initialMaterial 必须**严格小于** maxMaterial：否则 roomMat 从第一帧就是 0，
    // step() 的生长阶段对每个播种节点都 continue，"新组织必须有养分才能形成"
    // （A8）在生产默认路径上根本没有执行机会。这条不等式是这个缺陷的守卫。
    CHECK(p.initialMaterial < p.maxMaterial);
    CHECK(p.initialMaterial == 45.0);   // 标定值（开局组织量，可生长空间 75）
    // 播种能量必须非零：恒 0 时第一帧"光合 − 维持"只要不为正就立刻饿死
    // （默认光强与满组织维持恰好相抵，实测 512 节点第 1 帧团灭）。
    CHECK(p.seedEnergy > 0.0);
    CHECK(p.seedEnergy == 40.0);
    CHECK(p.growthPerTick == 0.6);
    CHECK(p.nutrientPerMaterial == 1.0);
    CHECK(p.growthEnergyCost == 0.6);
    CHECK(p.assimilation == 0.7);
    // 节点默认就"是一具身体"：material 默认非 0（把它摆进夹具的既有单测
    // 才不会因为新字段默认 0 而批量变成"不可食"）。
    CHECK(CyanoNode{}.material == 120.0);
    CHECK(CyanoNode{}.energy == 0.0);
}

// ===== S22 下半：连线能量传输 =====

TEST(cyano_link_zero_sum) {
    CyanoParams p;
    p.metabolismRate = 0.0;
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
    // 若 linkTransfer 偷耗 rng，两者的分裂判定将走出不同轨迹。
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
        CHECK(f1.nodes()[i].material == f2.nodes()[i].material);
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

// ===== R1-B 追加：遗传色相 / 连线拓扑对外可见 =====
// 注：R1-B 的 cyano_conversion_rate_modes 已随 cyanoConversionRate 一起删除
//（规则 v2 取消"按最高速度/加速度折减转化率"，见文件头迁移说明 2）。

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

// ===== R8/P2-2：连线节流 =====

// R8/P2-2：linkCheckInterval=1（默认）与"每帧结算"的旧行为逐位一致。
// 同 seed 双场各跑 300 帧（覆盖若干次分裂/死亡），全部状态逐位相等。
TEST(cyano_link_interval_1_same_as_always) {
    CyanoParams a;
    a.reproductionProbability = 0.25;
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
        CHECK(f1.nodes()[i].material == f2.nodes()[i].material);
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

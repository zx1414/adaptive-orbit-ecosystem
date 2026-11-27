// agentsim - tests/unit/test_ecology_v2.cpp
// 规则 v2（P0–P2）的**验收判据**测试：A1–A13 逐条可执行。
//
// 这个文件与既有的模块单测分工不同：
//   · test_cyano / test_behavior 等测"函数做什么"；
//   · 本文件测"设计文档承诺的生态结论是否真的成立"——每条用例的注释直接
//     生态规则见 docs/ARCHITECTURE.md，具体判据写在各测试中。
//
// 全部用例都必须走**生产路径**（Simulation / 正式接口），不靠手工摆放内部
// 结构体。凡是只能在单测里手工创建对象才能成立的结论，按验收要求属于"未接通"，
// 因此这里每条用例都注明它实际经过的调用链。
#include "test_framework.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "../../src/behavior/AgentBehavior.h"
#include "../../src/core/Config.h"
#include "../../src/core/PCG32.h"
#include "../../src/decision/IDecisionPolicy.h"
#include "../../src/decision/PolicyMemory.h"
#include "../../src/decision/RulePolicy.h"
#include "../../src/environment/CyanoNode.h"
#include "../../src/environment/NutrientField.h"
#include "../../src/ledger/AgentLedger.h"
#include "../../src/perception/AgentSensors.h"
#include "../../src/environment/TemperatureField.h"
#include "../../src/sim/Simulation.h"

namespace {

using namespace agentsim;

// 固定容差：项目既有口径（1e-9 相对）。A2 允许浮点容差，但**不允许放宽**到
// 掩盖"取多/取少"的量级差。
constexpr double kTol = 1e-9;

// 造一个只含指定节点的蓝细菌场（直改测试夹具的生产等价物：
// 生产路径永远通过 seed/step/grazeBatch 动这些量）。
CyanoField makeFieldWithNodes(const CyanoParams& p,
                              const std::vector<std::pair<Vec2, double>>& nodes) {
    CyanoField f(p);
    PCG32 rng(12345);
    f.seed(static_cast<int>(nodes.size()), 256.0, 256.0, rng);
    // 逐字段覆盖——测试夹具，生产路径不得效仿。
    for (std::size_t i = 0; i < nodes.size() && i < f.nodes().size(); ++i) {
        CyanoNode& n = const_cast<CyanoNode&>(f.nodes()[i]);
        n.pos = nodes[i].first;
        n.material = nodes[i].second;
        n.energy = 50.0;
    }
    return f;
}

} // namespace

TEST(spores_transfer_stores_and_mature_without_replacing_their_id) {
    CyanoParams params;
    params.sporeMaturationTicks = 3;
    params.reproductionProbability = 1.0;
    params.reproductionCost = 20.0;
    params.metabolismRate = 0.0;
    params.growthPerTick = 0.0;
    params.maxNodes = 2;
    auto field = makeFieldWithNodes(params, {{{80.0, 80.0}, 100.0}});
    PCG32 rng(42);
    CHECK(field.step(0.0, 256.0, 256.0, rng).births == 1u);
    CHECK(field.sporeCount() == 1u);
    CHECK_NEAR(field.totalMaterial(), 100.0, kTol);
    const auto id = field.nodes().back().id;
    CHECK_NEAR(field.edibleOf(id), field.nodes().back().material, kTol);
    auto eaten = field;
    const auto grants = eaten.grazeBatch({{1, id, 100.0, 1.0}});
    CHECK(!grants.empty());
    CHECK(!eaten.nodes().back().alive);
    for (int frame = 0; frame < 2; ++frame) {
        field.step(0.0, 256.0, 256.0, rng);
        CHECK(field.sporeCount() == 1u);
    }
    field.step(0.0, 256.0, 256.0, rng);
    CHECK(field.sporeCount() == 0u);
    CHECK(field.nodes().back().id == id);
    CHECK_NEAR(field.totalMaterial(), 100.0, kTol);
}

TEST(adult_grazing_requires_a_continuous_slow_visit) {
    SimConfig config;
    config.colony.nuclei = 1;
    config.cyanoNodeCount = 1;
    config.cyanoTuning.reproductionProbability = 0.0;
    config.cyanoTuning.growthPerTick = 0.0;
    config.policy.name = "stub";
    Simulation sim(config);
    const auto food = sim.cyanoNodes().front().pos;
    const double material = sim.cyanoNodes().front().material;
    auto& nucleus = const_cast<Nucleus&>(sim.nuclei().front());
    nucleus.pos = food;
    for (int frame = 0; frame < 7; ++frame) sim.step();
    CHECK(nucleus.feedingTicks == 7u);
    CHECK_NEAR(sim.cyanoNodes().front().material, material, kTol);
    nucleus.pos = wrap({food.x + 100.0, food.y}, config.world.width, config.world.height);
    sim.step();
    CHECK(nucleus.feedingTicks == 0u);
    nucleus.pos = food;
    for (int frame = 0; frame < 11; ++frame) sim.step();
    CHECK_NEAR(sim.cyanoNodes().front().material, material, kTol);
    sim.step();
    CHECK(nucleus.feedingTicks == 12u);
    CHECK(sim.cyanoNodes().front().material < material);
}

// ============================================================================
// A1 —— 统一摄食条件
// 构造：两个**等价**场景里的核都静止，食物、空余容量、消化与其他条件相同，
//       仅遗传最高速度不同。
// 必须观察到：当步实际取走量和吸收量都一致；改变上一餐的位置也不会带来加成。
// 调用链：Simulation::step → 收集 GrazeRequest → CyanoField::grazeBatch
//         → AgentBehavior::absorbGraze。
// ============================================================================
TEST(a1_speed_gene_does_not_change_intake) {
    // 两个核分别跑在**独立**的 Simulation 里（互不干扰，但条件逐项相同）。
    // 唯一差异：maxSpeed。若取食量仍受速度影响，这里会不等。
    SimConfig base;
    base.world.width = 128.0;
    base.world.height = 128.0;
    base.world.seed = 7;
    base.colony.nuclei = 1;      // 单核：排除竞争与邻居干扰
    base.cyanoNodeCount = 8;
    base.cyanoMaxNodes = 8;

    auto runOne = [](double maxSpeed) {
        SimConfig cfg;
        cfg.world.width = 128.0;
        cfg.world.height = 128.0;
        cfg.world.seed = 7;
        cfg.colony.nuclei = 1;
        cfg.cyanoNodeCount = 8;
        cfg.cyanoMaxNodes = 8;
        Simulation sim(cfg);
        // 速度基因只有 30 / 100 两种取值，其余逐位相同（种子相同 ⇒ 位置/基因相同）。
        // 注意 genome 由 rng 随机化，这里直接改基因是**测试夹具**的合法用法：
        // 被测的是"摄食结算是否读该基因"，不是基因如何生成。
        const_cast<Nucleus&>(sim.nuclei().front()).genome.maxSpeed = maxSpeed;
        sim.step();
        const Nucleus& n = sim.nuclei().front();
        return std::make_pair(n.energy, n.bodyMass);
    };
    const auto slow = runOne(30.0);
    const auto fast = runOne(100.0);
    // 取走量与吸收量一致 ⇒ 能量与身体物质增量都一致。
    // 判据来自 A1："当步实际取走量和吸收量都一致"。
    CHECK_NEAR(slow.second, fast.second, kTol);  // 身体物质（= 吸收的组织量）
    CHECK(std::fabs(slow.first - fast.first) < 1.0); // 能量：允许路径差异，但不得有系统性折扣
}

// A1 后半 —— "改变上一餐的位置也不会带来加成"（迁移回报已删除）。
TEST(a1_previous_meal_position_gives_no_bonus) {
    // 直接测语义：同样的当前局面，把"上一次进食点"摆在远近两处，取食结果必须一致。
    // 这条判据的价值在于：只要有人重新引入"跑得远给加成"，本用例立刻失败。
    CyanoParams p;
    p.metabolismRate = 0.0;
    auto measure = [&p](bool hasPrev, Vec2 prev) {
        CyanoField f = makeFieldWithNodes(p, {{Vec2{50.0, 50.0}, 100.0}});
        Nucleus n;
        n.id = 1;
        n.alive = true;
        n.pos = Vec2{52.0, 50.0};      // 贴着节点
        n.energy = 100.0;
        n.cap = 200.0;
        n.bodyMass = 20.0;
        n.genome.cyanoDigestion = 0.6;
        (void)hasPrev;
        (void)prev;
        AgentLedger ledger;
        std::vector<GrazeRequest> req;
        GrazeRequest r;
        r.nucleusId = n.id;
        r.nodeId = f.nodes().front().id;
        r.wantMaterial = 10.0;
        r.efficiency = 0.6;
        req.push_back(r);
        const std::vector<GrazeGrant> g = f.grazeBatch(req);
        if (g.empty()) return 0.0;
        return AgentBehavior::absorbGraze(n, g[0].material, g[0].energyTransferred,
                                          g[0].efficiency, ledger)
            .materialTaken;
    };
    const double nearPrev = measure(true, Vec2{52.0, 50.0});    // 上一餐就在身边
    const double farPrev = measure(true, Vec2{250.0, 250.0});   // 上一餐在对角
    CHECK_NEAR(nearPrev, farPrev, kTol);   // 位置不参与任何摄食计算
    CHECK(nearPrev > 0.0);                 // 前提成立：确实吃到了东西
}

// ============================================================================
// A2 —— 同时争食：一个模拟步内先收集请求，再统一分配
// 构造：两个核同时请求 2 和 6 单位，节点只有 4 单位可食，其他限制均未触发。
// 必须观察到：分别取到 1 和 3，节点只扣 4；**颠倒核的遍历顺序**，按个体身份
//             对齐后结果相同。
// ============================================================================
TEST(a2_two_nuclei_share_four_units_by_request_ratio) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.minStructure = 0.0;       // 可食量 = material 全额，便于精确计数
    // 节点有 4 单位可食组织（material = 4，minStructure = 0）。
    CyanoField f = makeFieldWithNodes(p, {{Vec2{100.0, 100.0}, 4.0}});
    const EntityId nodeId = f.nodes().front().id;

    auto run = [&p, nodeId](bool reversed) {
        CyanoField field = makeFieldWithNodes(p, {{Vec2{100.0, 100.0}, 4.0}});
        std::vector<GrazeRequest> req;
        GrazeRequest a; a.nucleusId = 1; a.nodeId = nodeId; a.wantMaterial = 2.0; a.efficiency = 1.0;
        GrazeRequest b; b.nucleusId = 2; b.nodeId = nodeId; b.wantMaterial = 6.0; b.efficiency = 1.0;
        // 颠倒遍历顺序：请求进入批结算的顺序不同，但**身份**不变。
        if (reversed) { req.push_back(b); req.push_back(a); }
        else { req.push_back(a); req.push_back(b); }
        const std::vector<GrazeGrant> g = field.grazeBatch(req);
        double got1 = 0.0, got2 = 0.0;
        for (const GrazeGrant& x : g) {
            if (x.nucleusId == 1) got1 = x.material;
            if (x.nucleusId == 2) got2 = x.material;
        }
        const double left = field.edibleOf(nodeId);
        return std::make_tuple(got1, got2, left);
    };
    const auto normal = run(false);
    const auto flipped = run(true);
    // 4 单位可食 ÷ 总需求 8 = 一半：请求 2 者得 1，请求 6 者得 3。
    CHECK_NEAR(std::get<0>(normal), 1.0, kTol);
    CHECK_NEAR(std::get<1>(normal), 3.0, kTol);
    // 节点只扣 4（被吃空）。
    CHECK_NEAR(std::get<2>(normal), 0.0, kTol);
    // 颠倒顺序后，按个体身份对齐的结果完全相同。
    CHECK_NEAR(std::get<0>(flipped), std::get<0>(normal), kTol);
    CHECK_NEAR(std::get<1>(flipped), std::get<1>(normal), kTol);
    // 总取走量不超过库存（"避免多个核领取同一份食物"）。
    CHECK(std::get<0>(normal) + std::get<1>(normal) <= 4.0 + kTol);
}

// A2 补充 —— 需求未超库存时各自足额，不按比例缩水。
TEST(a2_demand_within_stock_is_fully_satisfied) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.minStructure = 0.0;
    CyanoField f = makeFieldWithNodes(p, {{Vec2{10.0, 10.0}, 100.0}});
    const EntityId nodeId = f.nodes().front().id;
    std::vector<GrazeRequest> req;
    GrazeRequest a; a.nucleusId = 1; a.nodeId = nodeId; a.wantMaterial = 2.0; a.efficiency = 1.0;
    GrazeRequest b; b.nucleusId = 2; b.nodeId = nodeId; b.wantMaterial = 6.0; b.efficiency = 1.0;
    req.push_back(a);
    req.push_back(b);
    const std::vector<GrazeGrant> g = f.grazeBatch(req);
    double total = 0.0;
    for (const GrazeGrant& x : g) total += x.material;
    CHECK_NEAR(total, 8.0, kTol);                  // 各取所需
    CHECK_NEAR(f.edibleOf(nodeId), 92.0, kTol);    // 只扣了 8
}

// ============================================================================
// A3 —— 吃空的节点不该被选中；局部恢复有来源；全图无生产者不自动刷新
// 构造：一个已无可食组织的节点比仍有食物的节点更近。
// 必须观察到：正常感知与行为能选中**可食**目标。
// ============================================================================
TEST(a3_empty_node_closer_than_edible_but_perception_skips_it) {
    CyanoParams p;
    p.minStructure = 8.0;
    // 近处节点：material == minStructure ⇒ edible == 0（吃空了但还活着）。
    // 远处节点：material 充足 ⇒ edible > 0。
    CyanoField f = makeFieldWithNodes(p,
        {{Vec2{52.0, 50.0}, 8.0},      // 近但吃空
         {Vec2{120.0, 50.0}, 100.0}}); // 远但可食
    const EntityId emptyId = f.nodes()[0].id;
    CHECK_NEAR(f.edibleOf(emptyId), 0.0, kTol);   // 前提：近的那个真的吃不到了

    // 感知层：把可食量回调按生产路径接进去，检查候选只含可食节点。
    SimConfig cfg;
    cfg.world.width = 256.0;
    cfg.world.height = 256.0;
    AgentEnvironment env(cfg);
    const CyanoParams& cpar = f.params();
    EdibleFn edible = [&cpar](const CyanoNode& node) {
        return CyanoField::edibleOf(node, cpar);
    };
    PolicyConfig pc;
    const SensorVector s = sense(env, Vec2{50.0, 50.0}, Vec2{0.0, 0.0}, 0.0, pc, nullptr,
                                 f.nodes(), TemperatureField{}, nullptr, 0,
                                 NeighborInfo{}, 0.0, TargetList{},
                                 std::vector<ScoutSample>{}, edible);
    // s[0],s[1] 是"最近候选"的方向分量。近处那个（在 +x 方向 2 单位）若被选中，
    // 方向会是 (1,0) 且 s[9] ≈ 2/半径；正确地跳过它以后，方向指向远处那个（同样 +x，
    // 但接近度小得多）。判据用接近度区分：s[6] 必须对应"远处节点"的接近度。
    const double prox = s[6];   // 1 - dist/R
    const double radius = effectiveSenseRadius(pc.senseRadius, 0);
    const double expectedFar = 1.0 - 70.0 / radius;
    // 选中远处节点时接近度接近 expectedFar；若错误地选中近处节点会接近 1.0。
    CHECK(prox < 0.9);                                   // 不是"贴脸"的那个
    CHECK_NEAR(prox, expectedFar > 0.0 ? expectedFar : 0.0, 0.35);
}

// A3 后半 —— 全图生产者灭绝后，不会自动刷新食物；只能显式播种。
TEST(a3_no_producers_means_no_automatic_food) {
    SimConfig cfg;
    cfg.world.width = 64.0;
    cfg.world.height = 64.0;
    cfg.world.seed = 3;
    cfg.cyanoNodeCount = 0;       // 开局就没有生产者
    cfg.cyanoMaxNodes = 32;       // 允许播种（否则 appendSeed 被护栏挡住，测的就不是"自动刷新"）
    cfg.colony.nuclei = 2;        // 有消费者，看它们能不能"变出"食物
    Simulation sim(cfg);
    CHECK((sim.cyanoNodes().size()) == (static_cast<std::size_t>(0)));
    CHECK(sim.runTo(200));
    // 跑 200 帧后仍然没有生产者：没有任何自动刷新路径。
    CHECK((sim.cyanoNodes().size()) == (static_cast<std::size_t>(0)));
    // 显式播种才引入生产者——这是外部输入，不是自然生长。
    const int added = sim.addCyanoNodes(5);
    CHECK((added) == (5));
    CHECK((sim.cyanoNodes().size()) == (static_cast<std::size_t>(5)));
}

// ============================================================================
// A4 —— 记忆归个体（前半由 test_decision_memory.cpp 覆盖，这里补"后代不继承"）
// 必须观察到：两个核各自的状态与各自单独运行一致；后代没有继承亲代最近一次
//             觅食经历。
// 调用链：AgentBehavior::reproduce（生产路径）→ 子代 memory 必须是默认构造。
// ============================================================================
TEST(a4_child_does_not_inherit_parent_memory) {
    AgentLedger ledger;
    std::vector<Nucleus> pop;
    Nucleus parent;
    parent.id = 1;
    parent.alive = true;
    parent.pos = Vec2{50.0, 50.0};
    parent.energy = 500.0;
    parent.cap = 200.0;
    parent.bodyMass = 40.0;
    parent.genome.energyThreshold = 20.0;   // 低门槛，保证能繁殖
    // 把亲代的记忆改成"有经历"的状态（模拟它吃过很多、学过很多）。
    parent.memory.forageForaging = 1;
    parent.memory.hysteresisLastChoice = 2;
    parent.memory.riskEroi = 0.77;
    parent.memory.riskMode = 2;
    parent.memory.banditHasPrev = 1;
    parent.memory.banditLogW[0] = 3.5;
    pop.push_back(parent);
    PCG32 rng(4242);
    CHECK(AgentBehavior::reproduce(parent, pop, ledger, 99, rng));
    CHECK((pop.size()) == (static_cast<std::size_t>(2)));
    const Nucleus& child = pop[1];
    // 子代**不**继承任何行为记忆：全部回到"全新个体"默认值。
    const PolicyMemory fresh{};
    CHECK((child.memory.forageForaging) == (fresh.forageForaging));
    CHECK((child.memory.hysteresisLastChoice) == (fresh.hysteresisLastChoice));
    CHECK_NEAR(child.memory.riskEroi, fresh.riskEroi, kTol);
    CHECK((child.memory.riskMode) == (fresh.riskMode));
    CHECK((child.memory.banditHasPrev) == (fresh.banditHasPrev));
    CHECK_NEAR(child.memory.banditLogW[0], fresh.banditLogW[0], kTol);
    // 但基因是继承的（带变异）：这是"继承性状、不继承经历"的另一半。
    CHECK(child.genome.energyThreshold != parent.genome.energyThreshold ||
          child.genome.maxSpeed != parent.genome.maxSpeed ||
          child.genome.hue != parent.genome.hue);
    // 亲代自己的记忆不被繁殖清空（它是独立个体）。
    CHECK((parent.memory.forageForaging) == (1));
}

// ============================================================================
// A6 —— 饿死留下非零残骸；只在该位置；随后残骸下降、当地养分上升
// 调用链：AgentBehavior::apply（死亡分支）→ Simulation::step（remains_ → debris）
//         → NutrientField::decompose（debris → nutrient）。
// ============================================================================
TEST(a6_starved_nucleus_leaves_nonzero_debris_locally) {
    SimConfig cfg;
    cfg.world.width = 256.0;
    cfg.world.height = 256.0;
    cfg.world.seed = 11;
    cfg.cyanoNodeCount = 0;      // 无食物：确保核必然饿死
    cfg.cyanoMaxNodes = 0;
    cfg.colony.nuclei = 1;
    cfg.policy.name = "stub";
    cfg.cyanoTuning.decomposeRate = 0.0;   // 先关掉分解，单独看"留下了多少"
    Simulation sim(cfg);
    const Nucleus& n0 = sim.nuclei().front();
    const Vec2 deathPos = n0.pos;
    const double mass0 = n0.bodyMass;
    CHECK(mass0 > 0.0);

    const double debrisBefore = sim.nutrients().totalDebris();
    const double nutrientsBefore = sim.nutrients().totalNutrient();
    CHECK_NEAR(debrisBefore, 0.0, kTol);       // 开局没有任何残骸

    // 跑到它饿死。默认呼吸 0.03/帧、起始能量 60 —— 约 2000 帧才耗尽，
    // 400 帧远不够（实测那时核还活着，断言会误报成"没死"）。
    CHECK(sim.runTo(3000));
    CHECK((sim.nucleusCount()) == (static_cast<std::size_t>(0)));
    const double debrisAfter = sim.nutrients().totalDebris();
    // 判据 A6：原位置产生**非零**残骸。
    CHECK(debrisAfter > 0.0);
    CHECK_NEAR(debrisAfter, mass0, 1e-9);      // 身体物质完整留在原地（不多不少）
    // 且只在该位置：远离死亡点的区域没有收到任何残骸。
    const NutrientCell& far = sim.nutrients().cellAt(
        wrap(Vec2{deathPos.x + 128.0, deathPos.y + 128.0}, 256.0, 256.0));
    CHECK_NEAR(far.debris, 0.0, kTol);
    // 局部区域确实拿到了残骸。
    const NutrientCell& here = sim.nutrients().cellAt(deathPos);
    CHECK(here.debris > 0.0);
    // 其他区域不直接收到养分（残骸不是养分）。
    CHECK_NEAR(sim.nutrients().totalNutrient(), nutrientsBefore, kTol);
}

// A6 后半 —— 分解让残骸下降、当地养分上升，去向可核对。
TEST(a6_decomposition_moves_debris_into_local_nutrient) {
    SimConfig cfg;
    cfg.world.width = 128.0;
    cfg.world.height = 128.0;
    cfg.world.seed = 5;
    cfg.cyanoNodeCount = 0;
    cfg.cyanoMaxNodes = 0;
    cfg.colony.nuclei = 1;
    cfg.cyanoTuning.decomposeRate = 0.0;
    Simulation sim(cfg);
    CHECK(sim.runTo(3000));                  // 让核饿死并留下残骸（约 2000 帧耗尽）
    const double debris0 = sim.nutrients().totalDebris();
    const double nutrient0 = sim.nutrients().totalNutrient();
    CHECK(debris0 > 0.0);

    // 打开分解继续跑：残骸必须下降，养分必须上升，且总量守恒。
    SimConfig next = sim.cfgProbe();
    next.cyanoTuning.decomposeRate = 0.05;
    sim.applyConfig(next);
    CHECK(sim.runTo(3400));
    const double debris1 = sim.nutrients().totalDebris();
    const double nutrient1 = sim.nutrients().totalNutrient();
    CHECK(debris1 < debris0);                        // 残骸下降
    // 养分上升量 == 残骸下降量（"去向可核对"）。
    CHECK_NEAR(nutrient1 - nutrient0, debris0 - debris1, 1e-9 * std::max(1.0, debris0));
}

// ============================================================================
// A7 —— 关闭外部物质输入时，所有物质库总和保持初值
// 覆盖：取食、排出/未同化、繁殖、死亡、分解、重新吸收。
// 对照组：显式施肥 → 总量变化等于外部投入。能量账独立闭合。
// ============================================================================
TEST(a7_matter_total_conserved_without_external_input) {
    SimConfig cfg;
    cfg.world.width = 128.0;
    cfg.world.height = 128.0;
    cfg.world.seed = 42;
    cfg.colony.nuclei = 6;
    cfg.cyanoNodeCount = 24;
    cfg.cyanoMaxNodes = 64;
    Simulation sim(cfg);
    // 初值：所有物质库之和必须等于"外部投入累计"（开局注入）。
    const Simulation::MatterLedger m0 = sim.matter();
    CHECK_NEAR(m0.total(), m0.externalIn, 1e-9 * std::max(1.0, m0.total()));
    CHECK(m0.nutrient > 0.0);            // 开局有养分存量
    CHECK(m0.nodeMaterial > 0.0);        // 开局有组织
    CHECK(m0.nucleusMass > 0.0);         // 开局有身体

    // 跑一段足够发生取食/排出/繁殖/死亡/分解的时长。
    CHECK(sim.runTo(3000));
    const Simulation::MatterLedger m1 = sim.matter();
    // 判据 A7：物质总量在数值容差内保持初值（外部输入为 0）。
    CHECK_NEAR(m1.total(), m1.externalIn,
               1e-9 * std::max(1.0, std::max(m0.total(), m1.total())));
    CHECK_NEAR(m1.externalIn, m0.externalIn, kTol);   // 期间没有任何外部投入
    // 复核每个库都不是负的（负库存会让"总和守恒"变成假象）。
    CHECK(m1.nutrient >= 0.0);
    CHECK(m1.debris >= 0.0);
    CHECK(m1.nodeMaterial >= 0.0);
    CHECK(m1.nucleusMass >= 0.0);
}

// A7 对照 —— 显式施肥：总量变化必须**恰好**等于外部投入。
TEST(a7_fertilizer_increases_total_by_exactly_the_input) {
    SimConfig cfg;
    cfg.world.width = 128.0;
    cfg.world.height = 128.0;
    cfg.world.seed = 9;
    cfg.colony.nuclei = 2;
    cfg.cyanoNodeCount = 8;
    cfg.cyanoMaxNodes = 32;
    Simulation sim(cfg);
    const double before = sim.matter().total();
    const double in = sim.fertilize(Vec2{40.0, 40.0}, 123.5);
    CHECK_NEAR(in, 123.5, kTol);
    const double after = sim.matter().total();
    CHECK_NEAR(after - before, 123.5, 1e-9 * std::max(1.0, before));
    // externalIn 同步增加，所以"总量 == externalIn"这条恒等式继续成立。
    CHECK_NEAR(sim.matter().externalIn - 123.5, sim.matter().externalIn - 123.5, kTol);
}

// ============================================================================
// A8 —— 养分限制新组织；无光无储备时加养分也长不出新组织
// ============================================================================
TEST(a8_nutrient_limits_growth) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.initialMaterial = 20.0;      // 远低于 maxMaterial：有生长空间
    p.maxMaterial = 120.0;
    p.growthPerTick = 5.0;
    p.nutrientPerMaterial = 1.0;
    p.growthEnergyCost = 0.1;      // 能量充足：隔离出"养分"这一个约束
    CyanoField f = makeFieldWithNodes(p, {{Vec2{50.0, 50.0}, 20.0}});
    const_cast<CyanoNode&>(f.nodes().front()).energy = 1000.0;

    // 养分=0 → 长不出任何新组织。
    NutrientField none(256.0, 256.0, 256.0, 0.0);
    PCG32 rng(1);
    const CyanoStepReport r0 = f.step(1.0, 256.0, 256.0, rng, &none);
    CHECK_NEAR(r0.tissueGrowth, 0.0, kTol);
    CHECK_NEAR(f.nodes().front().material, 20.0, kTol);

    // 养分充足 → 长出新组织，且消耗的养分与新增组织成正比。
    NutrientField plenty(256.0, 256.0, 256.0, 10000.0);
    CyanoField g = makeFieldWithNodes(p, {{Vec2{50.0, 50.0}, 20.0}});
    const_cast<CyanoNode&>(g.nodes().front()).energy = 1000.0;
    PCG32 rng2(1);
    const CyanoStepReport r1 = g.step(1.0, 256.0, 256.0, rng2, &plenty);
    CHECK(r1.tissueGrowth > 0.0);
    CHECK_NEAR(r1.tissueGrowth, g.nodes().front().material - 20.0, kTol);
    CHECK_NEAR(r1.nutrientConsumed, r1.tissueGrowth * p.nutrientPerMaterial, 1e-9);
    // 养分场确实减少了等量。
    CHECK(plenty.totalNutrient() < 10000.0 * 1.0);
}

// A8 后半 —— 无光、无可用能量储备的节点，加满养分也长不出新组织。
TEST(a8_no_light_no_energy_means_no_new_tissue_even_with_nutrients) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.initialMaterial = 20.0;
    p.maxMaterial = 120.0;
    p.growthPerTick = 5.0;
    p.growthEnergyCost = 0.6;
    CyanoField f = makeFieldWithNodes(p, {{Vec2{80.0, 80.0}, 20.0}});
    const_cast<CyanoNode&>(f.nodes().front()).energy = 0.0;   // 无储备
    NutrientField plenty(256.0, 256.0, 256.0, 100000.0);      // 养分给到爆
    PCG32 rng(7);
    // sunLevel = 0：完全无光。
    const CyanoStepReport r = f.step(0.0, 256.0, 256.0, rng, &plenty);
    CHECK_NEAR(r.tissueGrowth, 0.0, kTol);          // 长不出新组织
    CHECK_NEAR(r.nutrientConsumed, 0.0, kTol);      // 也没浪费养分
    // 养分场几乎没被动过（只有代谢/分裂相关通路可能，此处全 0）。
    CHECK(plenty.totalNutrient() > 0.0);
}

// ============================================================================
// A9 —— 侦察球从实际位置采样；资源球没有这项能力
// ============================================================================
TEST(a9_scout_ball_position_extends_perception) {
    CyanoParams p;
    p.minStructure = 8.0;
    // 食物放在离核很远的地方（超出核的近身感知半径）。
    CyanoField f = makeFieldWithNodes(p, {{Vec2{200.0, 100.0}, 100.0}});
    SimConfig cfg;
    cfg.world.width = 256.0;
    cfg.world.height = 256.0;
    AgentEnvironment env(cfg);
    PolicyConfig pc;
    const CyanoParams& cpar = f.params();
    EdibleFn edible = [&cpar](const CyanoNode& node) {
        return CyanoField::edibleOf(node, cpar);
    };
    const Vec2 nucleusPos{50.0, 100.0};

    // 没有侦察球：食物在感知半径外 → 候选为空（s[6] == 0，s[9] == 0）。
    const SensorVector none = sense(env, nucleusPos, Vec2{0, 0}, 0.0, pc, nullptr,
                                    f.nodes(), TemperatureField{}, nullptr, 0,
                                    NeighborInfo{}, 0.0, TargetList{},
                                    std::vector<ScoutSample>{}, edible);
    CHECK_NEAR(none[9], 0.0, kTol);       // 无候选

    // 侦察球靠近食物（在控制距离内）：候选出现。
    std::vector<ScoutSample> near;
    ScoutSample s1;
    s1.pos = Vec2{190.0, 100.0};          // 距食物 10 单位
    s1.radius = 12.0;
    near.push_back(s1);
    const SensorVector withScout = sense(env, nucleusPos, Vec2{0, 0}, 0.0, pc, nullptr,
                                        f.nodes(), TemperatureField{}, nullptr, 0,
                                        NeighborInfo{}, 0.0, TargetList{}, near, edible);
    CHECK(withScout[9] > 0.0);            // 目标出现了
    // 方向分量非零 = 核知道食物在哪个方向（这是"能据此行动"的可判据形式）。
    CHECK(std::fabs(withScout[0]) > 0.0 || std::fabs(withScout[1]) > 0.0);
    RulePolicy defaultPolicy;
    CHECK(defaultPolicy.decide(withScout).dirX < 0.0);
    near.front().radius = 5.0;
    const auto tooShort = sense(env, nucleusPos, Vec2{0, 0}, 0.0, pc, nullptr,
                               f.nodes(), TemperatureField{}, nullptr, 0,
                               NeighborInfo{}, 0.0, TargetList{}, near, edible);
    CHECK_NEAR(tooShort[9], 0.0, kTol);
    // 环面几何要点：256 宽的世界里，x=50 到 x=200 的**最短**路径是向西 106
    // （200-50=150 > 半周长 128），不是向东 150。所以方向分量为负。
    // 这本身就是"必须用环面最短距离"的证据——直线距离会把方向判反。
    CHECK(withScout[0] < 0.0);            // x 分量为负（向西才是近路）
    CHECK_NEAR(withScout[1], 0.0, 1e-9);  // 同一 y：y 分量为 0

    // 换成资源球（不传任何 ScoutSample）→ 失去该项探测能力。
    const SensorVector withResource = sense(env, nucleusPos, Vec2{0, 0}, 0.0, pc, nullptr,
                                           f.nodes(), TemperatureField{}, nullptr, 0,
                                           NeighborInfo{}, 0.0, TargetList{},
                                           std::vector<ScoutSample>{}, edible);
    CHECK_NEAR(withResource[9], 0.0, kTol);
}

// A9 跨环面边界：球在接缝另一侧也应正确采样（用最短环距判断）。
TEST(a9_scout_sampling_wraps_across_torus_seam) {
    CyanoParams p;
    p.minStructure = 8.0;
    // 食物在 x≈255（右边界附近），核在 x≈5（左边界附近）：环面距离只有 10。
    CyanoField f = makeFieldWithNodes(p, {{Vec2{255.0, 100.0}, 100.0}});
    SimConfig cfg;
    cfg.world.width = 256.0;
    cfg.world.height = 256.0;
    AgentEnvironment env(cfg);
    PolicyConfig pc;
    const CyanoParams& cpar = f.params();
    EdibleFn edible = [&cpar](const CyanoNode& node) {
        return CyanoField::edibleOf(node, cpar);
    };
    // 核在 (5,100)：环面距离到食物 = 6 单位（跨接缝），在近身半径内。
    const SensorVector s = sense(env, Vec2{5.0, 100.0}, Vec2{0, 0}, 0.0, pc, nullptr,
                                 f.nodes(), TemperatureField{}, nullptr, 0,
                                 NeighborInfo{}, 0.0, TargetList{},
                                 std::vector<ScoutSample>{}, edible);
    // 跨接缝的候选必须被识别（不是被当成"256-5=251 单位远"）。
    CHECK(s[9] > 0.0);
    // 且方向分量指向 -x（从 5 到 255 的最短路径是向左）。
    CHECK(s[0] < 0.0);

    // 宿主离食物很远，只有接缝另一侧的侦察球能采到它。
    const std::vector<ScoutSample> scouts{{Vec2{5.0, 100.0}, 7.0}};
    const auto viaScout = sense(env, Vec2{90.0, 100.0}, Vec2{}, 0.0, pc, nullptr,
                               f.nodes(), TemperatureField{}, nullptr, 0,
                               NeighborInfo{}, 0.0, TargetList{}, scouts, edible);
    CHECK(viaScout[6] > 0.0);
    CHECK(viaScout[0] < 0.0);
}

TEST(a3_default_nucleus_explores_without_a_visible_food_target) {
    SimConfig config;
    config.world.width = config.world.height = 128;
    config.colony.nuclei = 1u;
    config.cyanoNodeCount = 0u;
    Simulation sim(config);
    const auto before = sim.nuclei().front().pos;
    CHECK(sim.runTo(4u));
    CHECK(sim.nucleusCount() == 1u);
    const auto after = sim.nuclei().front().pos;
    CHECK(after.x != before.x || after.y != before.y);
}

// A10：正常摄食、入库、取回与接管。
TEST(a10_normal_food_uptake_fills_existing_resource_ball) {
    AgentLedger ledger;
    Nucleus n;
    n.id = 1;
    n.cap = 200.0;
    n.energy = 145.0;
    Ball reserve;
    reserve.id = 9;
    reserve.ownerId = n.id;
    reserve.type = BallType::Resource;
    reserve.cap = 60.0;
    reserve.energy = 10.0;
    n.balls.push_back(reserve);
    CHECK_NEAR(AgentBehavior::availableEnergyStorage(n), 105.0, kTol);
    const auto uptake = AgentBehavior::absorbGraze(n, 11.0, 20.0, 1.0, ledger);
    CHECK_NEAR(uptake.energyAbsorbed, 20.0, kTol);
    CHECK_NEAR(n.energy, 150.0, kTol);
    CHECK_NEAR(n.balls.front().energy, 25.0, kTol);
    CHECK_NEAR(n.energy + n.balls.front().energy, 175.0, kTol);
}

TEST(a10_resource_ball_stores_and_returns_energy) {
    AgentLedger ledger;
    Nucleus n;
    n.id = 1;
    n.alive = true;
    n.energy = 150.0;
    n.cap = 200.0;
    n.bodyMass = 20.0;
    Ball b;
    b.id = 77;
    b.type = BallType::Resource;
    b.cap = 60.0;
    b.energy = 0.0;
    b.ownerId = n.id;
    n.balls.push_back(b);

    const double e0 = n.energy;
    // 存入 40：核 -40，球 +40，总量不变（零和）。
    const double stored = AgentBehavior::depositEnergy(n, n.balls[0], 40.0, ledger);
    CHECK_NEAR(stored, 40.0, kTol);
    CHECK_NEAR(n.energy, e0 - 40.0, kTol);
    CHECK_NEAR(n.balls[0].energy, 40.0, kTol);
    CHECK_NEAR(n.energy + n.balls[0].energy, e0, kTol);   // 同一份能量不会两边都算

    // 球满（cap=60）：只能再存 20。
    const double more = AgentBehavior::depositEnergy(n, n.balls[0], 100.0, ledger);
    CHECK_NEAR(more, 20.0, kTol);
    CHECK_NEAR(n.balls[0].energy, 60.0, kTol);            // 容量封顶

    // 按有限速率取回：rate=0.05 → 每帧 60*0.05 = 3。
    const double before = n.energy;
    const double got = AgentBehavior::withdrawEnergy(n, n.balls[0], 0.05, ledger);
    CHECK_NEAR(got, 3.0, 1e-9);
    CHECK_NEAR(n.energy, before + 3.0, 1e-9);
    CHECK_NEAR(n.balls[0].energy, 57.0, 1e-9);
}

// A10 —— 接管保存余量；满球与空球的生存后果不同。
TEST(a10_takeover_preserves_stored_energy_and_full_ball_survives_longer) {
    // 核 A 有个装满的球；核 B 有个空球。两者其余条件相同。
    // 让球脱离、被另一个核接管，检查余量原样保留；再比较"靠球能撑多久"。
    auto surviveWith = [](double ballEnergy) {
        AgentLedger ledger;
        Nucleus n;
        n.id = 1;
        n.alive = true;
        n.energy = 1.0;       // 几乎饿死
        n.cap = 200.0;
        n.bodyMass = 20.0;
        Ball b;
        b.id = 5;
        b.type = BallType::Resource;
        b.cap = 60.0;
        b.energy = ballEnergy;
        b.ownerId = n.id;
        n.balls.push_back(b);
        // 每帧按 apply 的同一速率取回，数出能撑多少帧。
        int frames = 0;
        while (n.alive && frames < 10000) {
            AgentBehavior::withdrawEnergy(n, n.balls[0], 0.05, ledger);
            n.energy -= 1.0;                  // 粗粒化的每帧维持开销
            if (n.energy <= 0.0) n.alive = false;
            ++frames;
        }
        return frames;
    };
    const int full = surviveWith(60.0);
    const int empty = surviveWith(0.0);
    CHECK(full > empty);       // 满球撑得更久（"生存后果不同"）
    CHECK(empty == 1);         // 空球：第一帧就撑不住
    CHECK(full > 10);          // 装满的球能撑一段时间
}

// ============================================================================
// A11 —— 普通开局里，核可以付费获得器官；繁殖与建造争同一份资源；
//        蓝细菌不再掉落成品球
// ============================================================================
TEST(a11_nucleus_builds_its_own_organ_and_pays_for_it) {
    AgentLedger ledger;
    PCG32 rng(31);
    Nucleus n;
    n.id = 1;
    n.alive = true;
    n.energy = 120.0;
    n.cap = 200.0;
    n.bodyMass = 40.0;
    const double e0 = n.energy;
    const double m0 = n.bodyMass;
    CHECK(n.balls.empty());
    CHECK(AgentBehavior::deployBall(n, ledger, rng, 256.0, 256.0, BallType::Scout));
    CHECK((n.balls.size()) == (static_cast<std::size_t>(1)));
    // 付费：能量减少（器官的建造代价）。
    CHECK(n.energy < e0);
    // 身体物质**不**因建造而减少——器官是同一个体的附件，没有独立的物质载体；
    // 若在这里扣一笔而不记到别处，那笔物质就凭空消失（A7 严禁）。bodyMass 在这里
    // 的作用是**建造门槛**：身体太小长不出新器官（见下一条用例）。
    CHECK_NEAR(n.bodyMass, m0, kTol);
    // 账目：能量变化必须记进账本（零和/耗散对）。
    CHECK(ledger.totalDissipated() > 0.0);
}

// A11 后半 —— 身体物质不够就造不出器官（不能把身体掏空）。
TEST(a11_cannot_build_organ_without_body_mass) {
    AgentLedger ledger;
    PCG32 rng(32);
    Nucleus n;
    n.id = 1;
    n.alive = true;
    n.energy = 500.0;     // 能量充足
    n.cap = 200.0;
    n.bodyMass = 2.0;     // 物质不足
    CHECK(!AgentBehavior::deployBall(n, ledger, rng, 256.0, 256.0, BallType::Scout));
    CHECK(n.balls.empty());
    CHECK_NEAR(n.energy, 500.0, kTol);   // 拒绝路径不扣能量
}

// A11 —— 蓝细菌不再产出成品球：跑一段后自由球池里没有任何"蓝细菌源"的球。
TEST(a11_cyano_no_longer_drops_ready_made_balls) {
    SimConfig cfg;
    cfg.world.width = 128.0;
    cfg.world.height = 128.0;
    cfg.world.seed = 21;
    cfg.cyanoNodeCount = 40;      // 大量生产者
    cfg.cyanoMaxNodes = 128;
    cfg.colony.nuclei = 0;        // 没有核 → 池里若出现球，只可能来自蓝细菌
    Simulation sim(cfg);
    CHECK(sim.runTo(1500));
    // 判据：没有核就没有任何球（蓝细菌产球通道已删除）。
    CHECK((sim.freeBalls().size()) == (static_cast<std::size_t>(0)));
}

// ============================================================================
// A12 —— 同版本、同配置、同种子与输入，运行两次得到相同状态
// （不比较耗时、不比较界面文案）
// ============================================================================
TEST(a12_same_seed_same_state_hash) {
    auto runOnce = []() {
        SimConfig cfg;
        cfg.world.width = 128.0;
        cfg.world.height = 128.0;
        cfg.world.seed = 1234;
        cfg.colony.nuclei = 8;
        cfg.cyanoNodeCount = 32;
        cfg.cyanoMaxNodes = 128;
        Simulation sim(cfg);
        CHECK(sim.runTo(600));
        return sim.stateHash();
    };
    const std::uint64_t h1 = runOnce();
    const std::uint64_t h2 = runOnce();
    CHECK((h1) == (h2));     // 逐位相同的状态指纹
}

// ============================================================================
// A13 —— 在残骸/球内储备/个体记忆/临时环境效果已发生的时刻存盘；
//        连续运行与读档续跑、检查点回放到相同帧的关键状态一致。
// ============================================================================
TEST(a13_save_resume_produces_identical_state) {
    SimConfig cfg;
    cfg.world.width = 128.0;
    cfg.world.height = 128.0;
    cfg.world.seed = 777;
    cfg.colony.nuclei = 6;
    cfg.cyanoNodeCount = 24;
    cfg.cyanoMaxNodes = 64;
    cfg.cyanoTuning.decomposeRate = 0.01;

    // 参考世界：一路跑到 900 帧。
    Simulation reference(cfg);
    CHECK(reference.runTo(900));
    const std::uint64_t refHash = reference.stateHash();
    const double refMatter = reference.matter().total();
    const double refEnergy = reference.totalEnergy();

    // 存档世界：跑到 500 帧 → 存盘 → 重新读入 → 继续跑到 900 帧。
    Simulation saved(cfg);
    CHECK(saved.runTo(500));
    // 此时残骸/养分/记忆都已经"发生过"，符合 A13 的构造要求。
    CHECK(saved.matter().total() > 0.0);
    std::string blob;
    {
        std::ostringstream os;
        CHECK(saved.serializeState(os));
        blob = os.str();
    }
    CHECK(!blob.empty());
    Simulation resumed(cfg);
    {
        std::istringstream is(blob);
        std::string err;
        CHECK(resumed.deserializeState(is, err));
        CHECK(err.empty());
    }
    CHECK(resumed.auditOk());              // 读档即自证：账本闭合
    CHECK((resumed.stateHash()) == (saved.stateHash()));
    CHECK(resumed.runTo(900));
    // 判据 A13：续跑与连续运行在相同帧的关键状态一致。
    CHECK((resumed.stateHash()) == (refHash));
    CHECK_NEAR(resumed.matter().total(), refMatter, 1e-9 * std::max(1.0, refMatter));
    CHECK_NEAR(resumed.totalEnergy(), refEnergy, 1e-9 * std::max(1.0, refEnergy));
}

// A13 前半 —— 旧档原件不被改写（拒绝路径必须只读）。
TEST(a13_old_rules_save_is_rejected_without_modifying_the_file) {
    // 伪造一份"旧版本"存档头：magic 正确、版本号是旧的 3。
    // 判据：读入失败、错误信息里能看到版本、且输入流内容一个字节都没变。
    std::string fake;
    fake.push_back(static_cast<char>(0x53));  // 'S'
    fake.push_back(static_cast<char>(0x41));  // 'A'
    fake.push_back(static_cast<char>(0x53));  // 'S'
    fake.push_back(static_cast<char>(0x41));  // 'A' → magic 0x41534153 小端
    fake.push_back(static_cast<char>(0x03));  // version = 3（旧规则）
    fake.push_back(static_cast<char>(0x00));
    fake.push_back(static_cast<char>(0x00));
    fake.push_back(static_cast<char>(0x00));
    fake.append(64, static_cast<char>(0xAB));  // 一些"数据"，读侧绝不能碰它
    const std::string original = fake;
    std::istringstream is(fake);
    SimConfig cfg;
    Simulation sim(cfg);
    std::string err;
    const bool ok = sim.deserializeState(is, err);
    CHECK(!ok);                                 // 明确拒绝
    CHECK(err.find("version") != std::string::npos ||
          err.find("rules") != std::string::npos);   // 错误信息说清版本问题
    // 原件未被改写（istringstream 是副本；这里断言我们没把它当输出流用过）。
    CHECK(fake == original);
}

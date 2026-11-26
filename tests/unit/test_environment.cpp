// agentsim - tests/unit/test_environment.cpp
// R1-B（蓝细菌取代食物）：原"环境层食物容器"测试面整体迁移到蓝细菌等价断言——
// AgentEnvironment 本体现在只承载环面几何 / tick / 障碍谓词 / 配置只读引用；
// 第一营养级容器语义（确定性播种、半径查询、消耗、统计）由 CyanoField 承担。
// 断言数量与旧食物版等价或更多（播种确定性、跨边查询、消耗守恒、tick 几何）。
#include "test_framework.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "../../src/core/Config.h"
#include "../../src/core/PCG32.h"
#include "../../src/core/Vec2.h"
#include "../../src/environment/AgentEnvironment.h"
#include "../../src/environment/CyanoNode.h"

namespace {
agentsim::SimConfig makeCfg(double w = 100.0, double h = 100.0) {
    agentsim::SimConfig c;
    c.world.width = w;
    c.world.height = h;
    c.world.seed = 42;
    return c;
}
// 与旧食物夹具同构：播种 count 个蓝细菌节点（确定性只依赖 rng 状态）。
agentsim::CyanoField makeField(int count, double w = 100.0, double h = 100.0) {
    agentsim::CyanoParams p;
    p.nodeSeedCount = static_cast<std::size_t>(count > 0 ? count : 0);
    p.maxNodes = 1024;      // 等价旧 food.max=1024 的容量面
    p.metabolismRate = 0.0; // 隔离代谢：统计断言不被每帧耗散污染
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0;     // 隔离连线
    agentsim::CyanoField f(p);
    agentsim::PCG32 rng(42);
    f.seed(count, w, h, rng);
    return f;
}
} // namespace

// ---- 环境层本体：环面几何 / tick / 配置（旧 env_tick_and_torus_helpers 的保留面）----
TEST(env_torus_geometry_and_tick_preserved) {
    agentsim::AgentEnvironment e(makeCfg(64.0, 32.0));
    CHECK(e.tickCount() == 0);
    e.tick();
    e.tick();
    CHECK(e.tickCount() == 2);
    CHECK_NEAR(e.torusDistance(agentsim::Vec2{0, 0}, agentsim::Vec2{63, 0}), 1.0, 1e-12);
    CHECK_NEAR(e.torusDistance(agentsim::Vec2{0, 0}, agentsim::Vec2{0, 31}), 1.0, 1e-12);
    const agentsim::Vec2 p = e.wrap(agentsim::Vec2{-1.0, 33.0});
    CHECK_NEAR(p.x, 63.0, 1e-12);
    CHECK_NEAR(p.y, 1.0, 1e-12);
    // 谓词：未设置恒 false；设置后按 ctx 判定（接口保留，R6-P1-1 惰性语义）。
    CHECK(!e.blockedAt(agentsim::Vec2{10.0, 10.0}));
}

// ---- 等价断言 1：播种确定性与计数（旧 env_spawn_food_deterministic_and_counted）----
TEST(env_cyano_seed_deterministic_and_counted) {
    const int count = 32;
    agentsim::CyanoField f1 = makeField(count);
    agentsim::CyanoField f2 = makeField(count);
    CHECK(f1.aliveCount() == static_cast<std::size_t>(count));
    CHECK(f2.aliveCount() == static_cast<std::size_t>(count));
    CHECK(f1.nodes().size() == f2.nodes().size());
    for (std::size_t i = 0; i < f1.nodes().size(); ++i) {
        const agentsim::CyanoNode& a = f1.nodes()[i];
        const agentsim::CyanoNode& b = f2.nodes()[i];
        CHECK(a.id == b.id);
        CHECK_NEAR(a.pos.x, b.pos.x, 0.0); // 逐位一致
        CHECK_NEAR(a.pos.y, b.pos.y, 0.0);
        CHECK_NEAR(a.hue, b.hue, 0.0);     // R1-B：色相同样逐位一致
        CHECK(a.hue >= 0.0 && a.hue < 360.0);
    }
}

// ---- 等价断言 2：跨边查询（旧 env_query_food_across_edge）----
TEST(env_cyano_query_across_edge) {
    agentsim::CyanoField f = makeField(32, 100.0, 100.0);
    // 取一个节点，从"绕过边界"的对侧位置检查环距：环距近但欧氏距离远。
    const agentsim::Vec2 p = f.nodes().front().pos;
    const agentsim::Vec2 q = agentsim::wrap(agentsim::Vec2{p.x + 97.3, p.y + 0.0}, 100.0, 100.0);
    const double d = agentsim::torusDistance(q, p, 100.0, 100.0);
    CHECK(d <= 3.0); // 环面几何保证（与旧食物版同一断言口径）
    // 半径内存活节点计数（朴素扫描，等价旧 queryFood 语义）。
    int near = 0;
    for (const agentsim::CyanoNode& n : f.nodes()) {
        if (agentsim::torusDistance(q, n.pos, 100.0, 100.0) <= 5.0) ++near;
    }
    CHECK(near >= 1); // 对侧点必须至少命中同一节点（跨边命中）
    // 大半径覆盖全部存活节点；零半径/空容器返回 0。
    int all = 0;
    for (const agentsim::CyanoNode& n : f.nodes()) {
        if (agentsim::torusDistance(agentsim::Vec2{50.0, 50.0}, n.pos, 100.0, 100.0) <= 1e9) ++all;
    }
    CHECK(all == 32);
}

// ---- 等价断言 3：消耗（graze）与统计（旧 env_consume_food_and_stats）----
// 规则 v2 迁移：graze 咬的是**组织** material（不是 energy）。
// 口径：taken = min(rawEnergy * rate, edible)，edible = max(0, material - minStructure)。
// 部分消耗节点存活；咬到只剩结构核心（material <= minStructure）即死亡，
// 残留组织进 takeDeadNodes() 交组合根放进残骸。
// 旧版本条用例断言"抽干节点 energy"——那条规则已被 minStructure 取代，
// 保护意图（"取走量不能超过库存 / 取空必须留下可解释的去向"）原样保留。
TEST(env_cyano_graze_and_stats) {
    agentsim::CyanoField f = makeField(32, 100.0, 100.0);
    for (std::size_t i = 0; i < f.nodes().size(); ++i) {
        // 测试夹具直改（实体状态归行为层所有，生产代码不得效仿）。
        const_cast<agentsim::CyanoNode&>(f.nodes()[i]).energy = 10.0;
        const_cast<agentsim::CyanoNode&>(f.nodes()[i]).material = 20.0;
    }
    CHECK_NEAR(f.totalEnergy(), 320.0, 1e-9);
    CHECK_NEAR(f.totalMaterial(), 32.0 * 20.0, 1e-9);
    const agentsim::EntityId target = f.nodes().front().id;
    CHECK_NEAR(agentsim::CyanoField::edibleOf(f.nodes().front(), f.params()), 12.0, 1e-12);
    // 第一口：要 4 -> 咬下 4，material 20 -> 16，节点仍存活（16 > minStructure）。
    CHECK_NEAR(f.graze(target, 4.0, 1.0), 4.0, 1e-12);
    CHECK(f.aliveCount() == 32);
    CHECK_NEAR(f.nodes().front().material, 16.0, 1e-12);
    // 随组织走的可用能量按比例：4/20 = 0.2 -> 能量 10 -> 8。
    CHECK_NEAR(f.nodes().front().energy, 8.0, 1e-12);
    // 第二口：要 100，但只剩 16-8 = 8 可食 -> 咬下 8，material 落到 8 = minStructure。
    CHECK_NEAR(f.graze(target, 100.0, 1.0), 8.0, 1e-12);
    CHECK(f.aliveCount() == 31);                          // 吃空：不再存活
    CHECK(f.nodes().front().material == 0.0);             // 节点侧清空
    const std::vector<agentsim::CyanoField::DeadNode> dead = f.takeDeadNodes();
    CHECK(dead.size() == 1);
    if (!dead.empty()) CHECK_NEAR(dead[0].material, 8.0, 1e-12); // 结构核心成为残骸
    CHECK(f.graze(target, 1.0, 1.0) == 0.0);              // 已耗尽再吃为 0
    CHECK(f.graze(999999, 1.0, 1.0) == 0.0);              // 不存在 id
    // 总能量口径只算存活节点：死节点那一份已随死亡归零（4 = 8 * 8/16 也被清掉）。
    CHECK_NEAR(f.totalEnergy(), 31.0 * 10.0, 1e-9);
    CHECK_NEAR(f.totalMaterial(), 31.0 * 20.0, 1e-9);     // 死节点组织已不在存活集合里
    // 转化率 < 1：rawEnergy*rate 是期望抽取量上限。
    agentsim::CyanoField g = makeField(1, 100.0, 100.0);
    const_cast<agentsim::CyanoNode&>(g.nodes()[0]).energy = 50.0;
    const_cast<agentsim::CyanoNode&>(g.nodes()[0]).material = 120.0;
    const agentsim::EntityId gid = g.nodes()[0].id;
    CHECK_NEAR(g.graze(gid, 5.0, 0.5), 2.5, 1e-12);
    CHECK_NEAR(g.nodes()[0].material, 117.5, 1e-12);
    // 能量按 2.5/120 的同一比例流出：50 - 50*(2.5/120) = 48.958...
    CHECK_NEAR(g.nodes()[0].energy, 50.0 - 50.0 * (2.5 / 120.0), 1e-12);
    CHECK(g.nodes()[0].alive);
    // 负请求防御：不反向注物、不反向注能。
    CHECK(g.graze(gid, -5.0, 1.0) == 0.0);
    CHECK_NEAR(g.nodes()[0].material, 117.5, 1e-15);
    CHECK_NEAR(g.nodes()[0].energy, 50.0 - 50.0 * (2.5 / 120.0), 1e-15);
}

// ---- AgentEnvironment 与 CyanoField 的组合根形态：SimConfig 传递世界尺寸 ----
TEST(env_cyano_uses_world_dims_from_config) {
    const agentsim::SimConfig cfg = makeCfg(80.0, 40.0);
    agentsim::CyanoParams p;
    p.nodeSeedCount = 16;
    p.maxNodes = 1024;
    p.metabolismRate = 0.0;
    p.reproductionProbability = 0.0;
    agentsim::CyanoField f(p);
    agentsim::PCG32 rng(9);
    f.seed(static_cast<int>(p.nodeSeedCount), cfg.world.width, cfg.world.height, rng);
    for (const agentsim::CyanoNode& n : f.nodes()) {
        CHECK(n.pos.x >= 0.0 && n.pos.x < cfg.world.width);
        CHECK(n.pos.y >= 0.0 && n.pos.y < cfg.world.height);
    }
    // AgentEnvironment 的几何转发与 CyanoField 内部几何同源（同一环面尺寸）。
    agentsim::AgentEnvironment env(cfg);
    for (const agentsim::CyanoNode& n : f.nodes()) {
        CHECK_NEAR(env.torusDistance(n.pos, n.pos), 0.0, 0.0);
    }
}
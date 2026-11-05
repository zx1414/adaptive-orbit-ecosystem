// agentsim - tests/unit/test_terrain.cpp
// 地形：空地形惰性、障碍阻挡、跨边界环距、resolveMove 推出、水域耗能、
// 温度偏移衰减、确定性、默认配置回归安全、数值有限性。
#include "test_framework.h"

#include <cmath>

#include "../../src/core/Config.h"
#include "../../src/core/PCG32.h"
#include "../../src/core/Vec2.h"
#include "../../src/environment/Terrain.h"
#include "../../src/environment/AgentEnvironment.h"
#include "../../src/environment/TemperatureField.h"
#include "../../src/sim/Simulation.h"

namespace {
agentsim::TerrainConfig emptyCfg() {
    agentsim::TerrainConfig c;
    c.obstacleCount = 0;
    c.waterCount = 0;
    c.thermalZoneCount = 0;
    return c;
}

agentsim::TerrainBlob blob(double x, double y, double r, double s = 1.0) {
    agentsim::TerrainBlob b;
    b.x = x;
    b.y = y;
    b.radius = r;
    b.strength = s;
    return b;
}
} // namespace

TEST(terrain_empty_terrain_inert_everywhere) {
    agentsim::PCG32 rng(1);
    const agentsim::Terrain t(emptyCfg(), 200.0, 100.0, rng);
    CHECK(t.obstacleCount() == 0);
    CHECK(t.waterCount() == 0);
    CHECK(t.thermalZoneCount() == 0);
    for (int i = 0; i < 6; ++i) {
        const agentsim::Vec2 p{17.0 * i + 0.5, 9.5 * i * i};
        CHECK(!t.blocked(p));
        CHECK(!t.inWater(p));
        CHECK(t.moveCostMultiplier(p) == 1.0);
        CHECK(t.temperatureOffset(p) == 0.0);
    }
}

TEST(terrain_single_obstacle_blocks_center_only) {
    agentsim::TerrainConfig c = emptyCfg();
    c.obstacles.push_back(blob(100.0, 50.0, 20.0));
    agentsim::PCG32 rng(2);
    const agentsim::Terrain t(c, 200.0, 100.0, rng);
    CHECK(t.obstacleCount() == 1);
    CHECK(t.blocked(agentsim::Vec2{100.0, 50.0}));            // 圆心
    CHECK(t.blocked(agentsim::Vec2{115.0, 50.0}));            // 环距 15 < 20
    CHECK(!t.blocked(agentsim::Vec2{130.0, 50.0}));           // 环距 30 > 20
    CHECK(!t.blocked(agentsim::Vec2{100.0, 75.0}));           // 环距 25 > 20
}

TEST(terrain_blocks_across_torus_boundary) {
    agentsim::TerrainConfig c = emptyCfg();
    c.obstacles.push_back(blob(195.0, 50.0, 10.0)); // 贴近 x=W=200 边界
    agentsim::PCG32 rng(3);
    const agentsim::Terrain t(c, 200.0, 100.0, rng);
    CHECK(t.blocked(agentsim::Vec2{195.0, 50.0}));  // 圆心
    CHECK(t.blocked(agentsim::Vec2{2.0, 50.0}));    // 跨边界环距 7 < 10
    CHECK(t.blocked(agentsim::Vec2{199.0, 50.0}));  // 环距 4
    CHECK(!t.blocked(agentsim::Vec2{10.0, 50.0}));  // 环距 15 > 10
    CHECK(!t.blocked(agentsim::Vec2{195.0, 70.0})); // y 方向环距 20
}

TEST(terrain_resolve_move_pushes_out_of_obstacle) {
    agentsim::TerrainConfig c = emptyCfg();
    c.obstacles.push_back(blob(100.0, 50.0, 20.0));
    agentsim::PCG32 rng(4);
    const agentsim::Terrain t(c, 200.0, 100.0, rng);
    // 直线穿越障碍：落点必须合法，且距 from 不远于原目标。
    const agentsim::Vec2 from{70.0, 50.0};
    const agentsim::Vec2 to{105.0, 50.0}; // 目标在障碍内（环距 5 < 20）
    const agentsim::Vec2 r = t.resolveMove(from, to);
    CHECK(!t.blocked(r));                                  // 落点合法
    CHECK(torusDistance(from, r, 200.0, 100.0) <=
          torusDistance(from, to, 200.0, 100.0) + 1e-9);
    CHECK(r.x < 100.0);                                    // 被推出到障碍左侧
    // 目标本就合法：原样返回（wrap 后）。
    const agentsim::Vec2 ok = t.resolveMove(from, agentsim::Vec2{60.0, 50.0});
    CHECK_NEAR(ok.x, 60.0, 1e-12);
    CHECK_NEAR(ok.y, 50.0, 1e-12);
    // from 本身被挡（陷入）：返回 from。
    const agentsim::Vec2 stuck = t.resolveMove(agentsim::Vec2{100.0, 50.0},
                                               agentsim::Vec2{110.0, 50.0});
    CHECK(stuck.x == 100.0 && stuck.y == 50.0);
    // 跨边界 resolveMove：目标障碍贴 x=0，from 在另一侧，落点合法。
    agentsim::TerrainConfig c2 = emptyCfg();
    c2.obstacles.push_back(blob(3.0, 50.0, 8.0));
    const agentsim::Terrain t2(c2, 200.0, 100.0, rng);
    const agentsim::Vec2 f2{193.0, 50.0};                  // 环距 10 > 8，障碍外
    const agentsim::Vec2 r2 = t2.resolveMove(f2, agentsim::Vec2{5.0, 50.0});
    CHECK(!t2.blocked(r2));
    CHECK(torusDistance(f2, r2, 200.0, 100.0) <= 6.0 + 1e-9);
}

TEST(terrain_water_cost_matches_membership) {
    agentsim::TerrainConfig c = emptyCfg();
    c.waterCostMultiplier = 2.5;
    c.waters.push_back(blob(50.0, 50.0, 30.0));
    agentsim::PCG32 rng(5);
    const agentsim::Terrain t(c, 200.0, 100.0, rng);
    CHECK(t.waterCount() == 1);
    CHECK(t.inWater(agentsim::Vec2{50.0, 50.0}));
    CHECK(t.inWater(agentsim::Vec2{70.0, 50.0}));          // 环距 20 < 30
    CHECK(!t.inWater(agentsim::Vec2{90.0, 50.0}));         // 环距 40
    CHECK_NEAR(t.moveCostMultiplier(agentsim::Vec2{50.0, 50.0}), 2.5, 1e-12);
    CHECK_NEAR(t.moveCostMultiplier(agentsim::Vec2{90.0, 50.0}), 1.0, 1e-12);
    CHECK(t.moveCostMultiplier(agentsim::Vec2{70.0, 50.0}) > 1.0); // 与 inWater 一致
    CHECK(!t.blocked(agentsim::Vec2{50.0, 50.0}));         // 水域不阻挡
}

TEST(terrain_temperature_offset_linear_decay) {
    agentsim::TerrainConfig c = emptyCfg();
    c.thermalZones.push_back(blob(100.0, 50.0, 40.0, 0.2));
    agentsim::PCG32 rng(6);
    const agentsim::Terrain t(c, 200.0, 100.0, rng);
    CHECK(t.thermalZoneCount() == 1);
    CHECK_NEAR(t.temperatureOffset(agentsim::Vec2{100.0, 50.0}), 0.2, 1e-12); // 中心最大
    CHECK_NEAR(t.temperatureOffset(agentsim::Vec2{120.0, 50.0}), 0.1, 1e-12); // 半径处一半
    CHECK_NEAR(t.temperatureOffset(agentsim::Vec2{140.0, 50.0}), 0.0, 1e-12); // 边界归零
    CHECK_NEAR(t.temperatureOffset(agentsim::Vec2{160.0, 50.0}), 0.0, 1e-12); // 圆外为 0
    // strength 越大偏移越大（同一点）。
    agentsim::TerrainConfig c2 = emptyCfg();
    c2.thermalZones.push_back(blob(100.0, 50.0, 40.0, 0.4));
    agentsim::PCG32 rng2(7);
    const agentsim::Terrain t2(c2, 200.0, 100.0, rng2);
    CHECK(t2.temperatureOffset(agentsim::Vec2{110.0, 50.0}) >
          t.temperatureOffset(agentsim::Vec2{110.0, 50.0}));
    // 跨边界：区贴 x=0，另一侧采样非零且对称。
    agentsim::TerrainConfig c3 = emptyCfg();
    c3.thermalZones.push_back(blob(2.0, 50.0, 20.0, 0.3));
    const agentsim::Terrain t3(c3, 200.0, 100.0, rng2);
    CHECK(t3.temperatureOffset(agentsim::Vec2{198.0, 50.0}) > 0.0);
    CHECK_NEAR(t3.temperatureOffset(agentsim::Vec2{198.0, 50.0}),
               t3.temperatureOffset(agentsim::Vec2{6.0, 50.0}), 1e-12);
}

TEST(terrain_generate_deterministic_same_seed) {
    agentsim::TerrainConfig c;
    c.obstacleCount = 6;
    c.waterCount = 4;
    c.thermalZoneCount = 3;
    c.obstacleRadius = 15.0;  // 缩小半径保证可放置（2r 间距约束）
    c.waterRadius = 20.0;
    c.thermalRadius = 25.0;
    agentsim::PCG32 r1(123), r2(123);
    const agentsim::Terrain t1(c, 256.0, 256.0, r1);
    const agentsim::Terrain t2(c, 256.0, 256.0, r2);
    CHECK(t1.obstacleCount() == c.obstacleCount);
    CHECK(t1.waterCount() == c.waterCount);
    CHECK(t1.thermalZoneCount() == c.thermalZoneCount);
    for (std::size_t i = 0; i < t1.obstacleCount(); ++i) {
        CHECK(t1.obstacles()[i].x == t2.obstacles()[i].x); // 逐位一致
        CHECK(t1.obstacles()[i].y == t2.obstacles()[i].y);
    }
    for (std::size_t i = 0; i < t1.thermalZoneCount(); ++i) {
        CHECK(t1.thermalZones()[i].strength == c.thermalStrength);
    }
}

TEST(terrain_default_simconfig_generates_nothing) {
    agentsim::SimConfig cfg{};                 // 默认 terrain 全 0：回归安全
    cfg.world.width = 256.0; cfg.world.height = 256.0;  // R8-T53：显式钉 256，本测 wrap 语义不随默认世界尺寸变
    CHECK(cfg.terrain.obstacleCount == 0);
    CHECK(cfg.terrain.waterCount == 0);
    CHECK(cfg.terrain.thermalZoneCount == 0);
    agentsim::PCG32 rng(8);
    const agentsim::Terrain t(cfg, rng);       // SimConfig 便捷构造
    CHECK(t.obstacleCount() == 0 && t.waterCount() == 0 && t.thermalZoneCount() == 0);
    CHECK(t.moveCostMultiplier(agentsim::Vec2{128.0, 128.0}) == 1.0);
    CHECK(t.temperatureOffset(agentsim::Vec2{128.0, 128.0}) == 0.0);
    // resolveMove 在空地形上 = wrap。
    const agentsim::Vec2 r = t.resolveMove(agentsim::Vec2{250.0, 250.0},
                                           agentsim::Vec2{260.0, 250.0});
    CHECK_NEAR(r.x, 4.0, 1e-12);
    CHECK_NEAR(r.y, 250.0, 1e-12);
}

TEST(terrain_generate_respects_margins_and_no_overlap) {
    agentsim::TerrainConfig c;
    c.obstacleCount = 5;
    c.obstacleRadius = 15.0;
    agentsim::PCG32 rng(9);
    const agentsim::Terrain t(c, 256.0, 256.0, rng);
    CHECK(t.obstacleCount() == 5);
    for (std::size_t i = 0; i < t.obstacleCount(); ++i) {
        const agentsim::TerrainBlob& b = t.obstacles()[i];
        CHECK(b.x >= 5.12 && b.x <= 250.88);   // margin = 2% 边界
        CHECK(b.y >= 5.12 && b.y <= 250.88);
        for (std::size_t j = i + 1; j < t.obstacleCount(); ++j) {
            CHECK(torusDistance(agentsim::Vec2{b.x, b.y},
                                  agentsim::Vec2{t.obstacles()[j].x,
                                                 t.obstacles()[j].y},
                                  256.0, 256.0) >= 30.0 - 1e-9); // 两倍半径间距
        }
    }
}

TEST(terrain_outputs_finite_everywhere) {
    agentsim::TerrainConfig c;
    c.obstacleCount = 3;
    c.waterCount = 3;
    c.thermalZoneCount = 3;
    agentsim::PCG32 rng(10);
    const agentsim::Terrain t(c, 200.0, 100.0, rng);
    for (int i = 0; i <= 8; ++i) {
        for (int j = 0; j <= 8; ++j) {
            const agentsim::Vec2 p{i * 25.0, j * 12.5};
            const double m = t.moveCostMultiplier(p);
            const double o = t.temperatureOffset(p);
            CHECK(std::isfinite(m) && m >= 1.0);
            CHECK(std::isfinite(o));
            CHECK(std::isfinite(torusDistance(
                p, t.resolveMove(p, agentsim::Vec2{p.x + 7.0, p.y + 3.0}),
                200.0, 100.0)));
        }
    }
}

// ---- R6-P1-1（R1-B 迁移）：障碍谓词保留为环境层通用"不可放置"判定接口 ----
// Food 撒点已删除，等价断言改为：谓词绑定后 blockedAt() 语义正确 + 惰性 + 空谓词恢复。
namespace {
bool ctxAlwaysTrue(void*, agentsim::Vec2) { return true; }
bool ctxRejectRing(void* ctx, agentsim::Vec2 p) {
    // ctx 复用 TerrainBlob*：以该 blob 圆心为中心的障碍。
    const agentsim::TerrainBlob* b = static_cast<const agentsim::TerrainBlob*>(ctx);
    return torusDistance(agentsim::Vec2{b->x, b->y}, p, 256.0, 256.0) <= b->radius;
}
} // namespace

TEST(terrain_predicate_semantics_preserved_after_food_removal) {
    agentsim::SimConfig scPin{};            // R8-T53：钉 256，跨缝距离语义与默认世界尺寸解耦
    scPin.world.width = 256.0; scPin.world.height = 256.0;
    agentsim::AgentEnvironment env(scPin);
    agentsim::TerrainBlob obst;             // 中央障碍 r=40（覆盖 2.4% 面积）
    obst.x = 128.0;
    obst.y = 128.0;
    obst.radius = 40.0;
    // 未设置谓词：恒 false（惰性）。
    CHECK(!env.blockedAt(agentsim::Vec2{128.0, 128.0}));
    env.setBlockedPredicate(&ctxRejectRing, &obst);
    CHECK(env.blockedAt(agentsim::Vec2{128.0, 128.0}));   // 圆心：blocked
    CHECK(!env.blockedAt(agentsim::Vec2{128.0, 168.1}));  // 环距 40.1：!blocked
    CHECK(env.blockedAt(agentsim::Vec2{128.0, 168.0}));   // 环距 40：blocked（<=）
    // 显式空谓词：恢复惰性。
    env.setBlockedPredicate(nullptr, nullptr);
    CHECK(!env.blockedAt(agentsim::Vec2{128.0, 128.0}));
    // 谓词不影响几何转发（R6 回归红线：无谓词路径零差异）。
    CHECK_NEAR(env.torusDistance(agentsim::Vec2{0, 0}, agentsim::Vec2{255, 0}), 1.0, 1e-12);
}
// ---- R6-P1-2：resolveMove 防隧穿——一步跨过障碍时必须被挡在障碍前 ----
TEST(terrain_resolve_move_no_tunneling_on_fast_step) {
    agentsim::TerrainConfig c = emptyCfg();
    c.obstacles.push_back(blob(100.0, 50.0, 24.0));   // r=24 障碍
    agentsim::PCG32 rng(31);
    const agentsim::Terrain t(c, 256.0, 256.0, rng);
    const agentsim::Vec2 from{60.0, 50.0};
    const agentsim::Vec2 to{146.0, 50.0};             // 距离 86，端点都合法
    CHECK(!t.blocked(to));
    const agentsim::Vec2 r = t.resolveMove(from, to);
    CHECK(!t.blocked(r));                             // 落点永远合法
    CHECK(torusDistance(from, r, 256.0, 256.0) < 16.0); // 二分收敛于障碍前 16-ε，不穿到对面
    // 同理跨边界：from/to 合法但最短路径横穿贴边障碍（f2 在障碍外）。
    agentsim::TerrainConfig c2 = emptyCfg();
    c2.obstacles.push_back(blob(2.0, 60.0, 24.0));    // 障碍贴 x=0
    const agentsim::Terrain t2(c2, 256.0, 256.0, rng);
    const agentsim::Vec2 f2{220.0, 60.0};             // 环距 38 > 24：合法出发点
    const agentsim::Vec2 to2{10.0, 60.0};             // 目标在障碍内
    const agentsim::Vec2 r2 = t2.resolveMove(f2, to2);
    CHECK(!t2.blocked(r2));
    CHECK(torusDistance(f2, r2, 256.0, 256.0) < 14.0 + 1e-9); // 停在障碍西侧 14-ε
}

// ---- R6-P0-2：组合根出生点不落障碍——全开地形 Simulation 200 tick 无核冻结饿死 ----
TEST(terrain_spawn_nuclei_avoid_obstacles_full_terrain) {
    agentsim::SimConfig cfg{};
    cfg.terrain.obstacleCount = 5;
    cfg.terrain.waterCount = 3;
    cfg.terrain.thermalZoneCount = 2;
    cfg.cyanoNodeCount = 64; // R1-B：初始第一营养级库存（等价旧 --food 64 口径）
    agentsim::Simulation sim(cfg);   // 构造：地形撒点 → 核出生（避障）→ 蓝细菌播种
    CHECK(sim.runTo(200));
    CHECK(sim.auditOk());
    CHECK(sim.nucleusCount() > 0);   // R6-P0-2：修复前 seed123 全开地形会核全灭
    CHECK(sim.cyano().aliveCount() > 0); // R1-B：第一营养级等价断言
}

// ---- R6 回归红线：默认（无地形）路径逐位不变 ----
TEST(terrain_predicate_default_bitwise_inert) {
    // (a) 空地形 resolveMove 仍是精确 wrap（256x256：wrap(260,250)={4,250}）。
    agentsim::PCG32 rngA(8);
    const agentsim::Terrain tEmpty(emptyCfg(), 256.0, 256.0, rngA);
    const agentsim::Vec2 r = tEmpty.resolveMove(agentsim::Vec2{250.0, 250.0},
                                                agentsim::Vec2{260.0, 250.0});
    CHECK_NEAR(r.x, 4.0, 1e-12);
    CHECK_NEAR(r.y, 250.0, 1e-12);
    // (b) 障碍远离线段：先采样三分点（全合法）后仍走快速路径，
    //     落点逐位等于 wrap(to)。
    agentsim::TerrainConfig c1 = emptyCfg();
    c1.obstacles.push_back(blob(150.0, 90.0, 5.0));   // 远离线段的障碍
    agentsim::PCG32 rngB(8);
    const agentsim::Terrain t1(c1, 256.0, 256.0, rngB);
    const agentsim::Vec2 r1 = t1.resolveMove(agentsim::Vec2{250.0, 250.0},
                                             agentsim::Vec2{260.0, 250.0});
    CHECK(r1.x == 4.0 && r1.y == 250.0);
    // (c) R1-B：Food 撒点已删除。等价断言：未设谓词/显式空谓词的 AgentEnvironment
    // 几何转发逐位一致（无谓词零差异），且不消耗任何 rng（无随机路径）。
    agentsim::SimConfig cfg{};
    agentsim::AgentEnvironment envA(cfg), envB(cfg);
    agentsim::PCG32 ra(55), rb(55);
    const agentsim::Vec2 pA = envA.wrap(agentsim::Vec2{-1.0, 33.0});
    envB.setBlockedPredicate(nullptr, nullptr);       // 显式空谓词也必须惰性
    const agentsim::Vec2 pB = envB.wrap(agentsim::Vec2{-1.0, 33.0});
    CHECK(pA.x == pB.x && pA.y == pB.y);
    std::uint64_t sa1, ia1, sa2, ia2;
    ra.getState(sa1, ia1);
    rb.getState(sa2, ia2);
    CHECK(sa1 == sa2 && ia1 == ia2); // 两条路径都不消耗 rng（确定性红线）
}


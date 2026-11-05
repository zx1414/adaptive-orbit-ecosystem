// agentsim - tests/unit/test_torus.cpp
// S1：Torus wrap / 最小环距用例（跨边、对角、半宽半高上界）。
#include "test_framework.h"

#include <cmath>

#include "../../src/core/Vec2.h"

namespace {
using agentsim::Vec2;
constexpr double W = 100.0;
constexpr double H = 50.0;
} // namespace

TEST(torus_wrap_basics) {
    CHECK_NEAR(agentsim::wrap(Vec2{0.0, 0.0}, W, H).x, 0.0, 1e-12);
    CHECK_NEAR(agentsim::wrap(Vec2{100.0, 0.0}, W, H).x, 0.0, 1e-12);
    CHECK_NEAR(agentsim::wrap(Vec2{-1.0, 0.0}, W, H).x, 99.0, 1e-12);
    CHECK_NEAR(agentsim::wrap(Vec2{250.0, -60.0}, W, H).y, 40.0, 1e-12);
}

TEST(torus_delta_cross_edges) {
    // (0,0)->(99,0)：右邻居走反方向缠绕，最小位移为 -1（环距长度 = 1）。
    Vec2 d = agentsim::torusDelta(Vec2{0, 0}, Vec2{W - 1, 0}, W, H);
    CHECK_NEAR(d.x, -1.0, 1e-12);
    CHECK_NEAR(d.y, 0.0, 1e-12);
    // 反向：(99,0)->(0,0) 最小位移为 +1，而不是 -99。
    d = agentsim::torusDelta(Vec2{W - 1, 0}, Vec2{0, 0}, W, H);
    CHECK_NEAR(d.x, 1.0, 1e-12);
    // 跨上边界：(0,0)->(0,48) 在高 50 上最短为 -2。
    d = agentsim::torusDelta(Vec2{0, 0}, Vec2{0, H - 2}, W, H);
    CHECK_NEAR(d.y, -2.0, 1e-12);
    d = agentsim::torusDelta(Vec2{0, H - 2}, Vec2{0, 0}, W, H);
    CHECK_NEAR(d.y, 2.0, 1e-12);
    // 环距长度恒等于 1/2，与符号无关（SPEC 用例 (0,0)->(W-1,0) 距离=1）。
    CHECK_NEAR(agentsim::torusDistance(Vec2{0, 0}, Vec2{W - 1, 0}, W, H), 1.0, 1e-12);
}

TEST(torus_delta_diagonal) {
    // 对角跨两轴：从 (1,1) 到 (98,48)，最小缠绕位移为 (-3,-3) 而非 (97,47)。
    Vec2 d = agentsim::torusDelta(Vec2{1, 1}, Vec2{W - 2, H - 2}, W, H);
    CHECK_NEAR(d.x, -3.0, 1e-12);
    CHECK_NEAR(d.y, -3.0, 1e-12);
    CHECK_NEAR(agentsim::torusDistance(Vec2{1, 1}, Vec2{W - 2, H - 2}, W, H),
               std::sqrt(18.0), 1e-12);
    // 反方向对角：(98,48)->(1,1) 为 (+3,+3)。
    d = agentsim::torusDelta(Vec2{W - 2, H - 2}, Vec2{1, 1}, W, H);
    CHECK_NEAR(d.x, 3.0, 1e-12);
    CHECK_NEAR(d.y, 3.0, 1e-12);
}

TEST(torus_distance_never_exceeds_half_extent) {
    // 环距分量永不超半宽/半高，标量距离永不超过 sqrt((W/2)^2+(H/2)^2)。
    const double maxD = std::sqrt(50.0 * 50.0 + 25.0 * 25.0);
    const Vec2 pts[] = {{0, 0},   {99, 0},  {0, 49},  {99, 49},
                        {37.5, 12}, {64, 40}, {1, 1},  {55, 25}};
    for (const Vec2& a : pts) {
        for (const Vec2& b : pts) {
            const double dist = agentsim::torusDistance(a, b, W, H);
            CHECK(dist <= maxD + 1e-9);
            CHECK(dist >= 0.0);
            const Vec2 d = agentsim::torusDelta(a, b, W, H);
            CHECK(d.x <= 50.0 + 1e-9 && d.x >= -50.0 - 1e-9);
            CHECK(d.y <= 25.0 + 1e-9 && d.y >= -25.0 - 1e-9);
        }
    }
    // 最远对角点恰为上界。
    CHECK_NEAR(agentsim::torusDistance(Vec2{0, 0}, Vec2{50, 25}, W, H), maxD, 1e-9);
}

TEST(vec2_helpers) {
    CHECK_NEAR(agentsim::dot(Vec2{3, 4}, Vec2{1, 1}), 7.0, 1e-12);
    CHECK_NEAR(agentsim::length(Vec2{3, 4}), 5.0, 1e-12);
    Vec2 n = agentsim::normalized(Vec2{3, 4});
    CHECK_NEAR(n.x, 0.6, 1e-12);
    CHECK_NEAR(n.y, 0.8, 1e-12);
    n = agentsim::normalized(Vec2{0, 0}); // 零向量安全
    CHECK_NEAR(n.x, 0.0, 1e-12);
    CHECK_NEAR(n.y, 0.0, 1e-12);
}

// agentsim - tests/unit/test_temperature.cpp
// 温度场：基线采样、高斯衰减单调性、跨边界连续性、确定性、范围与失真。
#include "test_framework.h"

#include <cmath>
#include <cstddef>

#include "../../src/core/Config.h"
#include "../../src/core/PCG32.h"
#include "../../src/core/Vec2.h"
#include "../../src/environment/TemperatureField.h"

namespace {
// 无源场：sample == baseline。
agentsim::TemperatureConfig baseCfg() {
    agentsim::TemperatureConfig c;
    c.sourceCount = 0;
    return c;
}
} // namespace

TEST(temp_empty_field_equals_baseline) {
    agentsim::PCG32 rng(1);
    agentsim::TemperatureField f(baseCfg(), rng); // sourceCount=0：不消耗 rng
    CHECK(f.sourceCount() == 0);
    for (int i = 0; i < 8; ++i) {
        const agentsim::Vec2 p{0.7 * i * 13.0, 1.3 * i * 7.0};
        CHECK_NEAR(f.sample(p), 0.5, 1e-12);
    }
    CHECK(f.sample(agentsim::Vec2{0.0, 0.0}) == 0.5);
}

TEST(temp_single_source_peak_at_center) {
    agentsim::TemperatureConfig c = baseCfg();
    c.sourceCount = 0;
    c.baseline = 0.5;
    agentsim::TemperatureSource s;
    s.x = 50.0;
    s.y = 50.0;
    s.amplitude = 0.4;
    s.radius = 30.0;
    c.sources.push_back(s);
    agentsim::PCG32 rng(2);
    agentsim::TemperatureField f(c, 100.0, 100.0, rng);
    CHECK(f.sourceCount() == 1);
    CHECK(f.sample(agentsim::Vec2{50.0, 50.0}) == 0.9); // 中心 = baseline + amplitude
    // 中心比周围任何点都热。
    double maxAround = -1.0;
    for (int i = 0; i < 16; ++i) {
        const double a = 6.283185307179586 * i / 16.0;
        const agentsim::Vec2 q{50.0 + 5.0 * std::cos(a), 50.0 + 5.0 * std::sin(a)};
        const double t = f.sample(q);
        if (t > maxAround) maxAround = t;
    }
    CHECK(f.sample(agentsim::Vec2{50.0, 50.0}) > maxAround);
    CHECK(maxAround < 0.9);
}

TEST(temp_decay_monotonic_with_distance) {
    agentsim::TemperatureConfig c = baseCfg();
    agentsim::TemperatureSource s;
    s.x = 0.0;
    s.y = 0.0;
    s.amplitude = 0.4;
    s.radius = 40.0;
    c.sources.push_back(s);
    agentsim::PCG32 rng(3);
    const agentsim::TemperatureField f(c, 200.0, 200.0, rng);
    double prev = f.sample(agentsim::Vec2{0.0, 0.0});
    CHECK(prev > 0.5);
    for (int d = 1; d <= 40; d += 4) { // 环面宽 200，d < 100 无绕回歧义
        const double t = f.sample(agentsim::Vec2{static_cast<double>(d), 0.0});
        CHECK(t < prev + 1e-15); // 距中心越远温度越回归 baseline（单调不增）
        prev = t;
    }
    CHECK(f.sample(agentsim::Vec2{60.0, 0.0}) > 0.5);  // 还没回到基线
    CHECK(f.sample(agentsim::Vec2{99.0, 0.0}) < f.sample(agentsim::Vec2{60.0, 0.0}));
}

TEST(temp_continuous_across_boundary) {
    agentsim::TemperatureConfig c = baseCfg();
    agentsim::TemperatureSource s;
    s.x = 1.0; // 源贴近 x=0 边界：x=W-eps 与 x=0+eps 处环距几乎相等
    s.y = 50.0;
    s.amplitude = 0.4;
    s.radius = 20.0;
    c.sources.push_back(s);
    agentsim::PCG32 rng(4);
    const agentsim::TemperatureField f(c, 100.0, 100.0, rng);
    const double left = f.sample(agentsim::Vec2{99.5, 50.0});  // W-1 附近
    const double right = f.sample(agentsim::Vec2{0.5, 50.0});  // 0 附近
    CHECK(std::fabs(left - right) < 0.05); // 跨边界必须连续
    // y 方向同理。
    agentsim::TemperatureConfig c2 = baseCfg();
    agentsim::TemperatureSource s2;
    s2.x = 50.0;
    s2.y = 1.0;
    s2.amplitude = -0.4;
    s2.radius = 20.0;
    c2.sources.push_back(s2);
    agentsim::TemperatureField f2(c2, 100.0, 100.0, rng);
    const double below = f2.sample(agentsim::Vec2{50.0, 99.5});
    const double above = f2.sample(agentsim::Vec2{50.0, 0.5});
    CHECK(std::fabs(below - above) < 0.05);
    CHECK(f2.sample(agentsim::Vec2{50.0, 1.0}) < 0.2); // 冷源中心明显低于基线
}

TEST(temp_deterministic_same_seed) {
    agentsim::TemperatureConfig c; // 3 个随机源
    agentsim::PCG32 r1(123), r2(123);
    agentsim::TemperatureField f1(c, 128.0, 64.0, r1);
    agentsim::TemperatureField f2(c, 128.0, 64.0, r2);
    CHECK(f1.sourceCount() == c.sourceCount);
    for (std::size_t i = 0; i < f1.sourceCount(); ++i) {
        CHECK(f1.sources()[i].x == f2.sources()[i].x);         // 逐位一致
        CHECK(f1.sources()[i].y == f2.sources()[i].y);
        CHECK(f1.sources()[i].amplitude == f2.sources()[i].amplitude);
        CHECK(f1.sources()[i].radius == f2.sources()[i].radius);
    }
    for (int i = 0; i < 10; ++i) {
        const agentsim::Vec2 p{1.7 * i, 0.9 * i * i};
        CHECK(f1.sample(p) == f2.sample(p)); // 采样也逐位一致
    }
}

TEST(temp_sample_and_distortion_in_unit_range) {
    agentsim::TemperatureConfig c;
    c.sourceCount = 6;
    c.baseline = 0.5;
    c.distortionK = 4.0; // 放大失真，考验 clamp
    agentsim::PCG32 rng(9);
    const agentsim::TemperatureField f(c, 100.0, 100.0, rng);
    for (int i = 0; i <= 10; ++i) {
        for (int j = 0; j <= 10; ++j) {
            const agentsim::Vec2 p{i * 10.0, j * 10.0};
            const double t = f.sample(p);
            const double d = f.distortion(p);
            CHECK(t >= 0.0 && t <= 1.0);
            CHECK(d >= 0.0 && d <= 1.0);
        }
    }
}

TEST(temp_distortion_zero_at_preferred) {
    // 精确解：单热源中心处 sample == baseline + amplitude（exp(0)==1），
    // 令 preferred 恰等于该值，则该点失真严格为 0。
    agentsim::TemperatureConfig c = baseCfg();
    c.baseline = 0.4;
    c.preferred = 0.8; // = baseline + amplitude
    agentsim::TemperatureSource s;
    s.x = 30.0;
    s.y = 30.0;
    s.amplitude = 0.4;
    s.radius = 25.0;
    c.sources.push_back(s);
    agentsim::PCG32 rng(11);
    const agentsim::TemperatureField f(c, 100.0, 100.0, rng);
    CHECK_NEAR(f.sample(agentsim::Vec2{30.0, 30.0}), 0.8, 1e-12);
    CHECK(f.distortion(agentsim::Vec2{30.0, 30.0}) == 0.0); // 最适温度处失真为 0
    CHECK(f.preferred() == 0.8);
    // 无源平场：baseline == preferred -> 处处失真为 0。
    agentsim::TemperatureConfig flat;
    flat.sourceCount = 0;
    flat.baseline = 0.5;
    flat.preferred = 0.5;
    agentsim::PCG32 rngF(3);
    agentsim::TemperatureField g(flat, rngF);
    CHECK_NEAR(g.distortion(agentsim::Vec2{37.0, 41.0}), 0.0, 1e-12);
    CHECK_NEAR(g.distortion(agentsim::Vec2{0.0, 0.0}), 0.0, 1e-12);
    CHECK_NEAR(g.preferred(), 0.5, 1e-12);
}

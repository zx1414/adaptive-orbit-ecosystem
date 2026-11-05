// agentsim - tests/unit/test_perceptron.cpp
// 感知机策略单测：Z 符号、sign 边界、确定性、全零权重无 NaN、
// serialize/deserialize 逐位一致、与 RulePolicy 输出可不同。全程无 RNG。
#include "test_framework.h"

#include <cmath>
#include <string>

#include "../../src/decision/PerceptronPolicy.h"
#include "../../src/decision/RulePolicy.h"

namespace {
using agentsim::ActionVector;
using agentsim::PerceptronPolicy;
using agentsim::RulePolicy;
using agentsim::SensorVector;

// 确定性构造输入（不用 <random>/rand）：三角函数 + 整数运算。
SensorVector makeInput(int variant) {
    SensorVector s;
    for (int j = 0; j < SensorVector::kDim; ++j) {
        const double t = static_cast<double>(variant * 7 + j);
        s[j] = std::sin(t * 0.37) * std::cos(t * 0.11);
    }
    return s;
}
} // namespace

// Z 计算正确：手工构造已知权重与输入，断言输出方向与 boost。
TEST(perceptron_z_known_weights) {
    PerceptronPolicy::Params p;
    p.w[0][0] = 1.0;   // Z0 = 1*2 + 0.5*(-1) + 0.25 = 1.75 > 0
    p.w[0][1] = 0.5;
    p.b[0] = 0.25;
    // 神经元 1 全零、b1=0 -> Z1 = 0 -> dirY 映射为 0，方向仅由 X 轴决定。
    PerceptronPolicy pol(p);
    SensorVector s;
    s[0] = 2.0;
    s[1] = -1.0;
    const ActionVector a = pol.decide(s);
    CHECK_NEAR(a.dirX, 1.0, 1e-12);      // clamp01((1.75+1)/2)=1 -> +1，归一化后仍为 1
    CHECK_NEAR(a.dirY, 0.0, 1e-12);
    CHECK_NEAR(a.boost, 0.5, 1e-12);     // Z0+Z1 = 1.75 > 0
}

// 反向：Z < 0 时映射到负方向；boost 按 Z0+Z1 的符号决定。
// 手算：Z0 = (-1)*(-1.5) + 0.5 = 2.0 -> dirX 原始 = clamp01(1.5)*2-1 = +1
//       Z1 = 1*(-1.5) + 0 = -1.5     -> dirY 原始 = clamp01(-0.25)*2-1 = -1
//       归一化后 = (+1,-1)/sqrt(2)；Z0+Z1 = 0.5 > 0 -> boost = 0.5
TEST(perceptron_negative_z_maps_negative) {
    PerceptronPolicy::Params p;
    p.w[1][0] = 1.0;
    p.w[0][0] = -1.0;
    p.b[0] = 0.5;
    PerceptronPolicy pol(p);
    SensorVector s;
    s[0] = -1.5;
    const ActionVector a = pol.decide(s);
    const double r2 = std::sqrt(0.5);
    CHECK_NEAR(a.dirX, r2, 1e-12);       // +1/sqrt(2)
    CHECK_NEAR(a.dirY, -r2, 1e-12);      // -1/sqrt(2)
    CHECK_NEAR(a.boost, 0.5, 1e-12);     // Z0+Z1 = 0.5 > 0
}

// sign 边界：Z = 0 -> +1，即 (Z+1)/2 = 0.5 -> 映射值 0；方向仅由另一维承担。
TEST(perceptron_sign_boundary_zero_is_positive) {
    PerceptronPolicy::Params p;
    p.w[0][0] = 0.0;   // Z0 = b0 = 0 -> y = +1 -> dirX 映射为 0（非 -1）
    p.w[1][0] = 2.0;   // Z1 = 2 -> dirY = +1
    PerceptronPolicy pol(p);
    SensorVector s;
    s[0] = 3.0;        // 权重为 0，Z0 仍为 0
    const ActionVector a = pol.decide(s);
    CHECK_NEAR(a.dirX, 0.0, 1e-12);      // 若 sign(0) 误取 -1，此处会是 -1
    CHECK_NEAR(a.dirY, 1.0, 1e-12);
}

// 确定性：相同权重相同输入 -> 逐位相同输出。
TEST(perceptron_deterministic_same_output) {
    PerceptronPolicy::Params p;
    for (int j = 0; j < SensorVector::kDim; ++j) {
        p.w[0][j] = 0.05 * (j % 5) - 0.1;
        p.w[1][j] = 0.07 * ((j + 2) % 4) - 0.05;
    }
    p.b[0] = -0.03;
    p.b[1] = 0.02;
    PerceptronPolicy a(p), b(p);
    for (int v = 0; v < 12; ++v) {
        const SensorVector s = makeInput(v);
        const ActionVector x = a.decide(s);
        const ActionVector y = b.decide(s);
        CHECK(x.dirX == y.dirX);
        CHECK(x.dirY == y.dirY);
        CHECK(x.boost == y.boost);
    }
}

// 种子构造：相同种子同初值，输出一致；不同种子至少一次输出不同。
TEST(perceptron_seeded_ctor_reproducible) {
    PerceptronPolicy a(12345, 0.1), b(12345, 0.1), c(999, 0.1);
    const SensorVector s = makeInput(3);
    const ActionVector x = a.decide(s), y = b.decide(s), z = c.decide(s);
    CHECK(x.dirX == y.dirX);
    CHECK(x.dirY == y.dirY);
    int diffs = (x.dirX != z.dirX) + (x.dirY != z.dirY) + (x.boost != z.boost);
    CHECK(diffs > 0);
}

// 全零权重：输出仅由偏置决定，boost 由 Z 之和决定，绝不产生 NaN/Inf。
TEST(perceptron_zero_weights_no_nan) {
    PerceptronPolicy::Params p;
    p.b[0] = 1.0;   // Z0 = 1.0 -> dirX 原始 = clamp01(1)*2-1 = +1
    p.b[1] = 0.0;   // Z1 = 0.0 -> sign 边界取 +1 -> dirY 原始 = 0
    PerceptronPolicy pol(p);
    const ActionVector a = pol.decide(makeInput(0));
    CHECK(std::isfinite(a.dirX));
    CHECK(std::isfinite(a.dirY));
    CHECK(std::isfinite(a.boost));
    CHECK_NEAR(a.dirX, 1.0, 1e-12);  // 长度恰为 1，归一化不变
    CHECK_NEAR(a.dirY, 0.0, 1e-12);
    CHECK_NEAR(a.boost, 0.5, 1e-12); // Z0+Z1 = 1 > 0
}

// 全零权重+全零偏置：两个方向原始值均为 0 -> 长度为 0 -> 输出 {0,0}，无 NaN/Inf。
TEST(perceptron_all_zero_outputs_zero_direction) {
    PerceptronPolicy pol(PerceptronPolicy::Params{});
    for (int v = 0; v < 8; ++v) {
        const ActionVector a = pol.decide(makeInput(v));
        CHECK(std::isfinite(a.dirX));
        CHECK(std::isfinite(a.dirY));
        CHECK(std::isfinite(a.boost));
        CHECK_NEAR(a.dirX, 0.0, 0.0);
        CHECK_NEAR(a.dirY, 0.0, 0.0);
        CHECK_NEAR(a.boost, 0.0, 0.0);
    }
}

// serialize -> deserialize -> 输出逐位一致（%.17g 保证 double 精确往返）。
TEST(perceptron_serialize_roundtrip_bit_exact) {
    PerceptronPolicy a(777, 0.1);
    const std::string text = a.serialize();
    PerceptronPolicy b = PerceptronPolicy::deserialize(text); // decide 非 const，接口如此
    for (int v = 0; v < 16; ++v) {
        const SensorVector s = makeInput(v);
        const ActionVector x = a.decide(s);
        const ActionVector y = b.decide(s);
        CHECK(x.dirX == y.dirX);
        CHECK(x.dirY == y.dirY);
        CHECK(x.boost == y.boost);
    }
    CHECK(b.serialize() == text); // 再导出文本也逐位一致
}

// 与 RulePolicy 在同一输入下输出可不同（证明感知机不是空壳/转发）。
TEST(perceptron_differs_from_rule_policy) {
    PerceptronPolicy pol(2026, 0.1);
    RulePolicy rule;
    int diffs = 0;
    for (int v = 0; v < 24; ++v) {
        SensorVector s = makeInput(v);
        s[6] = 0.9;                 // 构造有效食物候选
        s[0] = 0.6; s[1] = 0.8;     // 候选 0 方向 (0.6, 0.8)
        s[12] = 0.5; s[10] = 0.4; s[11] = 0.3; // 航向代理
        const ActionVector x = pol.decide(s);
        const ActionVector r = rule.decide(s);
        if (x.dirX != r.dirX || x.dirY != r.dirY) ++diffs;
    }
    CHECK(diffs > 0);
}

// 决策期无 RNG：同一实例跨多次 decide 输出不变（状态不被消耗）。
TEST(perceptron_decide_stateless) {
    PerceptronPolicy pol(42, 0.1);
    const SensorVector s = makeInput(5);
    const ActionVector first = pol.decide(s);
    for (int i = 0; i < 10; ++i) {
        const ActionVector again = pol.decide(s);
        CHECK(first.dirX == again.dirX);
        CHECK(first.dirY == again.dirY);
        CHECK(first.boost == again.boost);
    }
}

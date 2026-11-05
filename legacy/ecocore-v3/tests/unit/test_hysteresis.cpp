// agentsim - tests/unit/test_hysteresis.cpp
// HysteresisPolicy（滞回选择策略）单测：小 gap 不抖动、大 gap 会切换、gap=0 退化为
// 贪心、无候选零输出、无 RNG 双跑一致。红线：测试本身不用 RNG，全部手工构造传感器。
#include "test_framework.h"

#include <cmath>

#include "../../src/core/SensorTypes.h"
#include "../../src/decision/HysteresisPolicy.h"

namespace {

using agentsim::ActionVector;
using agentsim::HysteresisPolicy;
using agentsim::SensorVector;

// 单候选传感器：方向 (dx,dy) 归一化、接近度 prox（s[6+i]）、速度 (vx,vy)。
SensorVector oneCand(int slot, double dx, double dy, double prox,
                     double vx = 0.0, double vy = 0.0) {
    SensorVector s{};
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0) { dx /= len; dy /= len; }
    s[2 * slot] = dx;
    s[2 * slot + 1] = dy;
    s[6 + slot] = prox;
    s[10] = vx;
    s[11] = vy;
    const double sp = std::sqrt(vx * vx + vy * vy);
    s[12] = sp / (1.0 + sp);
    s[13] = 1.0;
    return s;
}

// 两候选传感器：A 在槽 0、B 在槽 1，速度置零（对齐项失效，纯接近度打分）。
SensorVector twoCands(double proxA, double proxB) {
    SensorVector s = oneCand(0, 1.0, 0.0, proxA);
    const SensorVector b = oneCand(1, 0.0, 1.0, proxB);
    s[2] = b[2];
    s[3] = b[3];
    s[7] = proxB;
    return s;
}

} // namespace

// ---- ① 小 gap：两候选分数接近时，连调 10 次选中恒为同一下标（消除抖动）。----
TEST(hysteresis_keeps_choice_when_gap_small) {
    // A=0.50 B=0.54：差 0.04 < gap 0.08。首帧（无现任）选当帧最高分 B，
    // 此后 B 为现任、A 的优势不足 gap -> 连调 10 次始终是 B（不抖动）。
    HysteresisPolicy pol;
    const SensorVector s = twoCands(0.50, 0.54);
    for (int k = 0; k < 10; ++k) {
        const ActionVector a = pol.decide(s);
        CHECK_NEAR(a.dirX, 0.0, 1e-12); // 始终指向槽 1（B）
        CHECK_NEAR(a.dirY, 1.0, 1e-12);
        CHECK(pol.lastChoice() == 1);
    }
}

// ---- ② 大 gap：挑战者分数比现任高出远超 gap 时会切换。----
TEST(hysteresis_switches_when_gap_large) {
    HysteresisPolicy pol;
    // 首帧 B 明显更优（0.2 vs 0.5，差 0.3 > gap）-> 选 B。
    const ActionVector a1 = pol.decide(twoCands(0.20, 0.50));
    CHECK_NEAR(a1.dirY, 1.0, 1e-12);
    CHECK(pol.lastChoice() == 1);
    // 下帧 A 反超（0.9 vs 0.5，差 0.4 > gap）-> 从 B 切到 A。
    const ActionVector a2 = pol.decide(twoCands(0.90, 0.50));
    CHECK_NEAR(a2.dirX, 1.0, 1e-12);
    CHECK(pol.lastChoice() == 0);
}

// ---- ③ gap=0：每次选当帧最高分，与贪心 argmax 完全一致。----
TEST(hysteresis_zero_gap_matches_greedy) {
    HysteresisPolicy::Params p;
    p.hysteresisGap = 0.0;
    HysteresisPolicy pol(p);
    // 序列：B 略优 -> 贪心选 B；A 反超 -> 贪心选 A；B 再微弱反超 -> 贪心选 B。
    CHECK(pol.decide(twoCands(0.50, 0.54)).dirY == 1.0);
    CHECK(pol.lastChoice() == 1);
    CHECK(pol.decide(twoCands(0.90, 0.50)).dirX == 1.0);
    CHECK(pol.lastChoice() == 0);
    CHECK(pol.decide(twoCands(0.89, 0.90)).dirY == 1.0);
    CHECK(pol.lastChoice() == 1);
}

// ---- ④ 无候选：全零输入 -> 输出零向量、boost=0，且不留下滞回记忆。----
TEST(hysteresis_no_candidates_zero_output) {
    HysteresisPolicy pol;
    SensorVector s{};
    const ActionVector a = pol.decide(s);
    CHECK_NEAR(a.dirX, 0.0, 1e-12);
    CHECK_NEAR(a.dirY, 0.0, 1e-12);
    CHECK_NEAR(a.boost, 0.0, 1e-12);
    CHECK(pol.lastChoice() == -1);
}

// ---- ⑤ 无 RNG：reset 后同一输入连调两次，输出逐位相同。----
TEST(hysteresis_deterministic) {
    HysteresisPolicy pol;
    // 预热两帧形成滞回记忆。
    (void)pol.decide(twoCands(0.50, 0.54));
    (void)pol.decide(twoCands(0.52, 0.54));
    const SensorVector s = twoCands(0.53, 0.55);
    pol.reset();
    const ActionVector a1 = pol.decide(s);
    pol.reset();
    const ActionVector a2 = pol.decide(s);
    CHECK(a1.dirX == a2.dirX);
    CHECK(a1.dirY == a2.dirY);
    CHECK(a1.boost == a2.boost);
    CHECK(pol.lastChoice() == 1); // 首帧（无现任）选当帧最高分 B（0.55 > 0.53）
}

// ---- ⑥ 现任候选消失时强制重选当帧最高分。----
TEST(hysteresis_reselects_when_current_vanishes) {
    HysteresisPolicy pol;
    (void)pol.decide(twoCands(0.54, 0.50)); // 现任 = 槽 0（A 分更高）
    CHECK(pol.lastChoice() == 0);
    // 槽 0 消失（s[6]=0），只剩槽 1 -> 必须切到槽 1。
    SensorVector s{};
    s[2] = 0.0; s[3] = 1.0;
    s[7] = 0.40;
    const ActionVector a = pol.decide(s);
    CHECK(pol.lastChoice() == 1);
    CHECK_NEAR(a.dirY, 1.0, 1e-12);
    CHECK_NEAR(a.boost, 0.40, 1e-12);
}

// ---- ⑦ 输出方向恒为单位向量（感知方向被噪声扰动后仍重新归一化）。----
TEST(hysteresis_output_is_unit_vector) {
    HysteresisPolicy pol;
    SensorVector s = twoCands(0.50, 0.30);
    s[0] = 3.0; s[1] = 4.0; // 非单位方向输入
    const ActionVector a = pol.decide(s);
    CHECK_NEAR(std::sqrt(a.dirX * a.dirX + a.dirY * a.dirY), 1.0, 1e-12);
}

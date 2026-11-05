// agentsim - tests/unit/test_thermal.cpp
// ThermalPolicy（趋温策略）单测：无目标早退、温度适宜度选中、失真规避排序、
// 高失真降速门控、方向归一化/防 NaN、无状态无 RNG（双跑一致）、与 RulePolicy 可分。
// 红线：测试本身也不使用 RNG，全部手工构造传感器向量。
#include "test_framework.h"

#include <cmath>
#include <initializer_list> // range-for over braced list（MSVC 需显式包含）

#include "../../src/core/SensorTypes.h"
#include "../../src/decision/RulePolicy.h"
#include "../../src/decision/ThermalPolicy.h"

namespace {

using agentsim::ActionVector;
using agentsim::RulePolicy;
using agentsim::SensorVector;
using agentsim::ThermalPolicy;

// 构造一个单候选传感器：方向 (dx,dy) 单位化、接近度 prox、s[9]=nearest、
// 失真 distortion、温度采样 temp。速度/边界维置 0（策略不消费）。
SensorVector oneFood(double dx, double dy, double prox,
                     double distortion, double temp) {
    SensorVector s{};
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0) { dx /= len; dy /= len; } else { dx = 0.0; dy = 0.0; }
    s[0] = dx; s[1] = dy;
    s[6] = prox;
    s[9] = (prox > 0.0) ? (1.0 - prox) : 0.0; // 近似一致即可：prox>0 <=> 有目标
    s[16] = distortion;
    s[17] = temp;
    return s;
}

} // namespace

// ---- ① 无目标：s[9]=0 且 s[6..8] 全 0 -> 输出 {0,0}，boost=0。----
TEST(thermal_no_target_returns_zero) {
    ThermalPolicy pol;
    SensorVector s{};
    const ActionVector a = pol.decide(s);
    CHECK_NEAR(a.dirX, 0.0, 1e-12);
    CHECK_NEAR(a.dirY, 0.0, 1e-12);
    CHECK_NEAR(a.boost, 0.0, 1e-12);
}

// ---- ② 同等距离下，温度更接近 preferredTemp 的候选被选中。----
// 两个候选等距（prox 同为 0.8）：A 处温度 0.5（=preferred），B 处 0.0。
// 温度项是自身位置全局项 -> 候选方向仍取决于接近度；此处用两次独立场景验证
// 温度适宜度评分：s[17]=0.5 时总分 > s[17]=0.0 时总分（candidateScore 直接验证）。
TEST(thermal_prefers_preferred_temperature) {
    ThermalPolicy pol;
    SensorVector hot = oneFood(1.0, 0.0, 0.8, 0.0, 0.5);
    SensorVector cold = oneFood(1.0, 0.0, 0.8, 0.0, 0.0);
    // 适宜度：|0.5-0.5|/0.5=0 -> 1.0；|0.0-0.5|/0.5=1 -> 0.0。
    CHECK_NEAR(pol.thermalSuitability(hot), 1.0, 1e-12);
    CHECK_NEAR(pol.thermalSuitability(cold), 0.0, 1e-12);
    // 温度适宜 → 总分更高（wThermal=1.2 放大差异）。
    CHECK(pol.candidateScore(hot, 0) > pol.candidateScore(cold, 0));
    // 适宜温度处的方向输出正常指向食物，高低温处策略仍追击（方向相同）。
    const ActionVector ah = pol.decide(hot);
    const ActionVector ac = pol.decide(cold);
    CHECK_NEAR(ah.dirX, 1.0, 1e-12);
    CHECK_NEAR(ac.dirX, 1.0, 1e-12);
}

// ---- 同距离同温度下，失真更大的候选场景得分更低（caution 生效）。----
// R6-P0-1 语义更新：caution 调制是“信任重分配”——失真压低 trustFood、放大
// trustStay，总分升降取决于两项配比（默认权重下不再单调）。故分别隔离两项
// 验证调制方向：接近度信任被失真抑制（保证无 argmax 反转），stay/thermal
// 信任被失真放大（参数仍生效）。
TEST(thermal_caution_penalizes_distortion) {
    ThermalPolicy pol;
    SensorVector cleanS = oneFood(1.0, 0.0, 0.8, 0.0, 0.5);
    SensorVector noisy = oneFood(1.0, 0.0, 0.8, 0.9, 0.5);
    CHECK_NEAR(pol.cautionFactor(cleanS), 1.0, 1e-12);
    CHECK_NEAR(pol.cautionFactor(noisy), 0.1, 1e-12);
    // 隔离接近度项（wThermal=0）：失真越大 -> trustFood 越小 -> 分数严格更低。
    ThermalPolicy::Params pFood;
    pFood.wThermal = 0.0;
    CHECK(ThermalPolicy(pFood).candidateScore(cleanS, 0)
        > ThermalPolicy(pFood).candidateScore(noisy, 0));
    // 隔离 stay/thermal 项（wProximity=0）：失真越大 -> trustStay 越大 -> 分数更高。
    ThermalPolicy::Params pStay;
    pStay.wProximity = 0.0;
    CHECK(ThermalPolicy(pStay).candidateScore(noisy, 0)
        > ThermalPolicy(pStay).candidateScore(cleanS, 0));
}

// ---- ③ s[16] > 0.5 时 boost = 0.3；否则 boost == 0。----
TEST(thermal_boost_gate_on_distortion) {
    ThermalPolicy pol;
    SensorVector low = oneFood(0.0, 1.0, 0.8, 0.5, 0.5);   // 恰在门限（不严格大于）
    SensorVector high = oneFood(0.0, 1.0, 0.8, 0.51, 0.5); // 刚过门限
    CHECK_NEAR(pol.decide(low).boost, 0.0, 1e-12);
    CHECK_NEAR(pol.decide(high).boost, 0.3, 1e-12);
    CHECK_NEAR(pol.decide(oneFood(0.0, 1.0, 0.8, 1.0, 0.5)).boost, 0.3, 1e-12);
}

// ---- ④ 输出方向长度约为 1（有目标）或恰为 0（无目标），二者必居其一。----
TEST(thermal_output_direction_unit_or_zero) {
    ThermalPolicy pol;
    const ActionVector a = pol.decide(oneFood(3.0, 4.0, 0.8, 0.2, 0.5));
    const double len = std::sqrt(a.dirX * a.dirX + a.dirY * a.dirY);
    CHECK(len > 0.99 && len < 1.01);
    const ActionVector z = pol.decide(SensorVector{});
    CHECK_NEAR(z.dirX, 0.0, 1e-12);
    CHECK_NEAR(z.dirY, 0.0, 1e-12);
}

// ---- 防御性：全 NaN 输入不崩、不产生 NaN 输出。----
TEST(thermal_all_nan_input_is_safe) {
    ThermalPolicy pol;
    SensorVector s{};
    for (int i = 0; i < SensorVector::kDim; ++i) {
        const double qnan = std::sqrt(-1.0); // 不引入 <random>，静默 NaN 常量
        s[i] = qnan;
    }
    const ActionVector a = pol.decide(s);
    CHECK(!std::isnan(a.dirX));
    CHECK(!std::isnan(a.dirY));
    CHECK(!std::isnan(a.boost));
    CHECK(!std::isinf(a.dirX) && !std::isinf(a.dirY) && !std::isinf(a.boost));
    // NaN 不满足 s[9] > 0，按无目标早退。
    CHECK_NEAR(a.dirX, 0.0, 1e-12);
    CHECK_NEAR(a.dirY, 0.0, 1e-12);
    CHECK_NEAR(a.boost, 0.0, 1e-12);
    // NaN 参数的构造也安全（权重/最适温度被清洗为有限值）。
    ThermalPolicy::Params bad;
    bad.wProximity = std::sqrt(-1.0);
    bad.preferredTemp = std::sqrt(-1.0);
    ThermalPolicy polBad(bad);
    const ActionVector ab = polBad.decide(oneFood(1.0, 0.0, 0.8, 0.2, 0.5));
    CHECK(!std::isnan(ab.dirX) && !std::isnan(ab.dirY) && !std::isnan(ab.boost));
}

// ---- 无状态、无 RNG：连续两次相同输入 -> 相同输出（逐字段一致）。----
TEST(thermal_deterministic_repeat_calls) {
    ThermalPolicy pol;
    const SensorVector s = oneFood(-1.0, 2.0, 0.6, 0.7, 0.4);
    const ActionVector a1 = pol.decide(s);
    const ActionVector a2 = pol.decide(s);
    CHECK_NEAR(a1.dirX, a2.dirX, 0.0);
    CHECK_NEAR(a1.dirY, a2.dirY, 0.0);
    CHECK_NEAR(a1.boost, a2.boost, 0.0);
    // 换实例（新构造）再跑一遍，策略无隐藏状态。
    ThermalPolicy pol2;
    const ActionVector a3 = pol2.decide(s);
    CHECK_NEAR(a1.dirX, a3.dirX, 0.0);
    CHECK_NEAR(a1.dirY, a3.dirY, 0.0);
    CHECK_NEAR(a1.boost, a3.boost, 0.0);
}

// ---- 与 RulePolicy 可分：同一输入下二者输出不同（非 RulePolicy 复制品）。----
// 场景：高失真 + 偏温（s[17]=0.0 偏离 preferred 0.5）。RulePolicy 追最近食物
// 且 boost=urgency；ThermalPolicy 高失真降速 boost=0.3，且接口语义完全不同。
TEST(thermal_differs_from_rule_policy) {
    ThermalPolicy tp;
    RulePolicy rp;
    const SensorVector s = oneFood(1.0, 0.0, 0.8, 0.9, 0.0);
    const ActionVector at = tp.decide(s);
    const ActionVector ar = rp.decide(s);
    // boost 语义不同：thermal 由失真门控，rule 由紧迫度驱动（s[12]=0 -> urgency=1）。
    CHECK(at.boost != ar.boost);
    CHECK_NEAR(ar.boost, 1.0, 1e-12);   // rule：无速度 -> 最紧迫 -> boost=1
    CHECK_NEAR(at.boost, 0.3, 1e-12);   // thermal：高失真降速
    // 接口层面也可分：策略名与得分接口是 thermal 独有的。
    CHECK(tp.name() != nullptr);
    // 策略名互异（"thermal" 不与 rule/stub/perceptron 共享前两字符）。
    const bool nameDiffers =
        tp.name()[0] == 't' && tp.name()[1] == 'h' && tp.name()[2] == 'e';
    CHECK(nameDiffers);
    CHECK(tp.candidateScore(s, 0) > 0.0);
}

// ---- R6-P2-2：双候选 argmax 在高 wCaution + 全失真（caution=0, d=1）下仍选近者。----
// 旧线性调制在 wc=5、caution=0 时 trustFood=1-2.5=-1.5，远者反超（argmax 反转）。
TEST(thermal_high_caution_two_candidates_still_picks_nearest) {
    SensorVector s{};
    s[0] = 1.0; s[1] = 0.0;  // 候选0：方向 (+1,0)，近
    s[2] = 0.0; s[3] = 1.0;  // 候选1：方向 (0,1)，远
    s[6] = 0.9;              // 近者接近度
    s[7] = 0.1;              // 远者接近度
    s[9] = 0.1;              // 有目标
    s[16] = 1.0;             // 全失真 -> caution = 0（d=1）
    s[17] = 0.5;             // 温度=最适：suitability=1；速度 0 -> stay=0 -> thermal=0.5
    ThermalPolicy::Params p; // 默认 wp=1, wt=1.2
    p.wCaution = 5.0;
    ThermalPolicy pol(p);
    // near = 0.9/3.5 + 1.2*0.5*3.5 ≈ 2.357 > far = 0.1/3.5 + 2.1 ≈ 2.129。
    CHECK(pol.candidateScore(s, 0) > pol.candidateScore(s, 1));
    const ActionVector a = pol.decide(s);
    CHECK_NEAR(a.dirX, 1.0, 1e-12);
    CHECK_NEAR(a.dirY, 0.0, 1e-12);
    CHECK_NEAR(a.boost, 0.3, 1e-12); // d=1 > 0.5：高失真降速门控不受修复影响
}

// ---- R6-P0-1：trustFood 恒正（经公开 candidateScore 间接断言）。----
// wThermal=0 时 score = wProximity*prox*trustFood，可反解 trustFood 直接验证恒正。
TEST(thermal_trust_food_stays_positive_for_any_wcaution) {
    const SensorVector s = oneFood(1.0, 0.0, 0.8, 1.0, 0.5); // caution=0：doubt 最大
    double prev = 2.0; // 上界哨兵：trustFood 随 wc 单调不增
    for (double wc : {0.0, 0.8, 3.0, 10.0, 100.0, 1e6}) {
        ThermalPolicy::Params p;
        p.wCaution = wc;
        p.wThermal = 0.0; // 隔离 proximity*trustFood 项
        ThermalPolicy pol(p);
        const double score = pol.candidateScore(s, 0);
        const double trustFood = score / (p.wProximity * 0.8);
        CHECK(trustFood > 0.0);          // 恒正：不再出现 argmax 反转
        CHECK(trustFood <= 1.0 + 1e-12); // 上界仍为 1
        CHECK(wc == 0.0 ? trustFood > 1.0 - 1e-12 : trustFood < 1.0 - 1e-12);
        CHECK(trustFood <= prev + 1e-12); // 单调不增（互异性由下一组断言补强）
        prev = trustFood;
    }
    // 关键 wCaution 取值下分数严格互异（参数真生效，不是死参数）。
    ThermalPolicy::Params p0; p0.wThermal = 0.0;
    ThermalPolicy::Params p3 = p0;  p3.wCaution = 3.0;
    ThermalPolicy::Params p100 = p0; p100.wCaution = 100.0;
    const double f0 = ThermalPolicy(p0).candidateScore(s, 0);
    const double f3 = ThermalPolicy(p3).candidateScore(s, 0);
    const double f100 = ThermalPolicy(p100).candidateScore(s, 0);
    CHECK(f0 > f3 && f3 > f100 && f100 > 0.0);
}

// ---- R6-P2-2 回归：wCaution=0 时与“完全没有 caution 项”等价。----
// doubt=0 -> trustFood=trustStay=1：分数退化为 wp*prox + wt*thermal，
// 且失真大小不再影响任何候选分数（失真只余 boost 门控一个作用点）。
TEST(thermal_zero_wcaution_equals_no_caution_term) {
    double scoreClean = 0.0;
    double scoreNoisy = 0.0;
    for (double distortion : {0.0, 0.5, 1.0}) {
        const SensorVector s = oneFood(1.0, 0.0, 0.8, distortion, 0.5);
        ThermalPolicy::Params p;
        p.wCaution = 0.0;
        ThermalPolicy pol(p);
        const double score = pol.candidateScore(s, 0);
        // 闭式参考（无 caution 项）：wp*0.8 + wt*1.0*0.5（速度 0 -> stay=0）。
        CHECK_NEAR(score, 1.0 * 0.8 + 1.2 * 0.5, 1e-12);
        if (distortion == 0.0) scoreClean = score;
        if (distortion == 1.0) scoreNoisy = score;
        const ActionVector a = pol.decide(s);
        CHECK_NEAR(a.dirX, 1.0, 1e-12);
        CHECK_NEAR(a.boost, distortion > 0.5 ? 0.3 : 0.0, 1e-12);
    }
    CHECK_NEAR(scoreClean, scoreNoisy, 0.0); // 失真对分数零影响 = 无 caution 项
}

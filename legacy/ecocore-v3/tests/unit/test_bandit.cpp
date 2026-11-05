// agentsim - tests/unit/test_bandit.cpp
// T24：BanditPolicy（Hedge/EXP3-lite 双臂加权）单测。
// 覆盖：初始等权、权重恒归一、反塌零（不塌成单臂）、输出单位/零方向、
// 确定性（无 RNG，重置后逐位一致）、单臂持续奖励下的双向学习、
// NaN/Inf 极端输入不产生 NaN 输出、eta 构造夹取、reset 回初态、长跑稳定。
// 红线：测试本身也绝不使用 RNG，全部手工构造确定性感知序列。
#include "test_framework.h"

#include <cmath>
#include <cstring>

#include "../../src/core/SensorTypes.h"
#include "../../src/decision/BanditPolicy.h"

namespace {

using agentsim::ActionVector;
using agentsim::BanditPolicy;
using agentsim::SensorVector;
using agentsim::ThermalPolicy;

// 基础场景：单食物位于角 theta（度），接近度 prox，速度沿食物方向 speed。
SensorVector oneFood(double thetaDeg, double prox, double speed) {
    SensorVector s{};
    const double a = thetaDeg * 3.14159265358979323846 / 180.0;
    s[0] = std::cos(a);
    s[1] = std::sin(a);
    s[6] = prox;
    s[9] = (prox > 0.0) ? (1.0 - prox) : 0.0;
    const double k = speed / (1.0 + speed);
    s[10] = std::cos(a) * k;
    s[11] = std::sin(a) * k;
    s[12] = k;
    return s;
}

// 前进一帧（单食物 + 指定速度），返回输出。
ActionVector step(BanditPolicy& p, double thetaDeg, double prox, double speed) {
    return p.decide(oneFood(thetaDeg, prox, speed));
}

// ---- 奖励 rule 臂的场景（可解析验证，wThermal=3.0 配套）----
// c0=(1,0) 近（prox 1.0）、c1=(0,1) 远（prox 0.4）、v̂=(0.28,0.96)、s[12]=0.5、
// s[16]=0、s[17]=0.5（suitability=1）：
//   R：s0 = 1.0+0.8*0.28+0.3 = 1.524 > s1 = 0.4+0.8*0.96+0.3 = 1.468 → 选 c0（边际 0.056）
//   T：0.4+3*(0.5+0.48) = 3.34 > 1.0+3*0.64 = 2.92 → 选 c1（边际 0.42）
//   potR = 1.0/1.4 = 0.714 > potT = 0.4/1.4 = 0.286（R 指向近食物）
//   rEnv = 0.4*0.5*(2*1-1) = +0.2 每帧恒定
//   rR-rT = 0.1*(agreeR-agreeT) + 0.428 ≥ 0.428 - 0.1 > 0 每帧恒正。
SensorVector ruleFavoredScenario() {
    SensorVector s{};
    s[0] = 1.0; s[1] = 0.0;     // c0 = (+1,0)，近
    s[2] = 0.0; s[3] = 1.0;     // c1 = (0,+1)，远
    s[6] = 1.0;
    s[7] = 0.4;
    s[9] = 0.1;                 // 有目标
    s[10] = 0.14; s[11] = 0.48; // v̂ = (0.28,0.96)，|v̂|*s[12] 一致
    s[12] = 0.5;
    s[16] = 0.0;
    s[17] = 0.5;
    return s;
}

// ---- 奖励 thermal 臂的场景（可解析验证，wThermal=0.6 配套）----
// 关键：势能按接近度加权，谁指向近食物谁得势能。要奖励 T，必须 T 的 argmax
// 指向近食物、R 被对齐项引诱到远食物。全失真 s[16]=1 把 R 的接近度信任打到
// 0.5、T 的打到 1/1.4≈0.714，同时 T 的 stay 项被 trustStay=1.4 放大：
// 配 wThermal=0.6 后 T 的 stay/prox 有效比 (0.6*1.4)/0.714≈1.18 < R 的 0.8/0.5=1.6，
// 即 T 比R 更“看重接近度”→ 存在 (T 选近, R 选远) 的可行窗口：
// cN=(-0.2,√0.96)（近，prox 1.0）、cF=-cN（远，prox 0.5）、v̂=(1,0)、s[12]=0.5：
//   R：0.5*1.0+0.8*(-0.2)+0.3 = 0.64 < 0.5*0.5+0.8*0.2+0.3 = 0.71 → 选远（边际 0.07）
//   T：1.0/1.4+0.6*0.4*1.4 = 1.050 > 0.5/1.4+0.6*0.6*1.4 = 0.861 → 选近（边际 0.19）
//   potT = 1.0/1.5 = 2/3 > potR = 0.5/1.5 = 1/3（T 指向近食物，恒差 1/3）
//   rEnv = +0.2 恒定；rT-rR = 0.2*(wT-wR) + 1/3 ≥ 0.133 > 0 每帧恒正。
// 两候选方向恰互为反向（cF=-cN），agreement 差分被放大到 2*(wT-wR)。
SensorVector thermalFavoredScenario() {
    SensorVector s{};
    const double c = 0.2;
    const double y = std::sqrt(1.0 - c * c); // √0.96
    s[0] = -c; s[1] = y;        // cN（近）
    s[2] = c;  s[3] = -y;       // cF（远）= -cN
    s[6] = 1.0;
    s[7] = 0.5;
    s[9] = 0.1;                 // 有目标
    s[10] = 0.5; s[11] = 0.0;   // v̂ = (1,0)
    s[12] = 0.5;
    s[16] = 1.0;                // 全失真：R trust=0.5，T trustFood=1/1.4
    s[17] = 0.5;                // suitability = 1
    return s;
}

ThermalPolicy::Params ruleFavoredParams() {
    ThermalPolicy::Params p;
    p.wThermal = 3.0; // stay 差异压过接近度差 → T 被引向远食物（见上注释）
    return p;
}

ThermalPolicy::Params thermalFavoredParams() {
    ThermalPolicy::Params p;
    p.wThermal = 0.6; // 压低 stay/prox 比 → T 守近食物、R 被对齐引走（见上注释）
    return p;
}

} // namespace

// ---- 1. 构造后初始权重严格等分 0.5/0.5。----
TEST(bandit_initial_equal_weights) {
    BanditPolicy p;
    CHECK_NEAR(p.weightRule(), 0.5, 1e-12);
    CHECK_NEAR(p.weightThermal(), 0.5, 1e-12);
    CHECK_NEAR(p.weightRule() + p.weightThermal(), 1.0, 1e-9);
    // 默认学习率也暴露在接口里，便于文档化。
    CHECK_NEAR(p.eta(), 0.05, 1e-12);
}

// ---- 2. 连调 100 次（含极端场景）：和恒为 1，误差 < 1e-9。----
TEST(bandit_weights_stay_normalized) {
    BanditPolicy p;
    // 混合多种场景：近/远、高速/静止、无目标，观察更新路径全覆盖。
    const double angles[] = {0.0, 90.0, 180.0, 270.0, 45.0};
    const double proxs[] = {0.9, 0.5, 0.1, 0.0};
    for (int i = 0; i < 100; ++i) {
        const double sp = (i % 3 == 0) ? 2.0 : 0.0;
        step(p, angles[i % 5], proxs[i % 4], sp);
        const double sum = p.weightRule() + p.weightThermal();
        CHECK(std::fabs(sum - 1.0) < 1e-9);
        CHECK(p.weightRule() > 0.0);
        CHECK(p.weightThermal() > 0.0);
    }
}

// ---- 3. 连调 1000 次极端输入：两权重恒 > 0（地板生效，永不塌成单臂）。----
// eta=10 最大学习率、持续强负回报差，对数权重被压到极深负值，
// softmax 下臂 T 理论权重 ~e^-数百，但 kWeightFloor 地板保证恒有 w >= 1e-6。
TEST(bandit_weights_stay_positive) {
    BanditPolicy p(10.0);
    for (int i = 0; i < 1000; ++i) {
        step(p, 90.0, 0.05, 2.0); // 静止食物 + 高速冲刺：pot 差 + rEnv 双重压 T
        CHECK(p.weightRule() > 0.0);
        CHECK(p.weightThermal() > 0.0);
    }
    CHECK(std::fabs(p.weightRule() + p.weightThermal() - 1.0) < 1e-9);
    CHECK(p.weightRule() <= 1.0 && p.weightThermal() <= 1.0);
}

// ---- 4. 输出方向长度 ∈ {0} ∪ [0.999, 1.001]，boost ∈ [0,1]。----
TEST(bandit_output_unit_or_zero) {
    BanditPolicy p;
    // 无目标 -> 零向量。
    const ActionVector z = p.decide(SensorVector{});
    CHECK_NEAR(z.dirX, 0.0, 1e-12);
    CHECK_NEAR(z.dirY, 0.0, 1e-12);
    CHECK(z.boost >= 0.0 && z.boost <= 1.0);
    // 多帧、多方向：非零输出必为单位向量。
    const double angles[] = {10.0, 130.0, 250.0, 300.0};
    for (int i = 0; i < 200; ++i) {
        const ActionVector a = step(p, angles[i % 4], 0.2 + 0.2 * (i % 3), 1.0 + (i % 2));
        const double len = std::sqrt(a.dirX * a.dirX + a.dirY * a.dirY);
        const bool zero = (len == 0.0);
        CHECK(zero || (len > 0.999 && len < 1.001));
        CHECK(a.boost >= 0.0 && a.boost <= 1.0);
        CHECK(!std::isnan(a.dirX) && !std::isnan(a.dirY) && !std::isnan(a.boost));
    }
}

// ---- 5. 确定性：同输入序列、重置后两次调用逐位一致 —— 证明 decide 无 RNG。----
TEST(bandit_deterministic) {
    BanditPolicy p1;
    BanditPolicy p2;
    const double angles[] = {0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0};
    const double proxs[] = {0.8, 0.6, 0.4, 0.2};
    for (int i = 0; i < 50; ++i) {
        const double th = angles[i % 8];
        const double pr = proxs[i % 4];
        const double sp = 0.5 * (i % 4);
        const ActionVector a1 = step(p1, th, pr, sp);
        const ActionVector a2 = step(p2, th, pr, sp);
        CHECK_NEAR(a1.dirX, a2.dirX, 0.0);
        CHECK_NEAR(a1.dirY, a2.dirY, 0.0);
        CHECK_NEAR(a1.boost, a2.boost, 0.0);
        // 权重轨迹也逐位一致。
        CHECK_NEAR(p1.weightRule(), p2.weightRule(), 0.0);
    }
    // 单实例 reset 后重放同一序列：输出逐位一致（状态完全可重置）。
    BanditPolicy p3;
    ActionVector first[50];
    for (int i = 0; i < 50; ++i) {
        first[i] = step(p3, angles[i % 8], proxs[i % 4], 0.5 * (i % 4));
    }
    p3.reset();
    CHECK_NEAR(p3.weightRule(), 0.5, 1e-12);
    CHECK_NEAR(p3.weightThermal(), 0.5, 1e-12);
    for (int i = 0; i < 50; ++i) {
        const ActionVector b = step(p3, angles[i % 8], proxs[i % 4], 0.5 * (i % 4));
        CHECK_NEAR(b.dirX, first[i].dirX, 0.0);
        CHECK_NEAR(b.dirY, first[i].dirY, 0.0);
        CHECK_NEAR(b.boost, first[i].boost, 0.0);
    }
}

// ---- 6. 学习方向性：两臂意见相左、持续“奖励 rule 侧”，wRule_ > wThermal_。----
// 场景 A（ruleFavoredScenario 注释含完整解析）：每帧 rR - rT >= 0.328 > 0，
// 100 帧 eta=0.5 后对数权重差 > 30 → wRule_ 精确逼近 1（地板之上）。
TEST(bandit_learns_toward_better_arm) {
    BanditPolicy p(0.5, ruleFavoredParams());
    const SensorVector s = ruleFavoredScenario();
    for (int i = 0; i < 100; ++i) {
        (void)p.decide(s);
    }
    CHECK(p.weightRule() > p.weightThermal());
    CHECK(p.weightRule() > 0.9);
    CHECK(p.weightThermal() < 0.1);
    CHECK(std::fabs(p.weightRule() + p.weightThermal() - 1.0) < 1e-9);
    CHECK(p.weightRule() > 0.0 && p.weightThermal() > 0.0); // 地板仍托底
}

// ---- 6b. 反向：持续“奖励 thermal 侧”，wThermal_ 反超（权重可双向学习）。----
// 场景 B（thermalFavoredScenario 注释含完整解析）：每帧 rT - rR >= 0.133 > 0，
// 150 帧 eta=0.5 后对数权重差 >= 10 → wThermal_ > 0.99。
TEST(bandit_learns_toward_thermal_arm_when_reversed) {
    BanditPolicy p(0.5, thermalFavoredParams());
    const SensorVector s = thermalFavoredScenario();
    for (int i = 0; i < 150; ++i) {
        (void)p.decide(s);
    }
    CHECK(p.weightThermal() > p.weightRule());
    CHECK(p.weightThermal() > 0.9);
    CHECK(p.weightRule() < 0.1);
    CHECK(std::fabs(p.weightRule() + p.weightThermal() - 1.0) < 1e-9);
    CHECK(p.weightRule() > 0.0 && p.weightThermal() > 0.0);
}

// ---- 7. 全 NaN / 全 Inf 输入不产生 NaN 输出；极端混合输入也安全。----
TEST(bandit_extreme_sensor_no_nan) {
    const double qnan = std::sqrt(-1.0); // 不引入 <random>，静默 NaN 常量
    // 全 NaN。
    BanditPolicy p;
    SensorVector sn{};
    for (int i = 0; i < SensorVector::kDim; ++i) sn[i] = qnan;
    ActionVector a = p.decide(sn);
    CHECK(!std::isnan(a.dirX) && !std::isnan(a.dirY) && !std::isnan(a.boost));
    CHECK(!std::isinf(a.dirX) && !std::isinf(a.dirY) && !std::isinf(a.boost));
    CHECK_NEAR(a.dirX, 0.0, 1e-12);
    CHECK_NEAR(a.dirY, 0.0, 1e-12);
    // 全 Inf（含 ±Inf）。
    SensorVector si{};
    for (int i = 0; i < SensorVector::kDim; ++i) si[i] = HUGE_VAL;
    for (int i = 0; i < SensorVector::kDim; i += 2) si[i] = -HUGE_VAL;
    a = p.decide(si);
    CHECK(!std::isnan(a.dirX) && !std::isnan(a.dirY) && !std::isnan(a.boost));
    CHECK(!std::isinf(a.dirX) && !std::isinf(a.dirY) && !std::isinf(a.boost));
    // NaN/Inf 与正常值交替；再接正常帧确认权重状态机未被污染。
    SensorVector sm{};
    sm[0] = 1.0; sm[1] = 0.0; sm[6] = 0.8; sm[9] = 0.2;
    for (int i = 0; i < SensorVector::kDim; ++i) {
        if (i % 3 == 0) sm[i] = qnan;
        if (i % 3 == 1) sm[i] = HUGE_VAL;
    }
    a = p.decide(sm);
    CHECK(!std::isnan(a.dirX) && !std::isnan(a.dirY) && !std::isnan(a.boost));
    for (int i = 0; i < 10; ++i) a = step(p, 30.0, 0.7, 1.0);
    CHECK(!std::isnan(a.dirX) && !std::isnan(a.dirY) && !std::isnan(a.boost));
    const double sum = p.weightRule() + p.weightThermal();
    CHECK(std::fabs(sum - 1.0) < 1e-9);
    CHECK(p.weightRule() > 0.0 && p.weightThermal() > 0.0);
    // 非 NaN 哨兵：权重仍有限。
    CHECK(std::isfinite(p.weightRule()) && std::isfinite(p.weightThermal()));
}

// ---- 8. eta 构造夹取：NaN/负值/超大值均安全，且 eta=0 冻结权重。----
TEST(bandit_eta_clamped_and_zero_freezes) {
    const double qnan = std::sqrt(-1.0);
    BanditPolicy pNaN(qnan);
    CHECK_NEAR(pNaN.eta(), 0.0, 1e-12);      // NaN -> 0（冻结）
    BanditPolicy pNeg(-3.0);
    CHECK_NEAR(pNeg.eta(), 0.0, 1e-12);
    BanditPolicy pHuge(1e9);
    CHECK_NEAR(pHuge.eta(), 10.0, 1e-12);    // 上限 10
    // eta=0：任何输入序列后权重保持 0.5/0.5（不学习）。
    BanditPolicy p0(0.0);
    for (int i = 0; i < 20; ++i) step(p0, 45.0 * i, 0.3 + 0.01 * i, 1.0);
    CHECK_NEAR(p0.weightRule(), 0.5, 1e-12);
    CHECK_NEAR(p0.weightThermal(), 0.5, 1e-12);
}

// ---- 9. 两臂方向一致时混合输出与两臂相同（权重凸组合仍指向同一方向）。----
TEST(bandit_mixed_direction_matches_arms_when_agree) {
    BanditPolicy p;
    // 单候选 (1,0)：rule 与 thermal 都会选它。
    const ActionVector ar = step(p, 0.0, 0.8, 0.0);
    CHECK_NEAR(ar.dirX, 1.0, 1e-9);
    CHECK_NEAR(ar.dirY, 0.0, 1e-9);
    CHECK(ar.boost >= 0.0 && ar.boost <= 1.0);
}

// ---- 10. 长跑稳定性：2000 帧混合场景（奖励 R 的帧 + 无目标/半径外穿插）后
// 权重仍归一、恒正、有限，且确实离开过 0.5（权重机制在长程中真实运转）。
// 注：首帧无历史不更新（权重恰为 0.5），故用 maxDev 而非 minDev 做哨兵。----
TEST(bandit_long_run_stability) {
    BanditPolicy p(0.5, ruleFavoredParams());
    const SensorVector fav = ruleFavoredScenario();
    const double angles[] = {0.0, 60.0, 120.0, 180.0, 240.0, 300.0};
    double maxDev = 0.0; // |wR - 0.5| 的最大值哨兵
    for (int i = 0; i < 2000; ++i) {
        if (i % 9 == 4) {
            (void)p.decide(SensorVector{}); // 无目标帧
        } else if (i % 9 == 6) {
            (void)step(p, angles[i % 6], 0.0, 1.5); // 半径外（prox=0）帧
        } else {
            (void)p.decide(fav); // 奖励 R 的帧 → 净漂移
        }
        const double dev = std::fabs(p.weightRule() - 0.5);
        if (dev > maxDev) maxDev = dev;
        if (i % 25 == 24) {
            const double sum = p.weightRule() + p.weightThermal();
            CHECK(std::fabs(sum - 1.0) < 1e-9);
            CHECK(p.weightRule() > 0.0 && p.weightThermal() > 0.0);
            CHECK(std::isfinite(p.weightRule()) && std::isfinite(p.weightThermal()));
        }
    }
    // 权重确实动过（不是死权重）：一度偏离 0.5 超过 0.05。
    CHECK(maxDev > 0.05);
    const double sum = p.weightRule() + p.weightThermal();
    CHECK(std::fabs(sum - 1.0) < 1e-9);
    CHECK(p.weightRule() > 0.0 && p.weightThermal() > 0.0);
}

// ==================== T24 扩展组：混合公式 / 状态机 / 长程回归 ====================

// ---- B1. boost 混合公式：冻结权重 0.5/0.5 下，boost = 0.5*boostR + 0.5*boostT。----
// rule boost = urgency = 1 - s[12]（s[12] 是归一化速度 speed/(1+speed)）；
// thermal boost = (s[16] > 0.5 ? 0.3 : 0)。
//   场景1：speed=0（s[12]=0）、s[16]=0   -> 0.5*1.0 + 0.5*0.0 = 0.5
//   场景2：speed=0（s[12]=0）、s[16]=1   -> 0.5*1.0 + 0.5*0.3 = 0.65
//   场景3：speed=1（s[12]=0.5）、s[16]=1 -> 0.5*0.5 + 0.5*0.3 = 0.4
// （eta=0 冻结权重，单候选两臂同向，混合公式可解析验证。）
TEST(bandit_boost_blends_arms_weights_frozen) {
    SensorVector base{};
    base[0] = 1.0; base[1] = 0.0; base[6] = 0.8; base[9] = 0.2;
    struct Case { double s12; double s16; double expect; };
    const Case cases[] = {{0.0, 0.0, 0.5}, {0.0, 1.0, 0.65}, {1.0, 1.0, 0.4}};
    for (const Case& c : cases) {
        BanditPolicy p(0.0); // 冻结 0.5/0.5
        SensorVector s = base;
        const double k = c.s12 / (1.0 + c.s12);
        s[10] = k; s[11] = 0.0; s[12] = k; // 速度沿 +x，模长一致
        s[16] = c.s16; s[17] = 0.5;
        const ActionVector a = p.decide(s);
        CHECK_NEAR(a.boost, c.expect, 1e-9);
        CHECK_NEAR(a.dirX, 1.0, 1e-9); // 两臂同向 (+1,0)
        CHECK_NEAR(a.dirY, 0.0, 1e-9);
    }
}

// ---- B2. 冻结权重下输出逐帧恒定，且方向 = normalize(0.5*uR + 0.5*uT) = 45°。----
// ruleFavoredScenario：uR=(1,0)、uT=(0,1) -> 混合方向 (0.7071,0.7071)；boost=0.25。
TEST(bandit_frozen_weights_constant_output_and_blend_direction) {
    BanditPolicy p(0.0, ruleFavoredParams());
    const SensorVector s = ruleFavoredScenario();
    const ActionVector first = p.decide(s);
    CHECK_NEAR(first.dirX, 0.7071067811865476, 1e-9);
    CHECK_NEAR(first.dirY, 0.7071067811865476, 1e-9);
    CHECK_NEAR(first.boost, 0.25, 1e-9); // 0.5*urgency(0.5) + 0.5*0
    for (int i = 0; i < 10; ++i) {
        const ActionVector a = p.decide(s);
        CHECK_NEAR(a.dirX, first.dirX, 0.0); // 权重冻结 -> 逐位恒定
        CHECK_NEAR(a.dirY, first.dirY, 0.0);
        CHECK_NEAR(a.boost, first.boost, 0.0);
    }
}

// ---- B3. thermal 参数透传：preferredTemp 不同 -> 臂 T 选择不同 -> 混合输出不同。----
// s[17]=0：pref=0.0 -> suitability=1 -> T 选远候选（stay 项主导）；pref=0.5 ->
// suitability=0 -> 温度项归零 -> T 退回最近候选。eta=0 冻结权重隔离混合效应。
TEST(bandit_thermal_params_passthrough_changes_output) {
    SensorVector s = ruleFavoredScenario();
    s[17] = 0.0; // 温度采样 = 0
    ThermalPolicy::Params pa;
    pa.wThermal = 3.0;
    pa.preferredTemp = 0.0; // 适宜 -> T 被stay项引向远候选
    ThermalPolicy::Params pb;
    pb.wThermal = 3.0;
    pb.preferredTemp = 0.5; // 全不适宜 -> 温度项归零
    BanditPolicy pa0(0.0, pa);
    BanditPolicy pb0(0.0, pb);
    const ActionVector oa = pa0.decide(s);
    const ActionVector ob = pb0.decide(s);
    const double diff = std::sqrt((oa.dirX - ob.dirX) * (oa.dirX - ob.dirX)
                                + (oa.dirY - ob.dirY) * (oa.dirY - ob.dirY));
    CHECK(diff > 0.1); // 参数真生效（非死参数）
    CHECK_NEAR(ob.dirX, 1.0, 1e-9);  // B：两臂都选近候选
    CHECK(oa.dirY > 0.1);            // A：T 分量混入
}

// ---- B4. reset 清空全部历史：重置后重放与全新实例逐位一致（覆盖隐藏历史字段）。----
TEST(bandit_reset_clears_history_bit_exact) {
    const SensorVector fav = ruleFavoredScenario();
    const double angles[] = {15.0, 75.0, 200.0};
    BanditPolicy p1;
    BanditPolicy p2;
    for (int i = 0; i < 5; ++i) (void)p2.decide(fav); // 制造历史
    p2.reset();
    for (int i = 0; i < 20; ++i) {
        ActionVector a1, a2;
        if (i % 4 == 3) {
            a1 = p1.decide(SensorVector{});
            a2 = p2.decide(SensorVector{});
        } else if (i % 4 == 1) {
            a1 = p1.decide(oneFood(angles[i % 3], 0.0, 1.0));
            a2 = p2.decide(oneFood(angles[i % 3], 0.0, 1.0));
        } else {
            a1 = p1.decide(fav);
            a2 = p2.decide(fav);
        }
        CHECK_NEAR(a1.dirX, a2.dirX, 0.0);
        CHECK_NEAR(a1.dirY, a2.dirY, 0.0);
        CHECK_NEAR(a1.boost, a2.boost, 0.0);
        CHECK_NEAR(p1.weightRule(), p2.weightRule(), 0.0);
    }
}

// ---- B5. 首次更新就偏向被奖励的臂：第 1 帧无历史（0.5 不动），第 2 帧起 wR > 0.5。----
TEST(bandit_first_update_already_favors_rewarded_arm) {
    BanditPolicy p(0.5, ruleFavoredParams());
    const SensorVector s = ruleFavoredScenario();
    (void)p.decide(s); // 首帧只建历史
    CHECK_NEAR(p.weightRule(), 0.5, 1e-12);
    (void)p.decide(s); // 首次更新：rR - rT = 0.428 > 0
    CHECK(p.weightRule() > 0.5);
    CHECK(p.weightThermal() < 0.5);
    // 反向场景对称成立。
    BanditPolicy q(0.5, thermalFavoredParams());
    const SensorVector t = thermalFavoredScenario();
    (void)q.decide(t);
    (void)q.decide(t);
    CHECK(q.weightThermal() > 0.5);
}

// ---- B6. 不变量：两臂方向恒相同时回报恒相等 -> 权重恒 0.5/0.5（对称性）。----
// 单候选直行：uR == uT == uOut -> agreeR == agreeT、potR == potT -> rR == rT。
TEST(bandit_symmetric_agreement_keeps_equal_weights) {
    BanditPolicy p;
    for (int i = 0; i < 50; ++i) {
        (void)step(p, 25.0, 0.7, 1.0);
        CHECK_NEAR(p.weightRule(), 0.5, 1e-12);
        CHECK_NEAR(p.weightThermal(), 0.5, 1e-12);
    }
}

// ---- B7. 两臂全部弃权 -> 输出精确零向量（候选存在但方向/门控排除）。----
TEST(bandit_all_arms_abstain_outputs_zero) {
    BanditPolicy p;
    // 候选方向近零（1e-15 < 1e-12 阈值）-> 两臂都弃权。
    SensorVector s{};
    s[0] = 1e-15; s[1] = 0.0;
    s[6] = 0.8; s[9] = 0.2;
    const ActionVector a = p.decide(s);
    CHECK_NEAR(a.dirX, 0.0, 1e-12);
    CHECK_NEAR(a.dirY, 0.0, 1e-12);
    CHECK(a.boost >= 0.0 && a.boost <= 1.0); // boost 仍混合（rule urgency=1 的一半）
    // s[9]=0 但候选方向有效：rule 要求 prox>0（有）、thermal 要求 s[9]>0（无）。
    // s[6]=0 时 rule 弃权、thermal 弃权 -> 零向量。
    SensorVector t{};
    t[0] = 1.0; t[1] = 0.0;
    t[9] = 0.0; // thermal 无目标
    const ActionVector b = p.decide(t);
    CHECK_NEAR(b.dirX, 0.0, 1e-12);
    CHECK_NEAR(b.dirY, 0.0, 1e-12);
}

// ---- B8. 负值输入一律被 clamp01/单位化吸收：无 NaN、输出合法、权重归一。----
TEST(bandit_negative_sensor_values_are_clamped) {
    BanditPolicy p;
    // 全负：等价全零 -> 零向量。
    SensorVector sn{};
    for (int i = 0; i < SensorVector::kDim; ++i) sn[i] = -1.5;
    const ActionVector a = p.decide(sn);
    CHECK(!std::isnan(a.dirX) && !std::isnan(a.dirY) && !std::isnan(a.boost));
    CHECK_NEAR(a.dirX, 0.0, 1e-12);
    // 负方向 + 有效接近度：正常指向负方向。
    SensorVector sd{};
    sd[0] = -1.0; sd[1] = 0.0;
    sd[6] = 0.7; sd[9] = 0.3;
    sd[12] = -2.0; // 负速度 -> clamp01 -> 0
    const ActionVector b = p.decide(sd);
    CHECK(b.dirX < -0.999); // 指向 (-1,0)
    CHECK(!std::isnan(b.dirY) && !std::isnan(b.boost));
    for (int i = 0; i < 20; ++i) (void)p.decide(sd);
    CHECK(std::fabs(p.weightRule() + p.weightThermal() - 1.0) < 1e-9);
    CHECK(p.weightRule() > 0.0 && p.weightThermal() > 0.0);
}

// ---- B9. 地板数值：极端单边奖励后 wThermal_ 被钉在地板 ~1e-6，恒正、和为 1。----
TEST(bandit_weight_floor_pins_thermal_near_floor) {
    BanditPolicy p(10.0, ruleFavoredParams()); // eta=10，每帧 log 差 ~4.28
    const SensorVector s = ruleFavoredScenario();
    for (int i = 0; i < 200; ++i) (void)p.decide(s); // log 差 ~850 -> softmax 下溢
    CHECK(p.weightThermal() > 0.0);           // 地板托底，恒正
    CHECK(p.weightThermal() < 1e-4);          // 且确实被压到地板量级
    CHECK(p.weightRule() > 0.9999);
    CHECK(std::fabs(p.weightRule() + p.weightThermal() - 1.0) < 1e-9);
}

// ---- B10. 学成后输出对齐胜者臂：wR≈1 -> 输出 ≈ uR = (1,0)。----
TEST(bandit_learned_direction_aligns_with_winner_arm) {
    BanditPolicy p(10.0, ruleFavoredParams());
    const SensorVector s = ruleFavoredScenario();
    for (int i = 0; i < 200; ++i) (void)p.decide(s);
    const ActionVector a = p.decide(s);
    CHECK(a.dirX > 0.99);
    CHECK(std::fabs(a.dirY) < 0.1);
    CHECK(std::fabs(std::sqrt(a.dirX * a.dirX + a.dirY * a.dirY) - 1.0) < 1e-9);
}

// ---- B11. 万帧马拉松：对数权重差 ~4e4 不溢出，输出/权重全程有限且归一。----
TEST(bandit_10000_frame_marathon_finite_and_normalized) {
    BanditPolicy p(10.0, ruleFavoredParams()); // 最大 eta 加速分离
    const SensorVector fav = ruleFavoredScenario();
    const double angles[] = {0.0, 60.0, 120.0, 180.0, 240.0, 300.0};
    for (int i = 0; i < 10000; ++i) {
        ActionVector a;
        if (i % 11 == 5) a = p.decide(SensorVector{});
        else if (i % 11 == 7) a = p.decide(oneFood(angles[i % 6], 0.0, 1.5));
        else a = p.decide(fav);
        if (i % 999 == 0) {
            CHECK(!std::isnan(a.dirX) && !std::isnan(a.dirY) && !std::isnan(a.boost));
        }
    }
    CHECK(std::isfinite(p.weightRule()) && std::isfinite(p.weightThermal()));
    CHECK(std::fabs(p.weightRule() + p.weightThermal() - 1.0) < 1e-9);
    CHECK(p.weightRule() > 0.0 && p.weightThermal() > 0.0);
    CHECK(p.weightRule() > 0.999); // 单边奖励 1 万帧 -> 深度分离但不塌零
}

// ---- B12. 无目标帧 -> 食物帧：历史含零输出不炸、下一帧正常。----
TEST(bandit_no_target_frame_then_food_no_nan) {
    BanditPolicy p;
    const ActionVector z = p.decide(SensorVector{});
    CHECK_NEAR(z.dirX, 0.0, 1e-12);
    const ActionVector a = p.decide(oneFood(30.0, 0.6, 1.0));
    CHECK(!std::isnan(a.dirX) && !std::isnan(a.dirY) && !std::isnan(a.boost));
    CHECK(std::fabs(std::sqrt(a.dirX * a.dirX + a.dirY * a.dirY) - 1.0) < 1e-9);
    const ActionVector b = p.decide(oneFood(30.0, 0.6, 1.0));
    CHECK(!std::isnan(b.dirX) && !std::isnan(b.dirY) && !std::isnan(b.boost));
    CHECK(std::fabs(p.weightRule() + p.weightThermal() - 1.0) < 1e-9);
}

// ---- B13. 策略名："bandit"（与 rule/thermal/stub/perceptron 互异）。----
TEST(bandit_name_is_bandit) {
    BanditPolicy p;
    CHECK(p.name() != nullptr);
    CHECK(std::strcmp(p.name(), "bandit") == 0);
}

// ---- B14. 默认构造 == 显式默认参数构造（逐位一致，20 帧混合序列）。----
TEST(bandit_default_ctor_equals_explicit_default_params) {
    BanditPolicy p1;
    BanditPolicy p2(0.05, ThermalPolicy::Params());
    const SensorVector fav = ruleFavoredScenario();
    for (int i = 0; i < 20; ++i) {
        ActionVector a1, a2;
        if (i % 5 == 2) {
            a1 = p1.decide(oneFood(50.0 * i, 0.4, 0.5));
            a2 = p2.decide(oneFood(50.0 * i, 0.4, 0.5));
        } else {
            a1 = p1.decide(fav);
            a2 = p2.decide(fav);
        }
        CHECK_NEAR(a1.dirX, a2.dirX, 0.0);
        CHECK_NEAR(a1.dirY, a2.dirY, 0.0);
        CHECK_NEAR(a1.boost, a2.boost, 0.0);
    }
    CHECK_NEAR(p1.weightRule(), p2.weightRule(), 0.0);
}

// ---- B15. 两臂意见相左时输出介于两臂方向之间（凸组合几何）。----
// 新鲜实例 0.5/0.5：uR=(1,0)、uT=(0,1) -> 输出 45°，与两臂点积均正且相等。
TEST(bandit_arms_disagree_output_between_arm_directions) {
    BanditPolicy p(0.0, ruleFavoredParams());
    const SensorVector s = ruleFavoredScenario();
    const ActionVector a = p.decide(s);
    const double dotR = a.dirX * 1.0 + a.dirY * 0.0;
    const double dotT = a.dirX * 0.0 + a.dirY * 1.0;
    CHECK(dotR > 0.0);
    CHECK(dotT > 0.0);
    CHECK_NEAR(dotR, dotT, 1e-9); // 等权 -> 与两臂等角
    CHECK(std::fabs(std::sqrt(a.dirX * a.dirX + a.dirY * a.dirY) - 1.0) < 1e-9);
}

// ---- B16. 极端 eta=10 + 万帧单边奖励：对数空间更新不溢出为 Inf/NaN（回归）。----
TEST(bandit_extreme_eta_log_overflow_safe) {
    BanditPolicy p(10.0, ruleFavoredParams());
    const SensorVector s = ruleFavoredScenario();
    for (int i = 0; i < 10000; ++i) (void)p.decide(s);
    CHECK(std::isfinite(p.weightRule()) && std::isfinite(p.weightThermal()));
    CHECK(!std::isnan(p.weightRule()) && !std::isnan(p.weightThermal()));
    CHECK(std::fabs(p.weightRule() + p.weightThermal() - 1.0) < 1e-9);
    CHECK(p.weightRule() > 0.0 && p.weightThermal() > 0.0);
    const ActionVector a = p.decide(s);
    CHECK(!std::isnan(a.dirX) && !std::isnan(a.dirY) && !std::isnan(a.boost));
}

// ---- B17. 零速度：对齐项失效但接近度仍主导，boost 混合可解析（0.5）。----
TEST(bandit_zero_speed_direction_and_boost) {
    BanditPolicy p(0.0);
    const ActionVector a = p.decide(oneFood(0.0, 0.8, 0.0));
    CHECK_NEAR(a.dirX, 1.0, 1e-9);  // 单候选 (1,0)，两臂同向
    CHECK_NEAR(a.dirY, 0.0, 1e-9);
    CHECK_NEAR(a.boost, 0.5, 1e-9); // 0.5*urgency(1) + 0.5*thermal(0)
}

// ---- B18. 学习后混合方向随权重连续转向：wR 从 0.5 升到 1 的过程中
// 输出与 uR 的点积单调不减（凸组合几何，等距采样 4 个学习深度）。----
TEST(bandit_output_rotates_toward_winner_monotonically) {
    const SensorVector s = ruleFavoredScenario();
    double prevDot = -1.0;
    for (int depth = 0; depth < 4; ++depth) {
        BanditPolicy p(0.5, ruleFavoredParams());
        for (int i = 0; i < depth * 3; ++i) (void)p.decide(s);
        const ActionVector a = p.decide(s);
        const double dotR = a.dirX; // uR = (1,0)
        CHECK(dotR >= prevDot - 1e-12); // 单调不减
        prevDot = dotR;
    }
    CHECK(prevDot > 0.9); // 终点充分靠近胜者臂
}

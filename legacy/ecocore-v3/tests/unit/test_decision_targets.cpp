// agentsim - tests/unit/test_decision_targets.cpp
// R2-C：TargetList / forceSlot / mod[4] 契约单测（V2-SPEC-AMEND-1 §2/§3）。
//
// 覆盖矩阵（8 策略 x 双参签名）：
//   A. 双参 decide(sv, targets) 不崩、输出有限（每策略一个综合"恶劣输入"用例）。
//   B. 有策略真的产出 forceSlot >= 0：
//      - foragebank_pulls_nearest_ball：觅食态 + kPullBoost -> slot=0 且矢量
//        沿 targets[0] 方向；蓄能态 -> 不施力（forceSlot==-1）。
//      - riskaverse_pushes_blocking_ball_in_earn：Earn 态 + 挡路球 -> slot 有效；
//        非挡路球 / Conserve 态 -> 不施力。
//   C. 有策略真的产出 mod 非零（abs > 0.01）：
//      - thermal_mod_tracks_comfort（mod[0]/mod[1]/mod[3]）
//      - hysteresis_mod_focus_tracks_proximity（mod[1]）
//      - bandit_explore_mod_peaks_at_uncertainty（mod[3]）
//      - riskaverse_mod_tracks_mode（mod[0]/mod[3]）
//   D. 保守契约（默认参数下 R2 通道静默，回归安全）：
//      - 全部 8 策略在默认构造 + 默认参数下 forceSlot==-1 且 mod 全 0
//       （Stub/Rule/Perceptron 三者连"开启开关"都没有，任何输入都不得产出）。
//   E. targets.count 的防御：count<0 / count>8 时输出仍有限、无越界读
//      （契约说 count ∈ [0,8]，但决策层不信任上游）。
// 红线：本文件不使用 RNG（手工构造全部输入），不 include environment/sim/ledger。
#include "test_framework.h"

#include <cmath>

#include "../../src/core/SensorTypes.h"
#include "../../src/decision/BanditPolicy.h"
#include "../../src/decision/ForageBankPolicy.h"
#include "../../src/decision/HysteresisPolicy.h"
#include "../../src/decision/IDecisionPolicy.h"
#include "../../src/decision/PerceptronPolicy.h"
#include "../../src/decision/RiskAversePolicy.h"
#include "../../src/decision/RulePolicy.h"
#include "../../src/decision/ThermalPolicy.h"

namespace {

using agentsim::ActionVector;
using agentsim::ForceTarget;
using agentsim::IDecisionPolicy;
using agentsim::PerceptronPolicy;
using agentsim::SensorVector;
using agentsim::TargetList;

// 有食物候选 0 + 一点航向的"活"传感器（多数策略会朝它走）。
SensorVector fedSensor() {
    SensorVector s{};
    s[0] = 1.0;  s[1] = 0.0;   // 候选 0 方向 (+1,0)
    s[6] = 0.7;                // 接近度
    s[9] = 0.3;                // 有目标
    s[10] = 0.3; s[11] = 0.0;  // 航向 (+1,0)（s[12] = 0.3/1.3）
    s[12] = 0.3 / 1.3;
    s[13] = 0.9;               // 离边远
    s[16] = 0.0;               // 无失真
    s[17] = 0.5;               // 最适温度
    return s;
}

// 单目标 TargetList：球在 (dx,dy)、距离 dist。
TargetList oneTarget(double dx, double dy, double dist) {
    TargetList t;
    t.count = 1;
    t.items[0].id = 77;
    t.items[0].dx = dx;
    t.items[0].dy = dy;
    t.items[0].dist = dist;
    t.items[0].type = 0;
    return t;
}

bool finiteAction(const ActionVector& a) {
    return std::isfinite(a.dirX) && std::isfinite(a.dirY) && std::isfinite(a.boost) &&
           std::isfinite(a.forceX) && std::isfinite(a.forceY);
}

bool modAllZero(const ActionVector& a) {
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(a.mod[i]) > 1e-12) return false;
    }
    return true;
}

double maxAbsMod(const ActionVector& a) {
    double m = 0.0;
    for (int i = 0; i < 4; ++i) {
        const double v = std::fabs(a.mod[i]);
        if (v > m) m = v;
    }
    return m;
}

} // namespace

// ============ A. 双参签名：每策略不崩、输出有限 ============
TEST(targets_all_policies_survive_two_arg_decide) {
    agentsim::RulePolicy rule;
    agentsim::StubPolicy stub;
    agentsim::PerceptronPolicy perc(12345, 0.1);
    agentsim::ThermalPolicy thermal;
    agentsim::HysteresisPolicy hyst;
    agentsim::ForageBankPolicy forage;
    agentsim::BanditPolicy bandit;
    agentsim::RiskAversePolicy risk;

    const SensorVector fed = fedSensor();
    const SensorVector empty{};
    const TargetList t1 = oneTarget(1.0, 0.0, 10.0);
    const TargetList tNone{};

    // 非多态调用（静态类型直调，覆盖 override 正确性与编译期签名）。
    const ActionVector a1 = rule.decide(fed, t1);
    const ActionVector a2 = stub.decide(fed, t1);
    const ActionVector a3 = perc.decide(fed, t1);
    const ActionVector a4 = thermal.decide(fed, t1);
    const ActionVector a5 = hyst.decide(fed, t1);
    const ActionVector a6 = forage.decide(fed, t1);
    const ActionVector a7 = bandit.decide(fed, t1);
    const ActionVector a8 = risk.decide(fed, t1);
    CHECK(finiteAction(a1) && finiteAction(a2) && finiteAction(a3) && finiteAction(a4));
    CHECK(finiteAction(a5) && finiteAction(a6) && finiteAction(a7) && finiteAction(a8));

    // 空目标表 + 空传感器也不崩。
    CHECK(finiteAction(rule.decide(empty, tNone)));
    CHECK(finiteAction(stub.decide(empty, tNone)));
    CHECK(finiteAction(perc.decide(empty, tNone)));
    CHECK(finiteAction(thermal.decide(empty, tNone)));
    CHECK(finiteAction(hyst.decide(empty, tNone)));
    CHECK(finiteAction(forage.decide(empty, tNone)));
    CHECK(finiteAction(bandit.decide(empty, tNone)));
    CHECK(finiteAction(risk.decide(empty, tNone)));

    // 多态调用面：基类指针走双参（组合根的调用形态）。
    IDecisionPolicy* pols[] = {&rule, &stub, &perc, &thermal, &hyst, &forage, &bandit, &risk};
    for (IDecisionPolicy* p : pols) {
        const ActionVector a = p->decide(fed, t1);
        CHECK(finiteAction(a));
        CHECK(p->name() != nullptr);
    }
}

// ============ B1. ForageBank：觅食态拉球 / 蓄能态不施力 ============
TEST(targets_foragebank_pulls_nearest_ball) {
    agentsim::ForageBankPolicy::Params p;
    p.kPullBoost = 2.0; // 开启施力
    agentsim::ForageBankPolicy pol(p);

    // 觅食态（能量代理低：s[13]=0.1, 静止 -> e = 0.8*0.1+0.2*1 = 0.28 < 0.3）。
    SensorVector s = fedSensor();
    s[13] = 0.1;
    s[10] = 0.0; s[11] = 0.0; s[12] = 0.0;
    const TargetList t = oneTarget(3.0, -4.0, 5.0);
    const ActionVector a = pol.decide(s, t);
    CHECK(a.forceSlot == 0);                        // targets 下标 0（最近球）
    CHECK_NEAR(a.forceX, 0.6 * 2.0, 1e-12);         // (3,-4)/5 = (0.6,-0.8)，x kPullBoost
    CHECK_NEAR(a.forceY, -0.8 * 2.0, 1e-12);

    // 蓄能态（代理高：s[13]=1.0，有速度 -> e = 1.0 > 0.7）：不施力。
    SensorVector bank = fedSensor();
    bank[13] = 1.0;
    bank[10] = 5.0; bank[11] = 0.0; bank[12] = 5.0 / 6.0;
    const ActionVector b = pol.decide(bank, t);
    CHECK(b.forceSlot == -1);
    CHECK_NEAR(b.forceX, 0.0, 0.0);
    CHECK_NEAR(b.forceY, 0.0, 0.0);
    // 蓄能态视野调制：-kModSight 默认 0 -> mod 全 0（保守契约由 D 组测试钉死）。

    // 无目标（count==0）：不施力，其余输出正常。
    const ActionVector c = pol.decide(s, TargetList{});
    CHECK(c.forceSlot == -1);
    CHECK(finiteAction(c));
}

// ============ B2. RiskAverse：Earn 态推开挡路球 ============
TEST(targets_riskaverse_pushes_blocking_ball_in_earn) {
    agentsim::RiskAversePolicy::Params p;
    p.kPushBoost = 3.0; // 开启施力
    agentsim::RiskAversePolicy pol(p);

    // 制造 Earn：接近度持续上升（obs ≈ 1.39，EMA ~12 帧后 > 1.3）。
    const SensorVector s = fedSensor();
    ActionVector a{};
    for (int k = 1; k <= 30; ++k) {
        SensorVector f = s;
        f[6] = 0.05 + 0.01 * static_cast<double>(k);
        f[9] = 1.0 - f[6];
        a = pol.decide(f, oneTarget(1.0, 0.0, 4.0));
    }
    CHECK(pol.mode() == 2);              // Earn
    CHECK(a.forceSlot == 0);             // 球在 (+1,0) = 期望方向 -> 挡路
    CHECK(a.forceX > 0.0);               // 推力主要沿 +x
    const double flen = std::sqrt(a.forceX * a.forceX + a.forceY * a.forceY);
    CHECK_NEAR(flen, 3.0, 1e-12);        // |a| = kPushBoost（单位向量合成后归一）

    // 球在侧后方（(-1,0)），不挡路 -> 不施力。
    agentsim::RiskAversePolicy pol2(p);
    for (int k = 1; k <= 30; ++k) {
        SensorVector f = s;
        f[6] = 0.05 + 0.01 * static_cast<double>(k);
        f[9] = 1.0 - f[6];
        a = pol2.decide(f, oneTarget(-1.0, 0.0, 4.0));
    }
    CHECK(pol2.mode() == 2);
    CHECK(a.forceSlot == -1);

    // EROI 崩塌后（Conserve）不施力：EROI 折扣到位后模式必然退出 Earn。
    agentsim::RiskAversePolicy pol3(p);
    for (int k = 1; k <= 30; ++k) {
        SensorVector f = s;
        f[6] = 0.05 + 0.01 * static_cast<double>(k);
        f[9] = 1.0 - f[6];
        (void)pol3.decide(f, oneTarget(1.0, 0.0, 4.0));
    }
    CHECK(pol3.mode() == 2);
    for (int k = 1; k <= 120; ++k) {
        a = pol3.decide(s, oneTarget(1.0, 0.0, 4.0)); // 无增益 -> EMA 衰减
    }
    CHECK(pol3.mode() == 0);             // Conserve
    CHECK(a.forceSlot == -1);
}

// ============ C1. Thermal：mod 跟随舒适度 ============
TEST(thermal_mod_tracks_comfort) {
    agentsim::ThermalPolicy::Params p;
    p.kModSight = 0.5;
    p.kModSpace = 0.5;
    p.kModCohesion = 0.5;
    agentsim::ThermalPolicy pol(p);

    // 温和无失真：comfort = 1*(1-0) - 0 = 1 -> mod 正向。
    SensorVector warm = fedSensor();
    const ActionVector a = pol.decide(warm, TargetList{});
    CHECK_NEAR(a.mod[0], 0.5, 1e-12);
    CHECK_NEAR(a.mod[1], 0.5, 1e-12);
    CHECK_NEAR(a.mod[3], 0.5, 1e-12);
    CHECK(maxAbsMod(a) > 0.01);

    // 全失真：comfort = 1*(1-0.5) - 0.5 = 0 -> mod 全 0。
    warm[16] = 1.0;
    const ActionVector b = pol.decide(warm, TargetList{});
    CHECK_NEAR(b.mod[0], 0.0, 1e-12);
    CHECK_NEAR(b.mod[1], 0.0, 1e-12);
    CHECK_NEAR(b.mod[3], 0.0, 1e-12);

    // 极端温度 + 半失真：comfort = 0*(1-0.25) - 0.25 = -0.25 -> mod 反向。
    SensorVector cold = fedSensor();
    cold[17] = 0.0;  // suitability = 0
    cold[16] = 0.5;
    const ActionVector c = pol.decide(cold, TargetList{});
    CHECK_NEAR(c.mod[0], -0.125, 1e-12);
    CHECK_NEAR(c.mod[1], -0.125, 1e-12);
    CHECK_NEAR(c.mod[3], -0.125, 1e-12);

    // 无目标帧也输出调制（舒适度与目标无关）。
    SensorVector noT{};
    noT[17] = 0.5; noT[16] = 0.0;
    const ActionVector d = pol.decide(noT, TargetList{});
    CHECK_NEAR(d.mod[0], 0.5, 1e-12);

    // 默认参数（强度全 0）：mod 恒 0。
    agentsim::ThermalPolicy def;
    const ActionVector e = def.decide(fedSensor(), oneTarget(1.0, 0.0, 5.0));
    CHECK(modAllZero(e));
    CHECK(e.forceSlot == -1); // 不施力策略
}

// ============ C2. Hysteresis：mod[1] 聚焦跟随目标接近度 ============
TEST(hysteresis_mod_focus_tracks_proximity) {
    agentsim::HysteresisPolicy::Params p;
    p.kModFocus = 0.8;
    agentsim::HysteresisPolicy pol(p);

    // 近目标（prox 0.9）：mod[1] = 0.8*(2*0.9-1) = 0.64。
    SensorVector near{};
    near[0] = 1.0; near[1] = 0.0; near[6] = 0.9; near[9] = 0.1;
    const ActionVector a = pol.decide(near, TargetList{});
    CHECK_NEAR(a.mod[1], 0.8 * 0.8, 1e-12);
    CHECK(maxAbsMod(a) > 0.01);

    // 远目标（prox 0.1）：mod[1] = 0.8*(2*0.1-1) = -0.64。
    SensorVector far = near;
    far[6] = 0.1; far[9] = 0.9;
    const ActionVector b = pol.decide(far, TargetList{});
    CHECK_NEAR(b.mod[1], -0.64, 1e-12);

    // 无目标：mod[1] = -0.8（放宽空间）。
    const ActionVector c = pol.decide(SensorVector{}, TargetList{});
    CHECK_NEAR(c.mod[1], -0.8, 1e-12);

    // 默认参数：mod 恒 0。
    agentsim::HysteresisPolicy def;
    const ActionVector d = def.decide(near, oneTarget(1.0, 0.0, 5.0));
    CHECK(modAllZero(d));
    CHECK(d.forceSlot == -1);
}

// ============ C3. Bandit：mod[3] 探索调制在不确定度峰值 ============
// 注（对称不变量，test_bandit B6）：两臂方向恒相同时回报恒相等 -> 权重恒 0.5/0.5。
// 因此"权重尖锐化"必须用**两臂分歧**的非对称场景（与 test_bandit 的
// ruleFavoredScenario 同构：c0 近而逆航向、c1 远而顺航向 + wThermal=3.0
// 把 thermal 臂引向远食物），单候选 fedSensor 场景永远无法让权重分离。
TEST(bandit_explore_mod_peaks_at_uncertainty) {
    // 两臂分歧场景：R 选近 c0 (+1,0)，T 选远 c1 (0,1)（解析见 test_bandit.cpp 注释）。
    SensorVector asym{};
    asym[0] = 1.0;  asym[1] = 0.0;   // c0 近（prox 1.0）
    asym[2] = 0.0;  asym[3] = 1.0;   // c1 远（prox 0.4）
    asym[6] = 1.0;
    asym[7] = 0.4;
    asym[9] = 0.1;                  // 有目标
    asym[10] = 0.14; asym[11] = 0.48; // v̂ = (0.28,0.96)
    asym[12] = 0.5;
    asym[16] = 0.0;
    asym[17] = 0.5;

    agentsim::BanditPolicy p1(0.05, {}, 0.9); // kModExplore 开启
    // 首帧权重 0.5/0.5 -> uncertainty = 4*0.5*0.5 = 1 -> mod[3] = 0.9。
    const ActionVector a = p1.decide(asym, oneTarget(1.0, 0.0, 5.0));
    CHECK_NEAR(a.mod[3], 0.9, 1e-12);
    CHECK(maxAbsMod(a) > 0.01);
    CHECK(a.forceSlot == -1); // bandit 不施力（设计决定）

    // 持续奖励 rule 臂（eta=0.5，每帧 rR-rT >= 0.328）：60 帧后对数权重差 > 19
    // -> wT ~ e^-19 -> uncertainty = 4*wR*wT ~ 1e-8 -> mod[3] -> 0。
    agentsim::ThermalPolicy::Params tp;
    tp.wThermal = 3.0;
    agentsim::BanditPolicy p2(0.5, tp, 0.9);
    for (int i = 0; i < 60; ++i) (void)p2.decide(asym, oneTarget(1.0, 0.0, 5.0));
    CHECK(p2.weightRule() > 0.9);            // 权重确实尖锐化了
    const ActionVector b = p2.decide(asym, oneTarget(1.0, 0.0, 5.0));
    CHECK(std::fabs(b.mod[3]) < 0.9 * 0.01); // uncertainty < 0.01

    // 默认参数：mod 恒 0。
    agentsim::BanditPolicy def;
    const ActionVector c = def.decide(asym, oneTarget(1.0, 0.0, 5.0));
    CHECK(modAllZero(c));
}

// ============ C4. RiskAverse：mod 跟随模式 ============
TEST(riskaverse_mod_tracks_mode) {
    agentsim::RiskAversePolicy::Params p;
    p.kModCohesion = 0.6;
    p.kModSight = 0.4;
    agentsim::RiskAversePolicy pol(p);

    // Earn：+聚拢 +视野。
    const SensorVector s = fedSensor();
    ActionVector a{};
    for (int k = 1; k <= 30; ++k) {
        SensorVector f = s;
        f[6] = 0.05 + 0.01 * static_cast<double>(k);
        f[9] = 1.0 - f[6];
        a = pol.decide(f, TargetList{});
    }
    CHECK(pol.mode() == 2);
    CHECK_NEAR(a.mod[3], 0.6, 1e-12);
    CHECK_NEAR(a.mod[0], 0.4, 1e-12);
    CHECK(maxAbsMod(a) > 0.01);

    // Conserve：-聚拢 -视野。
    for (int k = 1; k <= 120; ++k) {
        a = pol.decide(s, TargetList{});
    }
    CHECK(pol.mode() == 0);
    CHECK_NEAR(a.mod[3], -0.6, 1e-12);
    CHECK_NEAR(a.mod[0], -0.4, 1e-12);
}

// ============ D. 保守契约：默认构造下 R2 通道静默 ============
TEST(targets_default_params_keep_r2_channels_silent) {
    agentsim::RulePolicy rule;
    agentsim::StubPolicy stub;
    agentsim::PerceptronPolicy perc(2026, 0.1);
    agentsim::ThermalPolicy thermal;
    agentsim::HysteresisPolicy hyst;
    agentsim::ForageBankPolicy forage;
    agentsim::BanditPolicy bandit;
    agentsim::RiskAversePolicy risk;

    const SensorVector fed = fedSensor();
    const TargetList t = oneTarget(1.0, 0.0, 5.0);
    IDecisionPolicy* pols[] = {&rule, &stub, &perc, &thermal, &hyst, &forage, &bandit, &risk};
    for (IDecisionPolicy* p : pols) {
        const ActionVector a = p->decide(fed, t);
        CHECK(a.forceSlot == -1);       // 默认不点名施力
        CHECK_NEAR(a.forceX, 0.0, 0.0);
        CHECK_NEAR(a.forceY, 0.0, 0.0);
        CHECK(modAllZero(a));           // 默认不调制
        CHECK(finiteAction(a));
    }
    // Rule/Stub/Perceptron 连"开启开关"都不存在：喂满 R2 感知也不得产出。
    SensorVector social = fedSensor();
    social[18] = 0.2; social[19] = 1.0; social[20] = 0.0; // 贴脸邻居
    social[21] = 1.0;                                       // 能量满
    social[22] = 1.0; social[23] = 0.1;                     // 球贴脸
    const TargetList many{};
    CHECK(modAllZero(rule.decide(social, many)));
    CHECK(rule.decide(social, many).forceSlot == -1);
    CHECK(modAllZero(stub.decide(social, many)));
    CHECK(modAllZero(perc.decide(social, many)));
}

// ============ E. targets.count 防御：契约外的 count 也不越界、不 NaN ============
TEST(targets_defends_against_out_of_contract_count) {
    agentsim::ForageBankPolicy::Params fp;
    fp.kPullBoost = 1.0;
    agentsim::ForageBankPolicy forage(fp);
    agentsim::RiskAversePolicy::Params rp;
    rp.kPushBoost = 1.0;
    agentsim::RiskAversePolicy risk(rp);
    agentsim::BanditPolicy bandit;
    agentsim::ThermalPolicy thermal;
    agentsim::HysteresisPolicy hyst;
    agentsim::PerceptronPolicy perc(7, 0.1);
    agentsim::RulePolicy rule;
    agentsim::StubPolicy stub;

    const SensorVector fed = fedSensor();
    const SensorVector hungry = []() {
        SensorVector s = fedSensor();
        s[13] = 0.1; s[10] = 0.0; s[11] = 0.0; s[12] = 0.0;
        return s;
    }();

    // count 超上界：决策层只读 [0..count) 时会越界 -> 我们的实现只在
    // count > 0 时读 items[0..count)（forage 只读 [0]、riskaverse 线性扫
    // [0..count)）。构造 count=8 的满表 + 填充垃圾值，断言输出有限。
    TargetList full{};
    full.count = 8;
    for (int i = 0; i < 8; ++i) {
        full.items[i].id = static_cast<unsigned long long>(i + 1);
        full.items[i].dx = 0.0;
        full.items[i].dy = 0.0;
        full.items[i].dist = 3.0 + static_cast<double>(i);
        full.items[i].type = i % 3;
    }
    // 零方向球（dx=dy=0）：ForageBank 的施力分支必须跳过（长度 < 1e-9 防御），
    // 不得产出 NaN forceX/forceY。
    const ActionVector a = forage.decide(hungry, full);
    CHECK(finiteAction(a));
    // items[0] 方向为零 -> 不施力（防御生效）。
    CHECK(a.forceSlot == -1);

    // count = 0 但 items[0] 有垃圾：恒不读 -> 无影响。
    TargetList zeroCount{};
    zeroCount.count = 0;
    zeroCount.items[0].dist = 1e9;
    zeroCount.items[0].dx = std::sqrt(-1.0);
    CHECK(finiteAction(forage.decide(hungry, zeroCount)));
    CHECK(finiteAction(risk.decide(fed, zeroCount)));
    CHECK(finiteAction(bandit.decide(fed, zeroCount)));
    CHECK(finiteAction(thermal.decide(fed, zeroCount)));
    CHECK(finiteAction(hyst.decide(fed, zeroCount)));
    CHECK(finiteAction(perc.decide(fed, zeroCount)));
    CHECK(finiteAction(rule.decide(fed, zeroCount)));
    CHECK(finiteAction(stub.decide(fed, zeroCount)));

    // 负 count（契约外）：按 0 处理。
    TargetList neg{};
    neg.count = -3;
    neg.items[0].dx = 1.0; neg.items[0].dy = 0.0; neg.items[0].dist = 1.0;
    const ActionVector b = forage.decide(hungry, neg);
    CHECK(b.forceSlot == -1);
    CHECK(finiteAction(b));
}

// ============ F. 感知层新维喂给决策：端到端语义（digest 链路） ============
TEST(targets_policies_digest_new_sensor_dims) {
    // Perceptron 全额消费 s[18..23]：扩展维权重非零时，改变扩展维必须改变输出。
    PerceptronPolicy::Params p;
    for (int j = 0; j < SensorVector::kDim; ++j) {
        p.w[0][j] = 0.02 * (j % 7) - 0.06;
        p.w[1][j] = 0.03 * ((j + 3) % 5) - 0.06;
        if (j >= 18) { p.w[0][j] = 0.3; p.w[1][j] = -0.2; } // 扩展维权重放大
    }
    PerceptronPolicy pol(p);
    SensorVector base = fedSensor();
    SensorVector social = base;
    social[18] = 1.0; social[19] = 0.0; social[20] = 0.0; // 无邻居
    // 对照：有贴脸邻居 vs 无邻居，输出必须不同（扩展维真的进了线性组合）。
    SensorVector crowded = base;
    crowded[18] = 0.1; crowded[19] = 1.0; crowded[20] = 0.0; crowded[21] = 0.9;
    const ActionVector a1 = pol.decide(crowded, TargetList{});
    const ActionVector a2 = pol.decide(social, TargetList{});
    CHECK(a1.dirX != a2.dirX || a1.dirY != a2.dirY || a1.boost != a2.boost);

    // 全 24 维有限输入 -> 全部策略输出有限（digest 不产生 NaN）。
    agentsim::RulePolicy rule;
    agentsim::ThermalPolicy thermal;
    agentsim::HysteresisPolicy hyst;
    agentsim::ForageBankPolicy forage;
    agentsim::BanditPolicy bandit;
    agentsim::RiskAversePolicy risk;
    agentsim::StubPolicy stub;
    const TargetList t = oneTarget(1.0, 0.0, 5.0);
    CHECK(finiteAction(rule.decide(crowded, t)));
    CHECK(finiteAction(thermal.decide(crowded, t)));
    CHECK(finiteAction(hyst.decide(crowded, t)));
    CHECK(finiteAction(forage.decide(crowded, t)));
    CHECK(finiteAction(bandit.decide(crowded, t)));
    CHECK(finiteAction(risk.decide(crowded, t)));
    CHECK(finiteAction(stub.decide(crowded, t)));
}

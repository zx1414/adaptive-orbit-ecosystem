// agentsim - tests/unit/test_decision_memory.cpp
// P0-4 验收 A4（让记忆归个体）的决策层单测。
//
// 被测语义：跨帧学习/滞回状态住在 PolicyMemory 里，由**调用方（每个核）**持有；
// 策略实例只保留构造期标定的不可变参数。因此同一个 policy 实例服务 N 个核时，
// 各核的经历互不串味；新生核拿到默认构造的 memory（不继承任何人的经历）。
//
// 覆盖：
//   · decision_memory_two_agents_isolated —— A4 前半：两个体收到会触发不同状态的
//     局部输入序列（A 低能量连续帧 / B 高能量连续帧），**交错**喂给同一个策略实例；
//     各自跑完后，把 A 的序列单独跑在全新 memory 上，最终动作与内部状态必须与
//     交错跑时完全一致；B 对称再做一次。同时断言两个体的最终状态确实不同
//     （否则用例空转）。覆盖 4 个有记忆的策略：ForageBank / Hysteresis /
//     RiskAverse / Bandit。
//   · decision_memory_newborn_starts_neutral —— A4 后半的决策层一侧：默认构造的
//     memory 等于各策略 reset() 后的初态 = 新个体起点（"不继承亲代最近一次经历"）。
//   · decision_memory_is_pod_and_deterministic —— PolicyMemory 必须 trivially
//     copyable、可 memcpy 往返；同输入 + 同初值 memory 两次运行逐位相等。
// 红线：本文件不使用 RNG（全部手工构造确定性输入序列），不 include
// environment/sim/ledger。
#include "test_framework.h"

#include <cmath>
#include <cstring>
#include <type_traits>
#include <vector>

#include "../../src/core/SensorTypes.h"
#include "../../src/decision/BanditPolicy.h"
#include "../../src/decision/ForageBankPolicy.h"
#include "../../src/decision/HysteresisPolicy.h"
#include "../../src/decision/IDecisionPolicy.h" // 基类指针调用面（组合根形态）
#include "../../src/decision/PerceptronPolicy.h" // 基类指针调用面里的第 3 个策略
#include "../../src/decision/PolicyMemory.h"
#include "../../src/decision/RiskAversePolicy.h"
#include "../../src/decision/RulePolicy.h" // RulePolicy / StubPolicy
#include "../../src/decision/ThermalPolicy.h" // BanditPolicy::Params 里的臂参数

namespace {

using agentsim::ActionVector;
using agentsim::PolicyMemory;
using agentsim::SensorVector;
using agentsim::TargetList;
using Seq = std::vector<SensorVector>;

// ---------------------------------------------------------------------------
// 比对工具：动作逐位相等 + 记忆逐字段相等。
// 记忆比较刻意**逐字段**写，而不是 memcmp：PolicyMemory 是 POD，padding 字节不
// 保证被初始化，memcmp 会给出假阴性。同时这份清单也是"记忆里到底存了什么"的
// 可读清单 —— 新增字段时必须同步（编译期不会提醒，但测试会失败，属于有意为之）。
// ---------------------------------------------------------------------------
bool actionsBitEqual(const ActionVector& a, const ActionVector& b) {
    if (a.dirX != b.dirX || a.dirY != b.dirY || a.boost != b.boost) return false;
    if (a.forceSlot != b.forceSlot || a.forceX != b.forceX || a.forceY != b.forceY) return false;
    for (int i = 0; i < 4; ++i) {
        if (a.mod[i] != b.mod[i]) return false;
    }
    return true;
}

bool memoriesFieldEqual(const PolicyMemory& a, const PolicyMemory& b) {
    if (a.schemaVersion != b.schemaVersion) return false;
    // ---- ForageBank ----
    if (a.forageForaging != b.forageForaging) return false;
    // ---- Hysteresis ----
    if (a.hysteresisLastChoice != b.hysteresisLastChoice) return false;
    // ---- RiskAverse ----
    if (a.riskEroi != b.riskEroi) return false;
    if (a.riskPrevProx != b.riskPrevProx) return false;
    for (int i = 0; i < 2; ++i) {
        if (a.riskLastDir[i] != b.riskLastDir[i]) return false;
        if (a.riskCachedDir[i] != b.riskCachedDir[i]) return false;
    }
    if (a.riskHasCachedDir != b.riskHasCachedDir) return false;
    if (a.riskMode != b.riskMode) return false;
    if (a.riskFrameInMode != b.riskFrameInMode) return false;
    if (a.riskFrameInModeMode != b.riskFrameInModeMode) return false;
    // ---- Bandit ----
    for (int arm = 0; arm < 2; ++arm) {
        if (a.banditLogW[arm] != b.banditLogW[arm]) return false;
        if (a.banditW[arm] != b.banditW[arm]) return false;
        for (int k = 0; k < 2; ++k) {
            if (a.banditPrevArm[arm][k] != b.banditPrevArm[arm][k]) return false;
        }
        if (a.banditPrevOut[arm] != b.banditPrevOut[arm]) return false;
    }
    if (a.banditHasPrev != b.banditHasPrev) return false;
    for (int i = 0; i < SensorVector::kDim; ++i) {
        if (a.banditPrevS[i] != b.banditPrevS[i]) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// A4 前半：交错喂两个体 vs 单独跑，结果必须逐位一致。
// shared 是**同一个**策略实例（生产形态：Simulation 只持有一个 policy_）。
// ---------------------------------------------------------------------------
template <typename Policy>
void checkTwoAgentIsolation(Policy& shared, const Seq& seqA, const Seq& seqB) {
    CHECK(seqA.size() == seqB.size());

    // (1) 交错：A/B 交替同一实例，各自只动自己的 memory。
    PolicyMemory memA;
    PolicyMemory memB;
    ActionVector lastA{};
    ActionVector lastB{};
    for (std::size_t i = 0; i < seqA.size(); ++i) {
        lastA = shared.decide(seqA[i], TargetList{}, memA);
        lastB = shared.decide(seqB[i], TargetList{}, memB);
    }
    // 两个体的最终状态必须真的不同，否则"隔离"无从验证（空转保护）。
    CHECK(!memoriesFieldEqual(memA, memB));

    // (2) A 的序列单独跑在全新 memory 上（同一个实例）。
    PolicyMemory memASolo;
    ActionVector soloA{};
    for (const SensorVector& s : seqA) soloA = shared.decide(s, TargetList{}, memASolo);
    CHECK(actionsBitEqual(lastA, soloA));      // 最终动作完全一致
    CHECK(memoriesFieldEqual(memA, memASolo)); // 内部状态完全一致

    // (3) B 对称再做一次。
    PolicyMemory memBSolo;
    ActionVector soloB{};
    for (const SensorVector& s : seqB) soloB = shared.decide(s, TargetList{}, memBSolo);
    CHECK(actionsBitEqual(lastB, soloB));
    CHECK(memoriesFieldEqual(memB, memBSolo));

    // (4) 全新 memory 上的单独跑也不该把 A/B 两种结局混起来。
    CHECK(!memoriesFieldEqual(memASolo, memBSolo));
}

// ---------------------------------------------------------------------------
// 输入序列构造（全部手工、确定性、无 RNG）
// ---------------------------------------------------------------------------

// ForageBank：能量代理只看 s[13]（处境余裕）与 s[12]（节流余裕）。
//   低能量帧：s[13]=0.1、静止 -> e = 0.8*0.1 + 0.2*1 = 0.28 < 0.3 -> 觅食态。
//   高能量帧：s[13]=1.0、速度 5 -> e = 1.0 > 0.7 -> 蓄能态（且速度非零，不触发自举）。
SensorVector forageLowEnergy() {
    SensorVector s{};
    s[0] = 1.0; s[1] = 0.0; s[6] = 0.7; s[9] = 0.3;
    s[13] = 0.1;
    return s;
}
SensorVector forageHighEnergy() {
    SensorVector s{};
    s[0] = 1.0; s[1] = 0.0; s[6] = 0.7; s[9] = 0.3;
    s[10] = 5.0; // 速度沿 +x，模长 5
    s[12] = 5.0 / 6.0;
    s[13] = 1.0;
    return s;
}

// Hysteresis：两个候选，A 序列让"现任"落在槽 1，B 序列落在槽 0。
SensorVector twoCands(double proxA, double proxB) {
    SensorVector s{};
    s[0] = 1.0; s[1] = 0.0; s[2] = 0.0; s[3] = 1.0;
    s[6] = proxA;
    s[7] = proxB;
    s[9] = 0.1;
    s[13] = 1.0;
    return s;
}

// RiskAverse：A 侧接近度持续上升（EROI 冲高 -> Earn），B 侧接近度停滞（-> Conserve）。
SensorVector riskFrame(double prox, double speed) {
    SensorVector s{};
    s[0] = 1.0; s[1] = 0.0; s[6] = prox; s[9] = 1.0 - prox;
    s[10] = speed; s[12] = speed / (1.0 + speed);
    s[16] = 0.0;
    s[17] = 0.5;
    return s;
}

// Bandit：两个可解析场景（解析见 test_bandit.cpp 的同名场景注释）。
//   ruleFavored：c0=(1,0) 近而逆航向、c1=(0,1) 远而顺航向，wThermal=3 让 thermal
//     臂被引向远食物 -> 势能项持续奖励 rule 臂。
//   agreement：单候选、两臂必然同向 -> 对称不变量成立，权重恒 0.5/0.5（零漂移）。
//     用它当对照个体：A 在学（权重离开 0.5）而 B 该一动不动 —— 一旦 A 的经历
//     泄漏进 B 的记忆，B 的权重就会偏离 0.5，断言立刻失败。
SensorVector banditAgreement() {
    SensorVector s{};
    s[0] = 1.0; s[1] = 0.0;
    s[6] = 0.8;
    s[9] = 0.2;
    s[10] = 0.2; s[11] = 0.0;
    s[12] = 0.2 / 1.2;
    s[16] = 0.0;
    s[17] = 0.5;
    return s;
}
SensorVector banditRuleFavored() {
    SensorVector s{};
    s[0] = 1.0; s[1] = 0.0;
    s[2] = 0.0; s[3] = 1.0;
    s[6] = 1.0;
    s[7] = 0.4;
    s[9] = 0.1;
    s[10] = 0.14; s[11] = 0.48;
    s[12] = 0.5;
    s[16] = 0.0;
    s[17] = 0.5;
    return s;
}

} // namespace

// ===========================================================================
// 组合根调用形态：基类指针 + 三参 decide（Simulation 接线后的真实调用面）
// ===========================================================================
TEST(decision_memory_base_pointer_three_arg_is_callable) {
    agentsim::RulePolicy rule;
    agentsim::StubPolicy stub;
    agentsim::PerceptronPolicy perc(12345, 0.1);
    agentsim::ThermalPolicy thermal;
    agentsim::HysteresisPolicy hyst;
    agentsim::ForageBankPolicy forage;
    agentsim::BanditPolicy bandit;
    agentsim::RiskAversePolicy risk;
    agentsim::IDecisionPolicy* pols[8] = {&rule,  &stub,   &perc,  &thermal,
                                         &hyst,  &forage, &bandit, &risk};
    SensorVector s = forageLowEnergy();
    s[16] = 0.2;
    s[17] = 0.5;
    const TargetList t{};
    // 每个核各持一份记忆；同一个实例轮流服务它们（生产形态）。
    PolicyMemory memA;
    PolicyMemory memB;
    for (agentsim::IDecisionPolicy* p : pols) {
        CHECK(p->name() != nullptr);
        const ActionVector a = p->decide(s, t, memA);
        const ActionVector b = p->decide(s, t, memB);
        CHECK(actionsBitEqual(a, b)); // 两个新个体同输入 -> 同输出
        CHECK(std::isfinite(a.dirX) && std::isfinite(a.dirY) && std::isfinite(a.boost));
    }
}

// ===========================================================================
// 记忆分块：四种策略共用同一份 PolicyMemory 时，各自的块互不踩踏
// ===========================================================================
TEST(decision_memory_blocks_do_not_collide) {
    PolicyMemory shared{}; // 极端情形：一个个体一份记忆，被四种策略依次使用
    agentsim::ForageBankPolicy forage;
    agentsim::HysteresisPolicy hyst;
    agentsim::RiskAversePolicy risk;
    agentsim::ThermalPolicy::Params tp;
    tp.wThermal = 3.0;
    agentsim::BanditPolicy bandit(0.5, tp);
    const TargetList t{};

    for (int i = 0; i < 40; ++i) {
        (void)forage.decide(forageLowEnergy(), t, shared);
        (void)hyst.decide(twoCands(0.50, 0.54), t, shared);
        (void)risk.decide(riskFrame(0.05 + 0.01 * static_cast<double>(i + 1), 1.5), t, shared);
        (void)bandit.decide(banditRuleFavored(), t, shared);
    }
    CHECK(agentsim::ForageBankPolicy::foraging(shared));
    CHECK(agentsim::HysteresisPolicy::lastChoice(shared) == 1);
    CHECK(agentsim::RiskAversePolicy::mode(shared) == 2);
    CHECK(agentsim::BanditPolicy::weightRule(shared) > 0.9);

    // 单独 reset 某一策略只清自己那块，其它块原样保留。
    agentsim::ForageBankPolicy::reset(shared);
    CHECK(!agentsim::ForageBankPolicy::foraging(shared));
    CHECK(agentsim::HysteresisPolicy::lastChoice(shared) == 1);
    CHECK(agentsim::RiskAversePolicy::mode(shared) == 2);
    CHECK(agentsim::BanditPolicy::weightRule(shared) > 0.9);
    // 全清（新生核/新局）才回到默认。
    agentsim::clearPolicyMemory(shared);
    CHECK(agentsim::HysteresisPolicy::lastChoice(shared) == -1);
    CHECK(agentsim::RiskAversePolicy::mode(shared) == 1);
    CHECK_NEAR(agentsim::BanditPolicy::weightRule(shared), 0.5, 1e-12);
}

// ===========================================================================
// A4 前半：两个体的局部经历互不改写（四个有记忆的策略各跑一轮）
// ===========================================================================
TEST(decision_memory_two_agents_isolated) {
    const int kFrames = 60;

    // ---- ForageBank：A 连续低能量（觅食），B 连续高能量（蓄能）----
    {
        agentsim::ForageBankPolicy shared; // 生产形态：一个实例服务多个核
        Seq a;
        Seq b;
        for (int i = 0; i < kFrames; ++i) {
            a.push_back(forageLowEnergy());
            b.push_back(forageHighEnergy());
        }
        checkTwoAgentIsolation(shared, a, b);
        // 直接钉住"两种状态确实不同"：A = 觅食态，B = 蓄能态。
        PolicyMemory memA;
        PolicyMemory memB;
        for (int i = 0; i < kFrames; ++i) {
            (void)shared.decide(a[i], TargetList{}, memA);
            (void)shared.decide(b[i], TargetList{}, memB);
        }
        CHECK(agentsim::ForageBankPolicy::foraging(memA));
        CHECK(!agentsim::ForageBankPolicy::foraging(memB));
    }

    // ---- Hysteresis：A 的"现任"= 槽 1，B 的"现任"= 槽 0 ----
    {
        agentsim::HysteresisPolicy shared;
        Seq a;
        Seq b;
        for (int i = 0; i < kFrames; ++i) {
            a.push_back(twoCands(0.50, 0.54)); // 槽 1 略优 -> 一直保持槽 1
            b.push_back(twoCands(0.56, 0.50)); // 槽 0 略优 -> 一直保持槽 0
        }
        checkTwoAgentIsolation(shared, a, b);
        PolicyMemory memA;
        PolicyMemory memB;
        for (int i = 0; i < kFrames; ++i) {
            (void)shared.decide(a[i], TargetList{}, memA);
            (void)shared.decide(b[i], TargetList{}, memB);
        }
        CHECK(agentsim::HysteresisPolicy::lastChoice(memA) == 1);
        CHECK(agentsim::HysteresisPolicy::lastChoice(memB) == 0);
    }

    // ---- RiskAverse：A 的 EROI 冲高进 Earn，B 的 EROI 衰减进 Conserve ----
    {
        agentsim::RiskAversePolicy shared;
        Seq a;
        Seq b;
        for (int i = 0; i < 80; ++i) {
            a.push_back(riskFrame(0.05 + 0.01 * static_cast<double>(i + 1), 1.5)); // 增益
            b.push_back(riskFrame(0.7, 1.5));                                       // 零增益
        }
        checkTwoAgentIsolation(shared, a, b);
        PolicyMemory memA;
        PolicyMemory memB;
        for (int i = 0; i < 80; ++i) {
            (void)shared.decide(a[i], TargetList{}, memA);
            (void)shared.decide(b[i], TargetList{}, memB);
        }
        CHECK(agentsim::RiskAversePolicy::mode(memA) == 2); // Earn
        CHECK(agentsim::RiskAversePolicy::mode(memB) == 0); // Conserve
    }

    // ---- Bandit：A 的经历持续奖励 rule 臂（权重离开 0.5），
    //      B 的经历是两臂恒同向的对称场景（权重必须纹丝不动在 0.5/0.5）----
    {
        agentsim::ThermalPolicy::Params pr;
        pr.wThermal = 3.0; // 让 thermal 臂在 A 场景里被引向远食物（见 banditRuleFavored 注释）
        agentsim::BanditPolicy shared(0.5, pr); // 一个实例服务两个个体
        Seq a;
        Seq b;
        for (int i = 0; i < 60; ++i) {
            a.push_back(banditRuleFavored());
            b.push_back(banditAgreement());
        }
        checkTwoAgentIsolation(shared, a, b);
        PolicyMemory memA;
        PolicyMemory memB;
        for (int i = 0; i < 60; ++i) {
            (void)shared.decide(a[i], TargetList{}, memA);
            (void)shared.decide(b[i], TargetList{}, memB);
        }
        // A 确实学了：权重明显离开 0.5。
        CHECK(agentsim::BanditPolicy::weightRule(memA) > 0.9);
        // B 一动不动：对称场景下两臂回报恒相等，权重精确保持 0.5/0.5。
        // 这是"泄漏"的最灵敏探针——A 的任何学习结果若混进 B 的记忆，这里立刻失守。
        CHECK_NEAR(agentsim::BanditPolicy::weightRule(memB), 0.5, 1e-12);
        CHECK_NEAR(agentsim::BanditPolicy::weightThermal(memB), 0.5, 1e-12);
    }
}

// ===========================================================================
// A4 后半（决策层一侧）：新生核拿到的默认记忆 = 新个体起点，不继承任何经历
// ===========================================================================
TEST(decision_memory_newborn_starts_neutral) {
    PolicyMemory fresh{}; // = 新生核的 memory（默认构造）

    // 各策略的 reset() 结果必须与默认构造一致（"清空" == "回到新个体"）。
    PolicyMemory resetForage{};
    agentsim::ForageBankPolicy::reset(resetForage);
    PolicyMemory resetHyst{};
    agentsim::HysteresisPolicy::reset(resetHyst);
    PolicyMemory resetRisk{};
    agentsim::RiskAversePolicy::reset(resetRisk);
    PolicyMemory resetBandit{};
    agentsim::BanditPolicy::reset(resetBandit);
    CHECK(memoriesFieldEqual(fresh, resetForage));
    CHECK(memoriesFieldEqual(fresh, resetHyst));
    CHECK(memoriesFieldEqual(fresh, resetRisk));
    CHECK(memoriesFieldEqual(fresh, resetBandit));

    // 亲代跑完后的记忆 ≠ 新生记忆：把亲代那份直接当新生儿用，会继承经历（这正是
    // 要避免的接线错误）；而拷贝成默认值后行为回到起点。
    agentsim::ForageBankPolicy parent;
    PolicyMemory parentMem;
    const SensorVector low = forageLowEnergy();
    for (int i = 0; i < 5; ++i) (void)parent.decide(low, TargetList{}, parentMem);
    CHECK(agentsim::ForageBankPolicy::foraging(parentMem)); // 亲代在觅食态
    CHECK(!memoriesFieldEqual(parentMem, PolicyMemory{}));
    PolicyMemory child{};                                   // 新生：默认构造
    CHECK(!agentsim::ForageBankPolicy::foraging(child));
}

// ===========================================================================
// POD / 可逐字段序列化 / 确定性
// ===========================================================================
TEST(decision_memory_is_pod_and_deterministic) {
    // (1) 布局硬约束：trivially copyable ⇒ 无指针、无 vector/string、无虚函数、
    //     无自定义拷贝/析构；standard layout ⇒ 可按字段稳定序列化。
    static_assert(std::is_trivially_copyable<PolicyMemory>::value,
                  "PolicyMemory 必须 trivially copyable（存档按字段写读的前提）");
    static_assert(std::is_standard_layout<PolicyMemory>::value,
                  "PolicyMemory 必须是 standard layout（无指针、无虚函数）");
    static_assert(std::is_trivially_destructible<PolicyMemory>::value,
                  "PolicyMemory 必须有平凡析构（无堆资源、无 RAII 成员）");
    // 注：故意**不**要求 is_trivially_default_constructible —— 每个字段都带默认成员
    // 初始化器（"默认值 = 新个体起点"这条约束需要它），这会让该 trait 为 false，
    // 但构造仍然无分配、无副作用：PolicyMemory{} 只是逐字段写常量。
    static_assert(std::is_trivially_copy_constructible<PolicyMemory>::value,
                  "PolicyMemory 必须可平凡拷贝（每核一份、可整块快照）");
    static_assert(sizeof(PolicyMemory) < 2048,
                  "PolicyMemory 是每核一份的热数据，不该膨胀到 KB 级");

    // (2) memcpy 往返：逐字段值不变（等价于存档的整块快照/回滚）。
    PolicyMemory a;
    a.forageForaging = 1;
    a.hysteresisLastChoice = 2;
    a.riskEroi = 3.5;
    a.riskMode = 2;
    a.banditLogW[0] = 1.25;
    a.banditW[1] = 0.75;
    a.banditHasPrev = 1;
    a.banditPrevS[7] = 0.5;
    PolicyMemory b;
    std::memcpy(&b, &a, sizeof(a));
    CHECK(memoriesFieldEqual(a, b));
    CHECK(b.forageForaging == 1);
    CHECK(b.hysteresisLastChoice == 2);
    CHECK(b.riskEroi == 3.5);
    CHECK(b.banditPrevS[7] == 0.5);

    // (3) 确定性：同输入 + 同初值 memory，两次运行逐帧逐位相等（含记忆内容）。
    //     两个个体拿到的输入序列完全相同，只差各自那份记忆对象。
    agentsim::ThermalPolicy::Params p;
    p.wThermal = 3.0;
    agentsim::BanditPolicy pol(0.5, p);
    PolicyMemory m1; // 同初值
    PolicyMemory m2;
    for (int i = 0; i < 80; ++i) {
        // 前 40 帧持续奖励 rule 臂（学习真的发生），后 40 帧切到对称场景。
        const SensorVector s = (i < 40) ? banditRuleFavored() : banditAgreement();
        const ActionVector a1 = pol.decide(s, TargetList{}, m1);
        const ActionVector a2 = pol.decide(s, TargetList{}, m2);
        CHECK(actionsBitEqual(a1, a2));
        CHECK(memoriesFieldEqual(m1, m2));
    }
    // 跑完确实学到了东西（否则"确定性"平凡成立）。
    CHECK(!memoriesFieldEqual(m1, PolicyMemory{}));
    CHECK(agentsim::BanditPolicy::weightRule(m1) > 0.9);

    // (4) 其余三个有记忆的策略同样逐位可复现（记忆内容一起比）。
    {
        agentsim::RiskAversePolicy risk;
        PolicyMemory r1;
        PolicyMemory r2;
        for (int i = 0; i < 60; ++i) {
            const SensorVector s = riskFrame(0.05 + 0.01 * static_cast<double>(i + 1), 1.5);
            CHECK(actionsBitEqual(risk.decide(s, TargetList{}, r1),
                                  risk.decide(s, TargetList{}, r2)));
            CHECK(memoriesFieldEqual(r1, r2));
        }
        CHECK(!memoriesFieldEqual(r1, PolicyMemory{}));
    }
    {
        agentsim::ForageBankPolicy forage;
        PolicyMemory f1;
        PolicyMemory f2;
        for (int i = 0; i < 10; ++i) {
            const SensorVector s = (i % 2 == 0) ? forageLowEnergy() : forageHighEnergy();
            CHECK(actionsBitEqual(forage.decide(s, TargetList{}, f1),
                                  forage.decide(s, TargetList{}, f2)));
            CHECK(memoriesFieldEqual(f1, f2));
        }
    }
    {
        agentsim::HysteresisPolicy hyst;
        PolicyMemory h1;
        PolicyMemory h2;
        for (int i = 0; i < 10; ++i) {
            const SensorVector s = twoCands(0.50, 0.54);
            CHECK(actionsBitEqual(hyst.decide(s, TargetList{}, h1),
                                  hyst.decide(s, TargetList{}, h2)));
            CHECK(memoriesFieldEqual(h1, h2));
        }
        CHECK(agentsim::HysteresisPolicy::lastChoice(h1) == 1);
    }
}

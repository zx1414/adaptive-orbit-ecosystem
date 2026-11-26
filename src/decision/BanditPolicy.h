// agentsim - decision/BanditPolicy.h
// T24：BanditPolicy —— 多臂老虎机式策略加权（Hedge / EXP3-lite 指数加权）。
//
// 把已有的两条确定性策略当作两个"臂"：
//   臂 R = RulePolicy（规则打分），臂 T = ThermalPolicy（趋温打分）。
// 每次 decide()：
//   1. 两臂各算一个 ActionVector；
//   2. 两臂方向各自单位化后按权重线性混合、再单位化输出（零向量安全）；
//      boost 按权重线性混合并 clamp 到 [0,1]；
//   3. 用上一帧自己拿到的感知向量（自存历史，绝不读世界）估计每臂回报
//      r_i ∈ [-1,1]，按 Hedge/EXP3-lite 指数加权更新（对数空间实现）：
//        logW_i += eta * r_i
//        w = softmax(logW)，再施加 1e-6 反塌零地板并重新归一化。
//   eta = 0.05（默认学习率，可构造参数覆盖）。
// 回报公式与设计理由详见 BanditPolicy.cpp 文件头注释。
//
// R2 生态位假设：一个"精明的机会主义管理者"——它不自己发明打法，而是根据
// 最近的环境反馈在"贪手快"（rule 臂）和"挑温稳"（thermal 臂）之间动态分配
// 信任。食物分布规则时押 rule，环境温热起伏时押 thermal，两边都不讨好就回到
// 对半开继续试。它死法：环境回报信号本身嘈杂到无法区分两臂好坏（权重噪声化）。
//   R2 通道用法：
//   · mod[3]（聚拢）做"探索调制"：wR ≈ wT = 0.5（两臂谁也说服不了谁 = 决策最
//     不确定）时最需要信息 -> 正调制放大聚拢、凑近邻居观察它们吃什么；权重
//     尖锐化（确定）后回归 0。强度 kModExplore 可配，默认 0 = 关闭（回归安全）。
//   · 不点名施力（臂的施力意图不采纳：混合两臂的施力矢量在物理上没有可解释
//     语义，且会绕过 Hedge 的信用分配）。
// P0-4（记忆归个体）：Hedge 权重、上一帧感知/两臂输出、上一帧实际动作等**全部**
// 跨帧学习状态都住在 PolicyMemory 的 bandit* 块里，由每个核各自持有一份；本类实例
// 只保留构造期标定的不可变参数（eta_、kModExplore_ 与两个臂的内嵌参数）。
// 因此同一个 BanditPolicy 实例服务多个核时，各自独立学习，互不改写对方权重（A4）。
// 红线：decide() 全程确定性算术、绝不使用 RNG；只消费传入的 SensorVector 与
// 传入的个体记忆；不 include environment/ sim/ ledger/。
#pragma once

#include "decision/IDecisionPolicy.h"
#include "decision/PolicyMemory.h"
#include "decision/RulePolicy.h"
#include "decision/ThermalPolicy.h"

namespace agentsim {

class BanditPolicy : public IDecisionPolicy {
public:
    // eta：Hedge 学习率，构造时夹到 [0,10]（非有限归 0）。
    // thermalParams：透传给内部 ThermalPolicy 臂（与其它策略同款调参入口）。
    // kModExplore：R2 探索调制强度（>0 时 mod[3] 生效），默认 0 = 关闭。
    explicit BanditPolicy(double eta = 0.05, ThermalPolicy::Params thermalParams = {},
                          double kModExplore = 0.0);

    // memory 是本个体的学习历史（就地更新），默认值 = 等权 0.5/0.5 的新个体。
    ActionVector decide(const SensorVector& sensors, const TargetList& targets,
                        PolicyMemory& memory) override;
    using IDecisionPolicy::decide; // 基类两参/单参兼容重载（一次性无状态语义）
    const char* name() const override { return "bandit"; }

    // ---- 可解释性 / 单测接口（全部静态 + 收记忆参数：状态不在实例里）----
    // 臂 R（rule）当前归一化权重。
    static double weightRule(const PolicyMemory& memory) { return memory.banditW[0]; }
    // 臂 T（thermal）当前归一化权重。
    static double weightThermal(const PolicyMemory& memory) { return memory.banditW[1]; }
    // 学习率是构造期标定的不可变参数，仍在实例上。
    double eta() const { return eta_; }
    // 回到本策略的初态：权重 0.5/0.5、清空历史（只动 bandit* 块）。
    static void reset(PolicyMemory& memory);

private:
    RulePolicy rule_;
    ThermalPolicy thermal_;
    double eta_;         // 构造期标定，不可变
    double kModExplore_; // R2：探索调制强度（决策不确定度 -> mod[3]），不可变
};

} // namespace agentsim

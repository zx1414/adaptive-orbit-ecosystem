// agentsim - decision/ForageBankPolicy.h
// 觅食-蓄能两态策略：风险偏好随自身状态变化（能量足则停手蓄能，不足才全力觅食）。
//   · 觅食态：能量代理低 -> 直奔最近食物 (s[0], s[1])，boost=1.0 冲刺抢能量。
//   · 蓄能态：能量代理高 -> 停止扩张（逆当前速度方向缓行/原地），boost=0.0。
//   · 两阈值之间的中间带保持上一帧状态（滞回，防在阈值附近反复换态抖动）。
// 能量代理量的构造（感知向量没有能量维，必须用现有维合成，结果恒 ∈ [0,1]）：
//   energyProxy = wMargin*s[13] + (1-wMargin)*(1 - s[12])
//   · s[13]：距最近边界的归一化距离。贴边 = 高风险（撞界/劣化区），离边远 =
//     接近中心低扰动区 —— 正是蓄能态想要的空间状态，故作"处境余裕"项。
//   · s[12]：速度大小归一化 speed/(1+speed)。RulePolicy/ThermalPolicy 已把
//     "速度低 ≈ 能量低"用作紧迫度代理（同源约定）：跑得快烧能多、跑得慢在省能，
//     故取 (1 - s[12]) 作"节流余裕"项。两项均 ∈ [0,1]、权重非负，和恒 ∈ [0,1]。
//   注：s[9]（最近食物距离）刻意不进代理 —— 它反映环境机会而非自身储备，若计入
//   会让代理量随环境抖动、破坏滞回稳定性。
// 中心方向的反推说明：s[13] 只给"离边界最近的归一距离"这一个标量，不含方位信息，
// 严格反推中心坐标不可行。本策略把蓄能态实现为"停止扩张"：输出当前速度的反方向
//（抵消漂移、净位移最小 ≈ 原地蓄能）；速度近零时输出零向量（完全静止）。
// 纯确定性算术，绝不使用 RNG；绝不 include environment/ sim/ ledger/（红线 S4）。
//
// R2 生态位假设：一个"吃饱就躺平的仓鼠"——能量低时是全场景最凶的冲刺者，
// 能量高时立刻收手蓄能，绝不为多余的能量冒险。它在食物按时间/空间均匀到达的
// 环境里净效率最高；在"进食窗口短暂且竞争激烈"的环境里，它的蓄能期就是别人
// 抢跑的窗口，容易在富营养竞争中被饿死。R2 通道用法（两个都占满）：
//   · forceSlot：**觅食态把最近的球往自己这边拉**（拉到自己吃得到的位置）——
//     饥饿时才花这个能量，蓄能时绝不施力（forceSlot=-1）。取 TargetList[0]
//（距离最近的球），加速度沿 ForceTarget 的方向（核->球最近像），强度
//     kPullBoost * boost（与冲刺意愿同源）。注意：这是"拉自己过去/把球拉过来"
//     的复合意图，行为层按校验与扣费规则执行，决策只表达方向意图。
//   · mod[0]（视野）：觅食态放大视野（找食物优先），蓄能态收窄（省心）。
#pragma once
#include "decision/IDecisionPolicy.h"

namespace agentsim {

class ForageBankPolicy : public IDecisionPolicy {
public:
    struct Params {
        double forageBelow = 0.3; // 代理低于此值 -> 进入觅食态
        double bankAbove = 0.7;   // 代理高于此值 -> 进入蓄能态；中间带保持上帧
        double wMargin = 0.8;     // "处境余裕"项(s[13])权重；默认 0.8 保证 bankAbove=0.7 可达
        // ---- R2 新增（全部默认 0 = 回归安全）----
        double kPullBoost = 0.0;  // 觅食态施力强度（>0 时 forceSlot 生效）
        double kModSight = 0.0;   // 视野调制强度（觅食 +、蓄能 -）
    };
    explicit ForageBankPolicy(const Params& p = {});

    // 纯确定性；非有限输入清洗为 0，全 NaN 输入也不产生 NaN 输出。
    ActionVector decide(const SensorVector& s, const TargetList& targets) override;
    using IDecisionPolicy::decide; // 旧单参调用面（基类非虚重载）
    const char* name() const override { return "foragebank"; }

    // ---- 可解释性接口（供单测验证）----
    // 能量代理 ∈ [0,1]：wMargin*s[13] + (1-wMargin)*(1-s[12])，各项先 clamp01。
    double energyProxy(const SensorVector& s) const;
    // 当前状态：true = 觅食态，false = 蓄能态（中间带 = 上帧状态）。
    bool foraging() const { return foraging_; }
    // 清空两态记忆（回到默认蓄能态，单测确定性验证用）。
    void reset() { foraging_ = false; }

private:
    Params p_;
    bool foraging_ = false; // 两态滞回记忆：默认保守蓄能起步
};

} // namespace agentsim

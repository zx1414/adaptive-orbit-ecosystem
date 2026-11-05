// agentsim - decision/RulePolicy.h
// 规则打分策略：对 3 个食物候选逐个打分，取最高分方向。纯确定性，无 RNG。
//
// R2 生态位假设（本策略靠什么活/怎么死）：
//   生存方式：贪手机会主义者——吃离自己最近的食物，越饿跑得越快；顺路候选加分，
//   把转向能耗压到最低；感知失真大时降低对读数的信任（少跑冤枉路）。
//   死法：无食物原野（原地饿死，没有任何探索冲动）；高失真原野（错信读数反复扑空）；
//   核群密集处（不主动争抢、被挤出资源区，只在拥挤时象征性减速）。
//   与 R2 新通道的关系：不点名施力（贪心逻辑下没有"替球搬家"的预算理由），
//   不做社会调制；只消费邻居维做"拥挤让速"（被动礼让，不是合作）。
#pragma once
#include "decision/IDecisionPolicy.h"

namespace agentsim {

class RulePolicy : public IDecisionPolicy {
public:
    explicit RulePolicy(double wProximity = 1.0, double wAlignment = 0.8, double wUrgency = 0.6);
    // R2 双参主接口：targets 参与契约（本策略恒不施力），感知只用 s[0..16]。
    ActionVector decide(const SensorVector& sensors, const TargetList& targets) override;
    using IDecisionPolicy::decide; // 旧单参调用面（基类非虚重载）
    const char* name() const override { return "rule"; }

private:
    double wP_;  // 接近度权重：奖励更近的食物
    double wA_;  // 对齐权重：奖励与当前航向一致的候选，减少折返抖动
    double wU_;  // 紧迫度权重：能量代理量越低越急迫，压过对齐项
};

class StubPolicy : public IDecisionPolicy {
public:
    // R2 双参接口：Stub 什么都不做——默认保守契约的完整性由此测试：
    // 零方向、零 boost、不施力（forceSlot=-1）、不调制（mod 全 0）。
    ActionVector decide(const SensorVector& sensors, const TargetList& targets) override;
    using IDecisionPolicy::decide;
    const char* name() const override { return "stub"; }
};

} // namespace agentsim

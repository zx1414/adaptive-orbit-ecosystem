// agentsim - decision/IDecisionPolicy.h
// 决策层接口。SensorVector/ActionVector 已上移到中立头 core/SensorTypes.h（G1 修复：
// 消除 探测->决策 逆向依赖）。本头文件只定义策略接口；决策层绝不 include
// environment/ sim/ ledger/，也不使用任何 RNG（红队检查项）。
//
// R2 契约（V2-SPEC-AMEND-1 §2/§3）：decide() 升级为双参版本——
//   额外接收 TargetList（本帧可施力球清单，按环面距离升序，items[0] 最近）；
//   ActionVector 新增点名施力（forceSlot/forceX/forceY）与社会参数调制（mod[4]）。
// 决策层只表达"意图"：有效性校验（slot 存活、球在 forceRange 内）与能量扣费
// 全部归行为层（唯一写点）。
//
// 兼容层：基类提供**非虚**单参 decide(sv) 重载，等价于 decide(sv, 空目标表)。
// 旧调用点（Simulation 的 policy_->decide(sv)、BanditPolicy 的内臂调用、
// 既有单测的 pol.decide(s)）据此继续编译且行为逐位不变——空 targets 下任何
// 合规策略都不得产出施力意图。每个具体策略必须实现双参版本，并用
// using IDecisionPolicy::decide; 把基类单参重载引入作用域（否则派生类的
// decide 同名声明会把基类重载隐藏掉，单参调用点编译失败）。
#pragma once
#include "core/SensorTypes.h"

namespace agentsim {

class IDecisionPolicy {
public:
    virtual ~IDecisionPolicy() = default;

    // R2 主接口：24 维感知向量 + 可施力球清单（按环面距离升序）。
    // 红线：实现绝不使用 RNG、绝不读世界（只消费传入快照）；输出意图，
    // 不落任何实体状态。
    virtual ActionVector decide(const SensorVector& sensors, const TargetList& targets) = 0;

    // 旧单参调用面（非虚）：等价于传入空 TargetList——不施力、无目标感知。
    // 函数局部 static 空表：全进程一份，避免每次调用构造临时对象。
    ActionVector decide(const SensorVector& sensors) {
        static const TargetList empty{};
        return decide(sensors, empty);
    }

    virtual const char* name() const = 0;
};

} // namespace agentsim

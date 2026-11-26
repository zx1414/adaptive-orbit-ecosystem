// agentsim - decision/IDecisionPolicy.h
// 决策层接口。SensorVector/ActionVector 已上移到中立头 core/SensorTypes.h（G1 修复：
// 消除 探测->决策 逆向依赖）。本头文件只定义策略接口；决策层绝不 include
// environment/ sim/ ledger/，也不使用任何 RNG（红队检查项）。
//
// R2 契约（V2-SPEC-AMEND-1 §2/§3）：decide() 接收 TargetList（本帧可施力球清单，
//   按环面距离升序，items[0] 最近）；ActionVector 含点名施力（forceSlot/forceX/
//   forceY）与社会参数调制（mod[4]）。
// 决策层只表达"意图"：有效性校验（slot 存活、球在 forceRange 内）与能量扣费
// 全部归行为层（唯一写点）。
//
// P0-4 契约（docs/NEXT-ECOLOGY-CONTRACT.md §2，验收 A4）：**记忆归个体**。
//   主接口是三参版本：
//     virtual ActionVector decide(const SensorVector&, const TargetList&,
//                                PolicyMemory& memory) = 0;
//   memory 是**调用方**（每个核）持有的那份状态：策略自己的跨帧可变量全部住在
//   这里，策略实例里不得再有跨 decide() 存活的实例可变成员（不可变参数/只读常量
//   除外）。同一个 policy 实例服务 N 个核时，各核的觅食经历 / 学习历史 / 上一帧
//   目标互不影响；新生核拿到默认构造的 PolicyMemory（不继承亲代经历）。
//   memory 是按引用传入的**输出**参数，策略会就地更新它（这正是"记忆"）。
//
// 兼容层（非虚，两个重载，基类内实现）：
//   · decide(sensors, targets)：等价于传入函数内新建的临时 PolicyMemory{}。
//   · decide(sensors)：等价于 decide(sensors, 空 TargetList) —— 不施力、无目标感知。
//   两者都是**一次性无状态语义**：每次调用都从"全新个体"的默认记忆出发，调用者
//   拿不到、也传不进任何跨帧历史。既有调用点（Simulation 的两参调用、BanditPolicy
//   的内臂调用、既有单测的 pol.decide(s) / pol.decide(s, t)）据此零改动继续编译，
//   且行为等价于"每次给一个新核"——旧单测关心的正是"单帧决策"这一面。
//   需要连续多帧记忆时**必须走三参版本**并自己持有 PolicyMemory（见 test_decision_memory.cpp）。
//   每个具体策略必须实现三参版本，并用 using IDecisionPolicy::decide; 把基类重载
//   引入作用域（否则派生类的 decide 同名声明会把基类重载隐藏掉，两参/单参调用点
//   编译失败）。
#pragma once

#include "core/SensorTypes.h"
#include "decision/PolicyMemory.h"

namespace agentsim {

class IDecisionPolicy {
public:
    virtual ~IDecisionPolicy() = default;

    // P0-4 主接口：24 维感知向量 + 可施力球清单（按环面距离升序）+ 本个体记忆。
    // 红线：实现绝不使用 RNG、绝不读世界（只消费传入快照与传入记忆）；输出意图，
    // 除 memory 外不写任何实体状态。memory 默认值 == 新个体起点（见 PolicyMemory.h）。
    virtual ActionVector decide(const SensorVector& sensors, const TargetList& targets,
                               PolicyMemory& memory) = 0;

    // 兼容重载 A（非虚）：等价于传入空 TargetList + **每次调用新建**的临时记忆。
    // 一次性无状态语义：本次调用的学习/滞回结果随临时对象一起丢弃，不会跨调用累积。
    // 函数局部 static 空表：全进程一份，避免每次调用构造临时 TargetList。
    ActionVector decide(const SensorVector& sensors) {
        static const TargetList empty{};
        PolicyMemory scratch; // 一次性：无状态语义（不要把它提升为成员！）
        return decide(sensors, empty, scratch);
    }

    // 兼容重载 B（非虚）：等价于传入**每次调用新建**的临时记忆。
    // 同上，一次性无状态语义；需要跨帧记忆请用三参版本。
    ActionVector decide(const SensorVector& sensors, const TargetList& targets) {
        PolicyMemory scratch; // 一次性：无状态语义（不要把它提升为成员！）
        return decide(sensors, targets, scratch);
    }

    virtual const char* name() const = 0;
};

} // namespace agentsim

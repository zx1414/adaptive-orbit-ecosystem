// agentsim - decision/PolicyMemory.h
// P0-4（验收 A4）：让决策记忆归个体。
//
// 背景（问题陈述）：组合根在核循环里对**同一个** policy_ 实例调用 decide()，
// 而 ForageBankPolicy / BanditPolicy / HysteresisPolicy / RiskAversePolicy 曾把
// 跨帧学习状态放在实例成员里 —— 于是 A 核的觅食经历会改写 B 核的状态：一个核
// 可以因为"远处同类"的行为改变自己的状态。修法：所有跨 decide() 存活的**可变量**
// 都搬进本结构体，由每个核各自持有一份（组合根接线：policy_->decide(sv, targets,
// n.memory)）；策略实例只保留构造期标定好的**不可变**参数。
//
// 三条硬约束（改这个结构体前先读）：
//   1. 必须 POD 且 trivially copyable：只含 double/int/固定长度数组，无指针、
//      无 std::vector/std::string、无虚函数、无自定义拷贝/析构。这样存档可以按
//      字段逐条写读，也可以整块拷贝做快照/回滚/一致性比对。
//   2. 每个字段都必须有**默认成员初始化器**，且默认值 == 该策略的"新个体起点"。
//      理由有二：
//        · IDecisionPolicy 的两参/单参兼容重载靠函数内 `PolicyMemory{}` 表达
//          "一次性无状态语义"（每次调用都从新个体起点出发）；
//        · 新生核的记忆默认构造（不继承亲代最近一次觅食经历，A4 后半）。
//   3. 这里不放任何"全局/共享"语义的东西：一份 PolicyMemory 只属于一个个体。
//      字段按所属策略分块命名（forage* / hysteresis* / bandit* / risk*），互不干扰：
//      同一个体即使换了策略类型，记忆里其它块的数据也不会互相踩踏。
//
// 序列化口径（供存档接线）：按字段逐个写 double/int，**不要**整块写 sizeof 字节
// （padding 字节未定义）；读回时先比 schemaVersion，不符就提示并拒绝，不做静默迁移。
#pragma once

#include "core/SensorTypes.h"

namespace agentsim {

// ln(0.5)：Hedge 两臂对数权重初值（等价 0.5/0.5 等权）。
// 用常量字面值而不是 std::log(0.5)：默认成员初始化器必须是常量表达式，且记忆对象
// 不依赖任何构造期计算（可直接逐字段序列化）。softmax 对两臂同一初值的平移不变，
// 故即使与运行期 std::log(0.5) 相差 1 ulp 也不改变任何策略输出。
constexpr double kPolicyLogHalfWeight = -0.69314718055994530941723212145817657;

struct PolicyMemory {
    // 记忆布局版本。存档接好后：版本不符 -> 提示并拒绝；本版 = 1。
    static constexpr int kSchemaVersion = 1;
    int schemaVersion = kSchemaVersion;

    // ================= ForageBankPolicy（觅食 / 蓄能两态滞回） =================
    // 0 = 蓄能态（默认 = 新个体起点；策略自带"速度近零强制觅食"的自举，不会死锁），
    // 1 = 觅食态。同一输入序列在中间带内的走向取决于这份历史，故必须个体独有。
    int forageForaging = 0;

    // ================= HysteresisPolicy（候选滞回选择） =================
    // 上帧选中的候选下标 0/1/2（滞回的"现任"）；-1 = 尚未选择。
    // 无候选帧刻意不改它（食物只是暂时离开视野），该语义也随记忆归个体。
    int hysteresisLastChoice = -1;

    // ================= RiskAversePolicy（EROI 状态机） =================
    double riskEroi = 0.0;                // EMA 后含温度折扣的 EROI 估计，恒 >= 0
    double riskPrevProx = 0.0;            // 上帧候选 0 接近度（增益只取正部）
    double riskLastDir[2] = {0.0, 0.0};   // 最近一次有效的食物方向 [x, y]
    double riskCachedDir[2] = {0.0, 0.0}; // Conserve 态低频刷新的方向缓存 [x, y]
    int riskHasCachedDir = 0;             // Conserve 方向缓存是否已建立
    int riskMode = 1;                     // 0=Conserve 1=Mid 2=Earn（中性 Mid 起步）
    int riskFrameInMode = 0;              // 当前模式内已持续帧数
    int riskFrameInModeMode = 1;          // riskFrameInMode 所属模式（迁移时清零重计）

    // ================= BanditPolicy（Hedge / EXP3-lite 两臂指数加权） =========
    // 臂序：[0] = rule，[1] = thermal（与 BanditPolicy.h 的臂定义一致）。
    double banditLogW[2] = {kPolicyLogHalfWeight, kPolicyLogHalfWeight}; // 对数权重
    double banditW[2] = {0.5, 0.5};       // 归一化权重（反塌零地板之后）
    int banditHasPrev = 0;                // 是否已有上一帧历史（首帧只建历史不更新）
    double banditPrevS[SensorVector::kDim] = {}; // 上一帧清洗后的感知快照
    double banditPrevArm[2][2] = {};      // [臂][0=dirX, 1=dirY] 上一帧两臂方向
    double banditPrevOut[2] = {};         // 上一帧自身实际输出方向 [dirX, dirY]
};

// 回到"全新个体"：清空**全部**策略记忆。
// 用途：新建核、单测的基线、诊断工具。只想清自己那一块时用各策略提供的
// reset(PolicyMemory&)（避免误伤同一份记忆里其它策略的字段）。
inline void clearPolicyMemory(PolicyMemory& memory) { memory = PolicyMemory{}; }

} // namespace agentsim

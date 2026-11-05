// agentsim - decision/RulePolicy.cpp
// 打分机制（对传感器里 3 个食物候选逐个算分，取最高）：
//   proximity = s[6+i]            归一化接近度 1-dist/R，越近分越高 —— 吃到食物的期望收益。
//   alignment = 候选方向 · 当前速度方向（归一化后点积，[-1,1]）
//                               顺路候选分高、逆路候选分低 —— 减少急转与折返的能量浪费；
//                               速度近零（刚起步/被阻尼停住）时取 0，即对齐项失效。
//   urgency   = 1 - clamp(能量代理量)。传感器没有能量维，用 s[12]（速度大小归一化）
//                               做代理：跑得越慢代理能量越低 -> 越紧急 -> 越愿意牺牲对齐直奔食物。
//   温度失真（GOAL）：s[16] ∈ [0,1] 为所在点温度失真度。失真越大，感知读数越不可信，
//   故对 proximity 权重乘以信任因子 (1 - 0.5*s[16])（最多打 5 折），降低"看见什么信什么"
//   的程度；alignment/urgency 是自身状态项，不依赖读数质量，不打折。
//   score = wP*(1-0.5*s[16])*proximity + wA*alignment + wU*urgency
// 全部候选为空（s[6..8] 全 0）时输出 {0,0}，原地等待。
//
// R2 新维消费：s[18..20] 携带最近邻核信息（距离/方向），s[21..23] 是能量与可施力
// 球通道。本策略【不消费】targets（不点名施力），也【不做】mod 调制——贪手机会
// 主义者没有合作性行为。唯一的 R2 行为是"拥挤让速"：邻居贴近时降低 sprint 意愿，
// 减少与同类争抢同一目标；s[19]==0 && s[20]==0 是感知层"无邻居"哨兵，此通道静默时
// 输出与旧版逐位一致（回归安全，既有单测守护）。
// 纯确定性算术，绝不使用任何 RNG（红队检查项）。
#include "decision/RulePolicy.h"

namespace agentsim {

RulePolicy::RulePolicy(double wProximity, double wAlignment, double wUrgency)
    : wP_(wProximity), wA_(wAlignment), wU_(wUrgency) {}

ActionVector RulePolicy::decide(const SensorVector& s, const TargetList& /*targets*/) {
    ActionVector a;

    // 三个候选是否有效：s[6..8] 任一 > 0 表示该槽位有真实食物。
    bool any = false;
    for (int i = 0; i < 3; ++i) {
        if (s[6 + i] > 0.0) { any = true; break; }
    }
    if (!any) {
        return a; // 全 0：原地等待
    }

    // 紧迫度：传感器无能量维，用 s[12]（速度大小归一化，[0,1)）做能量代理。
    // clamp 到 [0,1] 防御异常输入；越慢 -> 越紧急。
    double energyProxy = s[12];
    if (energyProxy < 0.0) energyProxy = 0.0;
    if (energyProxy > 1.0) energyProxy = 1.0;
    const double urgency = 1.0 - energyProxy;

    // 当前航向：s[10..11] = vel/(1+speed)，模长恰为 s[12]，
    // 故 s[12] > 0 时除以它即得单位航向；s[12] 近零时对齐项整体失效（取 0）。
    const double hx = (s[12] > 1e-9) ? s[10] / s[12] : 0.0;
    const double hy = (s[12] > 1e-9) ? s[11] / s[12] : 0.0;

    double bestScore = 0.0;
    double bestX = 0.0;
    double bestY = 0.0;
    bool first = true;

    for (int i = 0; i < 3; ++i) {
        if (s[6 + i] <= 0.0) continue; // 空槽位跳过
        const double dx = s[2 * i + 0];      // 候选方向（已是单位向量）
        const double dy = s[2 * i + 1];
        const double proximity = s[6 + i];
        // 候选方向与航向直接点积；无航向时对齐项取 0。
        const double alignment = (s[12] > 1e-9) ? (dx * hx + dy * hy) : 0.0;
        // GOAL：温度失真大 -> 降低对当前感知的信任（只消费 s[16] 标量，不碰环境层）。
        // 防御性 clamp：契约保证 s[16] ∈ [0,1]，异常输入也不产生负权重。
        double d = s[16];
        if (d < 0.0) d = 0.0;
        if (d > 1.0) d = 1.0;
        const double trust = 1.0 - 0.5 * d;
        const double score = wP_ * trust * proximity + wA_ * alignment + wU_ * urgency;
        if (first || score > bestScore) {
            first = false;
            bestScore = score;
            bestX = dx;
            bestY = dy;
        }
    }

    a.dirX = bestX;
    a.dirY = bestY;
    // 冲刺意愿与紧迫度挂钩：越紧急越冲。
    a.boost = urgency;

    // R2 拥挤让速：邻居贴近（s[18] 归一化距离小）时压低 sprint 意愿。
    // 归一化因子 0.5 + 0.5*s[18] ∈ [0.5,1]：完全贴脸 -> 半速，视野外 -> 无影响。
    // 哨兵先行：s[19]==0 && s[20]==0 表示感知层判定无邻居，通道静默不打折。
    // NaN 防御：s[18] 非正（含 NaN）按完全拥挤处理（保守让速，不传播 NaN）。
    if (s[19] != 0.0 || s[20] != 0.0) {
        double sr = s[18];
        if (!(sr > 0.0)) sr = 0.0;
        if (sr > 1.0) sr = 1.0;
        a.boost *= 0.5 + 0.5 * sr;
    }
    return a;
}

// Stub：两参接口下依然是"绝对不作为"。默认保守（forceSlot=-1、mod 全 0）由
// ActionVector 默认成员给出，构造临时对象即契约。
ActionVector StubPolicy::decide(const SensorVector& /*sensors*/, const TargetList& /*targets*/) {
    return {};
}

} // namespace agentsim

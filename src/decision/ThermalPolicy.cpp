// agentsim - decision/ThermalPolicy.cpp
// 趋温策略实现：温度是资源品质的代理信号，显式推理、完全可解释、零 RNG。
// 打分机制（对至多 3 个食物候选逐个算分，取最高分方向）：
//   proximity = s[6+i]                     归一化接近度，越近分越高。
//   thermal   = 1 - |s[17]-preferred|/0.5  温度适宜度：s[17] 是"自身所在点"的
//                                          归一化温度采样（perception 契约），
//                                          越接近物种最适温度分越高，clamp [0,1]。
//   caution   = 1 - s[16]                  失真规避：s[16] 是该点温度失真度，
//                                          失真越大读数越不可信，越谨慎。
//   score = wProximity*proximity + wThermal*thermal + wCaution*caution
//   thermal/caution 是"自身位置"的全局项（不随候选变化），候选排序实际由接近度
//   决定；它们把"当前点值不值得追"显式写进总分，并驱动 ③ 的高失真降速门控。
//   ① s[9] <= 0（最近食物距离归一化为 0 = 无目标）时原地等待 {0,0}。
//   ③ s[16] > 0.5 时 boost = 0.3，否则 0：高失真区降速，减少噪声中的无谓机动。
//   ④ 方向归一化输出；长度近 0 输出 {0,0}，绝不产生 NaN。
//
// R2 新通道（targets/mod）：
//   · targets 只读不消费（本策略不施力）——契约完整性由单测钉死 forceSlot=-1。
//   · mod[] 社会调制：舒适度 comfort = suitability * (1 - 0.5*s[16])（温和且
//     不失真为正，极端温度或高失真为负，∈ [-1,1]）。三个可配强度把 comfort
//     写进 mod[0]（视野）、mod[1]（个人空间）、mod[3]（聚拢），失真/失适时取反：
//     温水舒适 -> 收窄视野、放大个人空间、压低聚拢（独行慢爬）；环境恶劣 ->
//     反向调制（放眼看同伴、抱团、缩小私人距离）。强度默认 0 = mod 全 0
//     （回归安全：既有单测的精确值断言不受影响）。
// 纯确定性算术，绝不使用任何 RNG（红队检查项）；非有限输入一律归 0 防御。
#include "decision/ThermalPolicy.h"

#include <cmath>

namespace agentsim {
namespace {

// 防御性清洗：非有限值（NaN/Inf）一律归 0，全 NaN 输入也不产生 NaN 输出。
double clean(double x) { return std::isfinite(x) ? x : 0.0; }

// clamp 到 [0,1]。
double clamp01(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

// clamp 到 [-1,1]（NaN 走第一支归 -1，绝不传播）。
double clampSym1(double x) {
    if (!(x > -1.0)) return -1.0;
    if (x > 1.0) return 1.0;
    return x;
}

// 高失真降速常量：失真严格大于门限时冲刺意愿取 0.3，抑制噪声区机动。
constexpr double kHighDistortionBoost = 0.3;
constexpr double kDistortionBoostGate = 0.5;

} // namespace

ThermalPolicy::ThermalPolicy(const Params& p) : p_(p) {
    // 参数清洗：权重非负、最适温度夹到 [0,1]，非有限参数归 0。
    p_.wProximity = clean(p.wProximity);
    if (p_.wProximity < 0.0) p_.wProximity = 0.0;
    p_.wThermal = clean(p.wThermal);
    if (p_.wThermal < 0.0) p_.wThermal = 0.0;
    p_.wCaution = clean(p.wCaution);
    if (p_.wCaution < 0.0) p_.wCaution = 0.0;
    p_.preferredTemp = clamp01(clean(p.preferredTemp));
    // R2 调制强度：非有限归 0，夹到 [-1,1]（超过 ±1 的调制没有生物学意义，
    // 且行为层还会按 ±40% 上限二次钳制，这里清洗只为策略自洽）。
    double k[3] = {clean(p.kModSight), clean(p.kModSpace), clean(p.kModCohesion)};
    for (double& v : k) {
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
    }
    p_.kModSight = k[0];
    p_.kModSpace = k[1];
    p_.kModCohesion = k[2];
}

// 温度适宜度：1 - |s[17]-preferred|/0.5，clamp [0,1]；偏离最适 0.5 以上记 0 分。
double ThermalPolicy::thermalSuitability(const SensorVector& s) const {
    const double t = clean(s[17]);
    const double dev = std::fabs(t - p_.preferredTemp) / 0.5;
    return clamp01(1.0 - dev);
}
// 失真规避因子：失真越大越谨慎（1 - clamp01(s[16])）。
double ThermalPolicy::cautionFactor(const SensorVector& s) const {
    return 1.0 - clamp01(clean(s[16]));
}

// R2：舒适度 = 适宜度 × 失真惩罚。温和（suitability=1）且读数可信（d=0）时为 +1，
// 完全失真或完全偏离最适温度时落向 0/负区。∈ [-1,1]。
double ThermalPolicy::comfort(const SensorVector& s) const {
    const double suit = thermalSuitability(s);
    const double d = clamp01(clean(s[16]));
    return clampSym1(suit * (1.0 - 0.5 * d) - 0.5 * d);
}

// 候选得分：接近度 + **候选级**温度适宜度 + 失真规避，权重来自 Params。
//
// 关键设计（R4-P1 修复）：s[17] 是"自身所在点"的温度，对所有候选相同，
// 若直接把 thermalSuitability(s) 加进总分，它对 argmax 是常数、无法影响选择，
// 导致 --thermal-pref 结构性失效。因此把温度适宜度做成**候选级**：
//
//   stay_i = dot(候选方向 i, 当前速度方向) ∈ [-1,1]
//            —— 越顺着当前运动方向走，途经温度变化越平滑，越可能留在适宜区。
//   thermal_i = suitability * (0.5 + 0.5 * stay_i)
//            —— 适宜度高时更愿意保持方向；适宜度低时该项整体缩小，
//               候选间温差被压平，行为退回"就近取食"。
//
// 于是 preferredTemp 变化 → suitability 变化 → 各候选 thermal_i 的相对权重变化
// → argmax 可能改变，参数真正生效（R4 验收：pref 0.5 与 0.05 输出必须不同）。
double ThermalPolicy::candidateScore(const SensorVector& s, int i) const {
    if (i < 0 || i > 2) return 0.0; // 至多 3 个候选槽位
    const double proximity = clamp01(clean(s[6 + i]));
    const double suitability = thermalSuitability(s);
    const double caution = cautionFactor(s);

    // 当前速度方向（s[10], s[11]）；速度为 0 时 stay 取 0（中性）。
    const double vx = clean(s[10]);
    const double vy = clean(s[11]);
    const double vlen = std::sqrt(vx * vx + vy * vy);
    double stay = 0.0;
    if (vlen > 1e-9) {
        const double cx = clean(s[2 * i + 0]);
        const double cy = clean(s[2 * i + 1]);
        const double clen = std::sqrt(cx * cx + cy * cy);
        if (clen > 1e-9) stay = (cx * vx + cy * vy) / (clen * vlen);
    }
    if (stay < -1.0) stay = -1.0;
    if (stay > 1.0) stay = 1.0;

    // 候选级温度项：适宜度决定"保持方向"的收益大小。
    const double thermal = suitability * (0.5 + 0.5 * stay);

    // 失真规避（E2-P1 修复）：caution 只依赖 s[16]，对 3 个候选是**同一个常数**。
    // 若把它作为独立加项 `wCaution * caution`，它只给所有候选同时加上一个常数，
    // argmax 完全不变 —— 这就是 --thermal-w-caution 结构性失效的根因（E2 用 80 组穷举证明）。
    //
    // 正确做法：让 wCaution 调节"接近度"与"保持方向"两个**候选级**信号的相对强度。
    //   · caution 高（失真小、读数可信）→ 更相信接近度，直接扑最近的食物；
    //   · caution 低（失真大、读数不可信）→ 压低接近度，更偏向 stay（保持当前方向，少折腾）。
    // 这样 wCaution 会改变候选间的分差，从而真正影响 argmax。
    // 失真规避（R6-P0-1 修复，取代会变负的线性调制 1 - 0.5*wc*(1-caution)）：
    // caution=1（读数可信）时两因子均为 1；caution 越小（失真越大）调制越强。
    // 两式恒正，任何 wCaution >= 0 都不会让 proximity 项变负 —— 避免 argmax 反转
    // （旧式在 wCaution*(1-caution) > 2 时 trustFood < 0，红队 probe6_wc 实测翻转）。
    const double doubt = 0.5 * p_.wCaution * (1.0 - caution); // >= 0
    const double trustFood = 1.0 / (1.0 + doubt);             // ∈ (0,1]，恒正
    const double trustStay = 1.0 + doubt;                     // >= 1，不翻转符号

    return p_.wProximity * proximity * trustFood
         + p_.wThermal * thermal * trustStay;
}

// ①无目标早退 -> ②逐候选打分取最高 -> ③高失真降速 -> ④方向归一化防 NaN
// -> ⑤ R2 社会调制（comfort 写 mod，强度默认 0）。
ActionVector ThermalPolicy::decide(const SensorVector& s, const TargetList& /*targets*/) {
    ActionVector a;

    // ① 无目标：最近食物距离归一化 s[9] <= 0，且候选槽位全空时原地等待。
    // 两者任一成立都视为无有效目标（契约：无食物时 s[9]=0、s[6..8]=0）。
    bool anyCandidate = false;
    for (int i = 0; i < 3; ++i) {
        if (clean(s[6 + i]) > 0.0) { anyCandidate = true; break; }
    }
    if (!(clean(s[9]) > 0.0) || !anyCandidate) {
        // R2：无目标帧也输出社会调制（环境舒适度与"有没有吃的"无关）。
        const double c = comfort(s);
        a.mod[0] = clampSym1(p_.kModSight * c);
        a.mod[1] = clampSym1(p_.kModSpace * c);
        a.mod[3] = clampSym1(p_.kModCohesion * c);
        return a; // {0,0}，boost 亦 0
    }

    // ② 逐候选打分，取最高分候选方向；并列时先到先得（稳定、确定性）。
    double bestScore = 0.0;
    double bestX = 0.0;
    double bestY = 0.0;
    bool first = true;
    for (int i = 0; i < 3; ++i) {
        if (clean(s[6 + i]) <= 0.0) continue; // 空槽位跳过
        const double score = candidateScore(s, i);
        if (first || score > bestScore) {
            first = false;
            bestScore = score;
            bestX = clean(s[2 * i + 0]);
            bestY = clean(s[2 * i + 1]);
        }
    }

    // ④ 方向归一化输出；长度近 0 时输出 {0,0}，绝不产生 NaN/Inf。
    const double len = std::sqrt(bestX * bestX + bestY * bestY);
    if (len > 1e-9 && std::isfinite(len)) {
        a.dirX = bestX / len;
        a.dirY = bestY / len;
    } else {
        a.dirX = 0.0;
        a.dirY = 0.0;
    }

    // ③ 高失真降速：s[16] > 0.5 时 boost = 0.3，减少噪声中的无谓机动；
    // 其余情况 boost = 0（趋温策略不鼓励无脑冲刺）。
    const double d = clamp01(clean(s[16]));
    a.boost = (d > kDistortionBoostGate) ? kHighDistortionBoost : 0.0;

    // ⑤ R2 社会调制：comfort > 0（温带、低失真）= 慢爬独行（收窄视野、放大
    // 个人空间、压低聚拢）；comfort < 0（极端温度/高失真）= 反向抱团求存。
    // mod[2]（斥力）不参与：斥力由行为层的碰撞响应决定，不做意愿调制。
    const double c = comfort(s);
    a.mod[0] = clampSym1(p_.kModSight * c);
    a.mod[1] = clampSym1(p_.kModSpace * c);
    a.mod[3] = clampSym1(p_.kModCohesion * c);
    return a;
}

} // namespace agentsim

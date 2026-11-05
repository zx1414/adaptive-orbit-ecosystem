// agentsim - decision/HysteresisPolicy.cpp
// 打分（候选级，与 RulePolicy 同源的信号，缺紧迫项 —— 本策略只解决抖动）：
//   proximity = s[6+i]      归一化接近度，越近分越高。
//   alignment = 候选方向·当前航向（[-1,1]），速度近零时取 0（对齐项失效）。
//   score_i   = wProximity*proximity_i + wAlignment*alignment_i
// 滞回规则（decide 无 RNG、不读世界，只动自身状态 lastChoice_）：
//   · 无候选（s[6..8] 全 <= 0）-> 输出 {0,0} 且**不改**滞回记忆：食物只是暂时
//     离开视野，回来时优先延续旧选择，进一步抑制抖动。
//   · 现任候选仍有效 -> 只有 bestScore > score(现任) + gap 才切换，否则保持现任。
//   · 现任失效（其槽位 <= 0）或首帧（lastChoice_ == -1）-> 直接选当帧最高分；
//     并列时先到先得（下标小者优先），确定且稳定。
// 方向输出 = 选中候选的 (s[2i], s[2i+1]) 重新归一化；长度近 0（如全 NaN 输入）
// 时输出 {0,0}，绝不产生 NaN。boost = 选中候选的接近度（越近越值得冲刺）。
//
// R2 新通道：targets 只读不消费（不施力）；mod[1] 聚焦调制——
//   focus = 选中候选的接近度 ∈ [0,1]（无候选 = 0）。
//   有 kModFocus 时：focus 高 -> mod[1] = +k*focus（个人空间放大=保持距离慢慢贴，
//   不惊扰目标）；focus 低/无目标 -> mod[1] = -k*(1-focus)（空间收窄，方便挤进
//   密集区）。k 默认 0：mod 全 0，与旧版逐位一致（回归安全）。
// 非有限输入一律清洗为 0（clean），纯确定性算术，绝不使用任何 RNG（红队检查项）。
#include "decision/HysteresisPolicy.h"

#include <cmath>

namespace agentsim {
namespace {

// 防御性清洗：非有限值（NaN/Inf）一律归 0。
double clean(double x) { return std::isfinite(x) ? x : 0.0; }

// clamp 到 [-1,1]（NaN 走第一支归 -1，绝不传播）。
double clampSym1(double x) {
    if (!(x > -1.0)) return -1.0;
    if (x > 1.0) return 1.0;
    return x;
}

} // namespace

HysteresisPolicy::HysteresisPolicy(const Params& p) : p_(p) {
    // 参数清洗：gap/权重非负（负 gap 会让滞回反向抖动，直接归 0）。
    p_.hysteresisGap = clean(p.hysteresisGap);
    if (p_.hysteresisGap < 0.0) p_.hysteresisGap = 0.0;
    p_.wProximity = clean(p.wProximity);
    if (p_.wProximity < 0.0) p_.wProximity = 0.0;
    p_.wAlignment = clean(p.wAlignment);
    if (p_.wAlignment < 0.0) p_.wAlignment = 0.0;
    // R2：聚焦调制强度夹到 [-1,1]（非有限清洗为 0 由 clean 完成）。
    p_.kModFocus = clean(p.kModFocus);
    if (p_.kModFocus > 1.0) p_.kModFocus = 1.0;
    if (p_.kModFocus < -1.0) p_.kModFocus = -1.0;
}

double HysteresisPolicy::candidateScore(const SensorVector& s, int i) const {
    if (i < 0 || i > 2) return 0.0; // 至多 3 个候选槽位
    const double proximity = clean(s[6 + i]);
    // 当前航向：s[10..11] = vel/(1+speed)，模长恰为 s[12]；速度近零对齐取 0。
    const double vx = clean(s[10]);
    const double vy = clean(s[11]);
    const double vlen = std::sqrt(vx * vx + vy * vy);
    double alignment = 0.0;
    if (vlen > 1e-9) {
        const double dx = clean(s[2 * i]);
        const double dy = clean(s[2 * i + 1]);
        const double dlen = std::sqrt(dx * dx + dy * dy);
        if (dlen > 1e-9) alignment = (dx * vx + dy * vy) / (dlen * vlen);
    }
    return p_.wProximity * proximity + p_.wAlignment * alignment;
}

ActionVector HysteresisPolicy::decide(const SensorVector& s, const TargetList& /*targets*/) {
    ActionVector a;
    // (1) 找当帧最高分候选（空槽位跳过；并列先到先得）。
    double bestScore = 0.0;
    int best = -1;
    for (int i = 0; i < 3; ++i) {
        if (clean(s[6 + i]) <= 0.0) continue;
        const double sc = candidateScore(s, i);
        if (best < 0 || sc > bestScore) {
            bestScore = sc;
            best = i;
        }
    }
    if (best < 0) {
        // 无候选：原地等待，不改变滞回记忆。R2：无目标 = 放宽空间（负调制）。
        if (p_.kModFocus != 0.0) a.mod[1] = clampSym1(-p_.kModFocus);
        return a;
    }
    // (2) 滞回：现任仍有效且挑战者优势不足 gap 时保持现任。
    const int cur = lastChoice_;
    if (cur >= 0 && cur <= 2 && clean(s[6 + cur]) > 0.0) {
        if (bestScore <= candidateScore(s, cur) + p_.hysteresisGap) best = cur;
    }
    lastChoice_ = best;
    // (3) 输出：选中候选方向重新归一化；boost 与其接近度挂钩。
    const double dx = clean(s[2 * best]);
    const double dy = clean(s[2 * best + 1]);
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-9) {
        a.dirX = dx / len;
        a.dirY = dy / len;
    }
    const double prox = clean(s[6 + best]);
    a.boost = prox;
    // (4) R2 聚焦调制：目标越近（focus = prox 高）个人空间越大（保持距离、
    // 精准贴身），目标越远个人空间越小（挤进密集区）。k=0 时 mod 恒 0。
    if (p_.kModFocus != 0.0) {
        a.mod[1] = clampSym1(p_.kModFocus * (2.0 * prox - 1.0));
    }
    return a;
}

} // namespace agentsim

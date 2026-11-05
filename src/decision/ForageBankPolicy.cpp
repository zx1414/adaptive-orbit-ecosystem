// agentsim - decision/ForageBankPolicy.cpp
// 两态 + 滞回实现（decide 无 RNG、不读世界，只动自身状态 foraging_）：
//   e = energyProxy(s)（构造与选维理由见头文件）
//   e < forageBelow  -> foraging_ = true  （全力觅食）
//   e > bankAbove    -> foraging_ = false （蓄能）
//   中间带            -> 保持 foraging_   （滞回，防阈值附近抖动）
// 觅食态输出：方向 = 最近食物候选方向 (s[0], s[1]) 归一化（感知契约：候选 0 即
// 最近食物，s[9] 由 cand[0] 算出）；boost = 1.0 冲刺。方向零向量（无候选/全 NaN）
// 时输出 {0,0}，boost 仍为 1.0（觅食意图保留，等待感知恢复）。
// 蓄能态输出：方向 = 当前速度的反方向（停止扩张，见头文件"反推说明"），boost=0。
//
// R2 新通道（点状施力 + 社会调制，全部默认 0 关闭）：
//   · 觅食态 + kPullBoost > 0 + targets.count > 0：对 targets[0]（距离最近的球）
//     施力，方向 = ForceTarget 的 (dx,dy)（核->球最近像，感知层保证环面语义），
//     矢量 = 方向 * (kPullBoost * 1.0)。只有觅食态施力（饥饿才有动机花能量），
//     蓄能态 forceSlot 恒 -1。行为层负责校验 slot/forceRange/扣费——本层只表达意图。
//   · mod[0] 视野调制：觅食态 +kModSight（放大视野找食），蓄能态 -kModSight
//    （收窄视野省心）。
// 非有限输入一律清洗为 0（clean），纯确定性算术，绝不使用任何 RNG（红队检查项）。
#include "decision/ForageBankPolicy.h"

#include <cmath>

namespace agentsim {
namespace {

// 防御性清洗：非有限值（NaN/Inf）一律归 0。
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

} // namespace

ForageBankPolicy::ForageBankPolicy(const Params& p) : p_(p) {
    // 参数清洗：阈值夹到 [0,1]；交叉（below > above）时交换恢复良序；
    // wMargin 夹到 [0,1] 保证能量代理恒 ∈ [0,1]。
    p_.forageBelow = clamp01(clean(p.forageBelow));
    p_.bankAbove = clamp01(clean(p.bankAbove));
    if (p_.forageBelow > p_.bankAbove) {
        const double t = p_.forageBelow;
        p_.forageBelow = p_.bankAbove;
        p_.bankAbove = t;
    }
    p_.wMargin = clamp01(clean(p.wMargin));
    // R2：新参数非负化（强度参数不该是负的；非有限由 clean 归 0）。
    p_.kPullBoost = clean(p.kPullBoost);
    if (p_.kPullBoost < 0.0) p_.kPullBoost = 0.0;
    p_.kModSight = clean(p.kModSight);
    if (p_.kModSight > 1.0) p_.kModSight = 1.0;
    if (p_.kModSight < -1.0) p_.kModSight = -1.0;
}

double ForageBankPolicy::energyProxy(const SensorVector& s) const {
    // 处境余裕：离边界越远越"有底气"（NaN 清洗后仍 clamp01 防御）。
    const double margin = clamp01(clean(s[13]));
    // 节流余裕：速度越低越"省油"（1 - 速度归一化）。
    const double thrift = 1.0 - clamp01(clean(s[12]));
    return p_.wMargin * margin + (1.0 - p_.wMargin) * thrift;
}

ActionVector ForageBankPolicy::decide(const SensorVector& s, const TargetList& targets) {
    ActionVector a;
    // (1) 两态滞回：只有跨过阈值才换态，中间带保持上帧状态。
    const double e = energyProxy(s);
    if (e < p_.forageBelow) {
        foraging_ = true;
    } else if (e > p_.bankAbove) {
        foraging_ = false;
    }
    if (foraging_) {
        // (2) 觅食态：直奔最近食物（候选 0），boost=1.0 冲刺。
        const double dx = clean(s[0]);
        const double dy = clean(s[1]);
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-9) {
            a.dirX = dx / len;
            a.dirY = dy / len;
        }
        a.boost = 1.0;
        // R2 施力：饥饿 -> 把最近的球拉向自己（kPullBoost > 0 且有目标时）。
        // 取 targets[0]（契约：按环面距离升序，[0] 最近）。方向用 ForceTarget
        // 的最近像位移 (dx,dy)（环面语义正确，跨接缝也不会指向"假方向"），
        // 矢量未归一化、长度 = kPullBoost，行为层负责钳制与扣费。
        if (p_.kPullBoost > 0.0 && targets.count > 0) {
            const ForceTarget& t = targets.items[0];
            const double tdx = clean(t.dx);
            const double tdy = clean(t.dy);
            const double tlen = std::sqrt(tdx * tdx + tdy * tdy);
            if (tlen > 1e-9) {
                a.forceSlot = 0; // TargetList 下标，非实体 id（契约）
                a.forceX = (tdx / tlen) * p_.kPullBoost;
                a.forceY = (tdy / tlen) * p_.kPullBoost;
            }
        }
        // R2 视野调制：觅食态放大视野（正调制）。
        if (p_.kModSight != 0.0) a.mod[0] = clampSym1(p_.kModSight);
    } else {
        // (3) 蓄能态：逆速度方向缓行（净位移最小 ≈ 原地蓄能），boost=0。
        const double vx = clean(s[10]);
        const double vy = clean(s[11]);
        const double len = std::sqrt(vx * vx + vy * vy);
        if (len > 1e-9) {
            a.dirX = -vx / len;
            a.dirY = -vy / len;
            a.boost = 0.0;
            // R2 视野调制：蓄能态收窄视野（负调制）；不施力（forceSlot 保持 -1）。
            if (p_.kModSight != 0.0) a.mod[0] = clampSym1(-p_.kModSight);
            return a;
        }
        // (3b) 蓄能态 + 速度近零 = **死锁保护**（P0 修复，2026-10-31）。
        //
        // 【缺陷】蓄能态输出"逆速度方向"，速度为零时退化为零向量 → 本帧不动 →
        // 下一帧速度仍为零 → 代理量 (1-s[12]) 项也恒不变 → **永久静止**。
        // 而核出生时速度本就为 0，且出生点的代理量普遍落在中间带
        //（实测 seed42 的 12 个核全部 e∈[0.316,0.696]，都在 0.3~0.7 之间），
        // 于是保持默认蓄能态 → 立即死锁。表现为 foragebank 与 StubPolicy
        // **逐帧完全相同**（实测 seed42 跑 2000 tick 数值一字不差），策略形同不存在。
        //
        // 【修法】速度近零时**强制转入觅食态**：策略必须保证"能靠自己动起来"。
        // 这不是绕过滞回——滞回在速度非零后照常生效；这只是给两态机制一个
        // 可达的自举（bootstrap）。静止的核既存不了能也吃不到食，比乱动更糟。
        foraging_ = true;
        const double fdx = clean(s[0]);
        const double fdy = clean(s[1]);
        const double dlen = std::sqrt(fdx * fdx + fdy * fdy);
        if (dlen > 1e-9) {
            a.dirX = fdx / dlen;
            a.dirY = fdy / dlen;
        }
        a.boost = 1.0;
        // 自举进觅食态 = 饥饿分支：施力与视野调制与 (2) 一致（保证分支行为对齐）。
        if (p_.kPullBoost > 0.0 && targets.count > 0) {
            const ForceTarget& t = targets.items[0];
            const double tdx = clean(t.dx);
            const double tdy = clean(t.dy);
            const double tlen = std::sqrt(tdx * tdx + tdy * tdy);
            if (tlen > 1e-9) {
                a.forceSlot = 0;
                a.forceX = (tdx / tlen) * p_.kPullBoost;
                a.forceY = (tdy / tlen) * p_.kPullBoost;
            }
        }
        if (p_.kModSight != 0.0) a.mod[0] = clampSym1(p_.kModSight);
    }
    return a;
}

} // namespace agentsim

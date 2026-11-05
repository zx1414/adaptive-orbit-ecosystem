// agentsim - decision/RiskAversePolicy.cpp
// EROI（能量回报率）估计与两阈值三模式状态机（带滞回）。decide() 禁 RNG、禁读世界，
// 只消费感知向量 s 与自身记忆（eroi_/prevProx_/lastDir_/cachedDir_/mode_/frameInMode_）。
//
// EROI 观测（每帧一步，N=32 -> alpha=0.125）：
//   gain   = kFoodEnergy * max(0, s[6] - prevProx)   接近度增益 * 单位食物能量
//            （接近度 ∈ [0,1] 且非负，差值只取正部 -> gain >= 0）
//   budget = (kMoveCostRate * v^2 + kRespRate * (1 + kRespTempK * s[16])) / 32
//            （v = |s[10..11]| 恰为归一化速度 s[12]；移动耗能 ∝ v^2 与
//             AgentBehavior 真实公式同形；呼吸含温度因子 (1 + K*d)）
//   obs    = gain / budget   （budget 下限 1e-6，不产生除零）
//   eroi_  = alpha*obs + (1-alpha)*eroi_     （EMA，eroi_ 起点 0 恒 >= 0）
//   eroi_ *= (1 - kTempDiscount * s[16])     （温度失真折扣，s[16] 必须参与；
//             s[16] 已 clamp01，折扣系数 ∈ [0.5,1] >= 0，eroi_ 仍 >= 0）
//   最后 clamp 到 [0, kEroiMax]（防病态观测爆表，状态保持有界）。
//
// 模式迁移（滞回带 h=0.1，只有完全穿越带宽才算换态，边界抖动被吸收）：
//   Earn 进：eroi_ > eroiHigh + h；Earn 出：eroi_ < eroiHigh - h
//   Conserve 进：eroi_ < eroiLow - h；Conserve 出：eroi_ > eroiLow + h
//   其余落中间带 -> Mid（eroiLow..eroiHigh 内插值 boost）。
//
// 动作输出：
//   Earn：boost=1.0，方向每帧更新 = 最近食物方向 (s[0], s[1]) 归一化。
//   Mid：boost = cruise + (1-cruise) * clamp01((eroi_-eroiLow)/(eroiHigh-eroiLow))。
//   Conserve：boost=0.3，方向每 3 帧刷新一次（mode 内第 1、4、7... 帧），
//     其余帧保持缓存方向；本帧无食物方向则输出零向量（首帧无缓存亦然），
//     绝不拿陈旧方向盲走。
//
// R2 新通道（targets / mod，全部默认 0 = 回归安全）：
//   · 扫路施力（Earn 态 + kPushBoost > 0 + 有目标）：取 targets 里 dist 最小的
//     球（近的优先处理），若球方向与本帧期望方向的点积 >= kPushAlignMin
//    （"基本挡在路上"），沿"球的最近像方向 + 侧向偏置"施力，把球从本帧路径上
//     推开：forceX/forceY = 球方向单位向量 + 0.5 * 期望方向单位向量，长度再缩放
//     到 kPushBoost。EROI 不足（Conserve/Mid）时绝不动球——省电模式不整理环境。
//   · mod：Earn 正调制聚拢/视野（呼朋引伴收割），Conserve 负调制（疏散省心）。
//     mod[2]（斥力）不参与：斥力是碰撞响应，不做意愿调制。
// 非有限输入一律清洗为 0（clean），纯确定性算术，绝不使用任何 RNG（红队检查项）。
#include "decision/RiskAversePolicy.h"

#include <cmath>

namespace agentsim {
namespace {

// 防御性清洗：非有限值（NaN/Inf）一律归 0。
double clean(double x) { return std::isfinite(x) ? x : 0.0; }

double clamp01(double v) {
    if (!(v > 0.0)) return 0.0; // NaN/负值归 0
    if (v > 1.0) return 1.0;
    return v;
}

// clamp 到 [-1,1]（NaN 走第一支归 -1，绝不传播）。
double clampSym1(double x) {
    if (!(x > -1.0)) return -1.0;
    if (x > 1.0) return 1.0;
    return x;
}

// EROI 估计的内部上界（防病态大观测把状态顶爆，恒有 eroi_ <= kEroiMax）。
constexpr double kEroiMax = 64.0;

// 扫路施力的侧向偏置权重：合力 = 球方向 + kPushSide * 期望方向。
constexpr double kPushSide = 0.5;

} // namespace

RiskAversePolicy::RiskAversePolicy(const Params& p) : p_(p) {
    // 参数清洗：全部非负化，非有限值归 0（负 alpha/阈值会破坏状态机单调性）。
    p_.eroiHigh = clean(p.eroiHigh);
    p_.eroiLow = clean(p.eroiLow);
    p_.alpha = clean(p.alpha);
    p_.hysteresis = clean(p.hysteresis);
    p_.cruiseBoost = clean(p.cruiseBoost);
    p_.kFoodEnergy = clean(p.kFoodEnergy);
    p_.kMoveCostRate = clean(p.kMoveCostRate);
    p_.kRespRate = clean(p.kRespRate);
    p_.kRespTempK = clean(p.kRespTempK);
    p_.kTempDiscount = clean(p.kTempDiscount);
    if (p_.eroiHigh < p_.eroiLow) {
        const double t = p_.eroiHigh;
        p_.eroiHigh = p_.eroiLow;
        p_.eroiLow = t;
    }
    if (p_.cruiseBoost < 0.0) p_.cruiseBoost = 0.0;
    if (p_.cruiseBoost > 1.0) p_.cruiseBoost = 1.0;
    if (p_.alpha < 0.0) p_.alpha = 0.0;
    if (p_.alpha > 1.0) p_.alpha = 1.0;
    if (p_.kTempDiscount < 0.0) p_.kTempDiscount = 0.0;
    if (p_.kTempDiscount > 1.0) p_.kTempDiscount = 1.0;
    if (p_.dirRefreshInterval < 1) p_.dirRefreshInterval = 1;
    // R2：新参数清洗（kPushAlignMin 夹到 [0,1]；调制强度夹到 [-1,1]）。
    p_.kPushBoost = clean(p.kPushBoost);
    if (p_.kPushBoost < 0.0) p_.kPushBoost = 0.0;
    p_.kPushAlignMin = clamp01(clean(p.kPushAlignMin));
    p_.kModCohesion = clean(p.kModCohesion);
    if (p_.kModCohesion > 1.0) p_.kModCohesion = 1.0;
    if (p_.kModCohesion < -1.0) p_.kModCohesion = -1.0;
    p_.kModSight = clean(p.kModSight);
    if (p_.kModSight > 1.0) p_.kModSight = 1.0;
    if (p_.kModSight < -1.0) p_.kModSight = -1.0;
}

ActionVector RiskAversePolicy::decide(const SensorVector& s, const TargetList& targets) {
    ActionVector a;

    // ---- 0) 输入清洗（copy-in：不改调用方向量，局部副本全干净）----
    const double prox = clean(s[6]);        // 候选 0 接近度 ∈ [0,1]
    const double d = clamp01(clean(s[16])); // 温度失真 ∈ [0,1]
    const double vx = clean(s[10]);
    const double vy = clean(s[11]);
    const double v2 = vx * vx + vy * vy;    // |s[10..11]| 恰为 s[12]，免再读一维
    const double fdx = clean(s[0]);         // 最近食物方向（感知层已归一化）
    const double fdy = clean(s[1]);

    // ---- 1) EROI 观测与 EMA ----
    const double gain = p_.kFoodEnergy * (prox > prevProx_ ? prox - prevProx_ : 0.0);
    const double budget = (p_.kMoveCostRate * v2 +
                           p_.kRespRate * (1.0 + p_.kRespTempK * d)) /
                          32.0;
    const double obs = gain / (budget > 1e-6 ? budget : 1e-6);
    eroi_ = p_.alpha * obs + (1.0 - p_.alpha) * eroi_;
    // 温度折扣：失真大 -> 能耗抬升 -> EROI 打折（必写项，s[16] 参与决策）。
    eroi_ *= (1.0 - p_.kTempDiscount * d);
    if (!(eroi_ > 0.0)) eroi_ = 0.0; // NaN/负值一律归 0，恒 >= 0
    if (eroi_ > kEroiMax) eroi_ = kEroiMax;
    prevProx_ = prox;

    // ---- 2) 模式迁移（滞回带 0.1）----
    if (mode_ == 2) {
        if (eroi_ < p_.eroiHigh - p_.hysteresis) mode_ = 1;
    } else if (mode_ == 0) {
        if (eroi_ > p_.eroiLow + p_.hysteresis) mode_ = 1;
    } else { // Mid
        if (eroi_ > p_.eroiHigh + p_.hysteresis) {
            mode_ = 2;
        } else if (eroi_ < p_.eroiLow - p_.hysteresis) {
            mode_ = 0;
        }
    }
    if (mode_ != frameInModeMode_) {
        frameInModeMode_ = mode_;
        frameInMode_ = 0;
    }
    ++frameInMode_;

    // ---- 3) 方向与 boost ----
    double dx = 0.0, dy = 0.0;
    const double len = std::sqrt(fdx * fdx + fdy * fdy);
    const bool hasFood = len > 1e-9;
    if (hasFood) {
        lastDirX_ = fdx / len;
        lastDirY_ = fdy / len;
    }

    if (mode_ == 2) {
        // Earn：环境肥沃，全速觅食，方向每帧更新（抓住窗口）。
        if (hasFood) {
            dx = lastDirX_;
            dy = lastDirY_;
        }
        a.boost = 1.0;
    } else if (mode_ == 0) {
        // Conserve：省能巡航，方向每 3 帧只更新一次（减少转向损耗）。
        if (hasFood && frameInMode_ % p_.dirRefreshInterval == 1) {
            hasCachedDir_ = true;
            cachedDirX_ = lastDirX_;
            cachedDirY_ = lastDirY_;
        }
        if (hasFood && hasCachedDir_) {
            dx = cachedDirX_;
            dy = cachedDirY_;
        } // 其余帧输出零向量（无食物/无缓存），绝不拿陈旧方向盲走
        a.boost = p_.cruiseBoost;
    } else {
        // Mid：EROI 线性插值 boost（eroiLow->cruise, eroiHigh->1.0），方向每帧更新。
        double t = 0.0;
        const double span = p_.eroiHigh - p_.eroiLow;
        if (span > 1e-9) {
            t = (eroi_ - p_.eroiLow) / span;
            if (!(t > 0.0)) t = 0.0;
            if (t > 1.0) t = 1.0;
        }
        a.boost = p_.cruiseBoost + (1.0 - p_.cruiseBoost) * t;
        if (hasFood) {
            dx = lastDirX_;
            dy = lastDirY_;
        }
    }
    a.dirX = dx;
    a.dirY = dy;

    // ---- 4) R2 扫路施力（Earn 态 + 有余裕 + 目标挡路才做）----
    // "整理环境"是奢侈行为：只有全速觅食（Earn）时才值得花能量。取距离最近的
    // 球（targets 按环面距离升序），球方向与本帧期望方向点积足够大（挡路）时，
    // 沿"球方向 + 侧向偏置（期望方向的一半）"施力：既有直接推离分量，又有
    // 顺着本帧路径的扫开分量。
    if (mode_ == 2 && p_.kPushBoost > 0.0 && targets.count > 0 && (dx != 0.0 || dy != 0.0)) {
        // 线性扫 dist 最小项（count <= 8，无稳定性需求）。
        int nearest = 0;
        double bestDist = targets.items[0].dist;
        for (int i = 1; i < targets.count; ++i) {
            if (targets.items[i].dist < bestDist) {
                bestDist = targets.items[i].dist;
                nearest = i;
            }
        }
        const ForceTarget& t = targets.items[nearest];
        const double tdx = clean(t.dx);
        const double tdy = clean(t.dy);
        const double tlen = std::sqrt(tdx * tdx + tdy * tdy);
        if (tlen > 1e-9) {
            const double ux = tdx / tlen; // 核->球单位向量（最近像，环面正确）
            const double uy = tdy / tlen;
            const double dlen = std::sqrt(dx * dx + dy * dy);
            if (dlen > 1e-9) {
                const double ex = dx / dlen;
                const double ey = dy / dlen;
                // 挡路判定：球方向与期望方向点积 >= 阈值（同向 = 在路径上）。
                if (ux * ex + uy * ey >= p_.kPushAlignMin) {
                    double fx = ux + kPushSide * ex;
                    double fy = uy + kPushSide * ey;
                    const double flen = std::sqrt(fx * fx + fy * fy);
                    if (flen > 1e-9) {
                        a.forceSlot = nearest;       // TargetList 下标（契约）
                        a.forceX = (fx / flen) * p_.kPushBoost;
                        a.forceY = (fy / flen) * p_.kPushBoost;
                    }
                }
            }
        }
    }

    // ---- 5) R2 社会调制（模式驱动）----
    // Earn：+聚拢 +视野（肥矿区呼朋引伴一起收割）；Conserve：-聚拢 -视野
    // （疏散、少看省心）；Mid：零（中性）。k 默认 0 = mod 全 0。
    if (p_.kModCohesion != 0.0) {
        double m = 0.0;
        if (mode_ == 2) m = p_.kModCohesion;
        else if (mode_ == 0) m = -p_.kModCohesion;
        a.mod[3] = clampSym1(m);
    }
    if (p_.kModSight != 0.0) {
        double m = 0.0;
        if (mode_ == 2) m = p_.kModSight;
        else if (mode_ == 0) m = -p_.kModSight;
        a.mod[0] = clampSym1(m);
    }
    return a;
}

void RiskAversePolicy::reset() {
    eroi_ = 0.0;
    prevProx_ = 0.0;
    lastDirX_ = 0.0;
    lastDirY_ = 0.0;
    hasCachedDir_ = false;
    cachedDirX_ = 0.0;
    cachedDirY_ = 0.0;
    mode_ = 1;
    frameInMode_ = 0;
    frameInModeMode_ = 1;
}

} // namespace agentsim

// agentsim - decision/BanditPolicy.cpp
// T24：BanditPolicy 实现 —— Hedge/EXP3-lite 指数加权两臂混合。
// 红线：decide() 全程确定性算术，绝不使用 RNG；只消费传入的 SensorVector 与
// 自存历史，绝不读世界；不 include environment/ sim/ ledger/（依赖守卫）。
//
// ============================ 回报估计（设计核心） ============================
// 感知向量里没有"我上一步做得对不对"的直接信号，回报由两个互补部分合成，
// 全部只消费：本帧传入感知 s、自存的上一帧感知 prevS_、上一帧自身输出 。
//
// (1) 环境回报 rEnv ∈ [-1,1]（两臂共享的"结果项"：这一步走得值不值）：
//       curProx  = max_j clamp01(s[6+j])       本帧最强食物接近度
//       prevProx = max_j clamp01(prevS_[6+j])  上帧同量（自存历史）
//       gain     = curProx - prevProx          接近度增益：>0 = 上一步走对了
//       speed    = clamp01(s[12])              速度大小归一化（能量代理，
//                                              与 RulePolicy 的 urgency 同源）
//       eff      = speed * curProx             高速且贴近食物 = 高效巡航
//       waste    = speed * (1 - curProx)       高速却远离食物 = 白跑
//       rEnv     = clamp( 0.6*gain + 0.4*(eff - waste), -1, 1 )
//     理由：接近度增益直接回答"离目标更近了吗"；速度-能量项实现 权衡——
//     冲刺只有在确实接近食物时才记正分，白跑记负分。0.6/0.4 固定配比（和为 1，
//     eff-waste ∈ [-1,1]、gain ∈ [-1,1]，故 rEnv 天然落在 [-1,1]，无需额外缩放）。
//
// (2) 每臂信用分配（谁的建议该记功/记过）：
//       u_i^prev = 上一帧臂 i 建议方向的单位向量（零方向 = 弃权）
//       agreement_i = u_i^prev · uOut^prev ∈ [-1,1]
//           实际执行方向里有多少来自臂 i；弃权臂记 0（不记功也不背锅）。
//       potential_i = Σ_j prox_j^prev * max(0, u_i^prev · d_j^prev) / Σ_j prox_j^prev
//           ∈ [0,1]："上帧若沿臂 i 的建议走，对可见食物的期望拉力"
//           （感知势能 = 接近度加权的方向对齐均值）；prevS_ 无候选时取中性 0.5。
//       r_i = clamp( 0.5*rEnv*agreement_i + (potential_i - 0.5), -1, 1 )
//     为什么必须要有 potential 项：对本两臂体系可证
//       agreement_R - agreement_T = (w_R - w_T) * (1 - u_R·u_T)，
//     权重相等时两臂 agreement 恒相等 —— 只凭"结果×同意度"永远打不破
//     0.5/0.5 的对称（第一步就死锁，永不学习）。potential_i 是一个只依赖
//     上帧感知的反事实信号（"沿你的建议走会不会更接近食物"），提供了打破
//     对称所需的每臂独立梯度；结果项 rEnv*agreement 在时间轴上校正它
//     （实际走完一帧后离食物更近/更远、冲刺是否划算）。
//
// ========================= 权重更新（Hedge / EXP3-lite） =========================
//       logW_i += eta * r_i                    （对数空间累加，任意帧数不溢出）
//       w = softmax(logW)（max-shift 数值稳定）
//       w_i = max(w_i, 1e-6) 后重新归一化       （反塌零地板：恒有 w_i ≳ 1e-6）
// 指数加权保证权重恒正、且随回报差指数分离；对数空间实现保证长期运行
//（数万帧）不上溢不下溢；地板保证坏的臂永远不会被彻底丢弃（可恢复性）。
// 输出契约：方向为单位向量或精确零向量，boost ∈ [0,1]，全 NaN/Inf 输入也不
// 产生 NaN 输出（所有输入先经 clean1 清洗，所有中间量夹在有限区间）。
//
// ========================= R2 新通道（targets / mod[3]） =========================
// targets：两臂各自内部不消费 targets（本策略给两臂喂清洗后的 s，不透传
//   targets——臂的施力/调制意图不采纳），外层也不施力：混合两个物理施力矢量
//   没有可解释语义，且会绕过 Hedge 的每臂信用分配（决定不施力是设计决定）。
// mod[3] 探索调制：uncertainty = 4*wR*wT ∈ [0,1]（0.5/0.5 -> 1；单臂 1 -> 0）。
//   决策最不确定时最该凑近同伴收集信息 -> mod[3] = +kModExplore * uncertainty
//   （放大聚拢），权重尖锐化后回归 0。kModExplore 默认 0 = mod 全 0（回归安全）。
#include "decision/BanditPolicy.h"

#include <cmath>

namespace agentsim {
namespace {

// 防御性清洗：非有限值（NaN/Inf）一律归 0 —— 决策层不信任任何输入。
double clean1(double x) { return std::isfinite(x) ? x : 0.0; }

// clamp 到 [0,1]；NaN 归 0。
double clamp01(double x) {
    if (!(x > 0.0)) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

// clamp 到 [-lim, lim]；NaN 走第一支归 -lim（防御性，绝不传播 NaN）。
double clampSym(double x, double lim) {
    if (!(x > -lim)) return -lim;
    if (x > lim) return lim;
    return x;
}

// max(0, x)：势能项只累计"指向食物"的分量。
double posPart(double x) { return x > 0.0 ? x : 0.0; }

// 鲁棒单位化：先按最大分量缩放再求模（防 x*x 溢出为 Inf）。长度 <= 1e-12
// 或输入非有限时输出精确 {0,0}；否则输出模长 1 ± 2e-16 的单位向量。
void unitOrZero(double x, double y, double& ux, double& uy) {
    x = clean1(x);
    y = clean1(y);
    const double ax = x < 0.0 ? -x : x;
    const double ay = y < 0.0 ? -y : y;
    const double m = ax > ay ? ax : ay;
    if (!(m > 1e-12)) { ux = 0.0; uy = 0.0; return; }
    const double xs = x / m;
    const double ys = y / m;
    const double n = std::sqrt(xs * xs + ys * ys);
    ux = xs / n;
    uy = ys / n;
}

// 反塌零地板：归一化后任一权重 >= kWeightFloor/(1+kWeightFloor) ≈ 1e-6。
constexpr double kWeightFloor = 1e-6;
// 单步对数权重贡献上限：防极端 eta 一步把 log 权重推爆（eta 构造时已夹 [0,10]）。
constexpr double kMaxLogStep = 100.0;

} // namespace

BanditPolicy::BanditPolicy(double eta, ThermalPolicy::Params thermalParams, double kModExplore)
    : rule_(),
      thermal_(thermalParams),
      eta_(clean1(eta)),
      kModExplore_(clean1(kModExplore)),
      logWR_(std::log(0.5)),
      logWT_(std::log(0.5)),
      wRule_(0.5),
      wThermal_(0.5),
      hasPrev_(false),
      prevS_(),
      prevARule_(),
      prevAThermal_(),
      prevOut_() {
    if (eta_ < 0.0) eta_ = 0.0;
    if (eta_ > 10.0) eta_ = 10.0;
    if (kModExplore_ > 1.0) kModExplore_ = 1.0;
    if (kModExplore_ < -1.0) kModExplore_ = -1.0;
}

void BanditPolicy::reset() {
    logWR_ = std::log(0.5);
    logWT_ = std::log(0.5);
    wRule_ = 0.5;
    wThermal_ = 0.5;
    hasPrev_ = false;
    prevS_ = SensorVector();
    prevARule_ = ActionVector();
    prevAThermal_ = ActionVector();
    prevOut_ = ActionVector();
}

ActionVector BanditPolicy::decide(const SensorVector& sRaw, const TargetList& /*targets*/) {
    // 0) 输入清洗：非有限 -> 0（绝不把 NaN/Inf 带进任何算术）。
    SensorVector s;
    for (int i = 0; i < SensorVector::kDim; ++i) s[i] = clean1(sRaw[i]);

    // 1) 两臂各出一招（子策略均为零 RNG 确定性策略；targets 不透传——见文件头
    //    "R2 新通道"说明：施力意图不做混合，保持每臂信用分配的纯净）。
    const ActionVector ar = rule_.decide(s);
    const ActionVector at = thermal_.decide(s);

    // 2) 臂方向单位化（零方向 = 弃权），按当前权重线性混合，再单位化输出。
    double uxR, uyR, uxT, uyT;
    unitOrZero(ar.dirX, ar.dirY, uxR, uyR);
    unitOrZero(at.dirX, at.dirY, uxT, uyT);

    ActionVector out;
    unitOrZero(wRule_ * uxR + wThermal_ * uxT,
               wRule_ * uyR + wThermal_ * uyT,
               out.dirX, out.dirY);
    out.boost = clamp01(wRule_ * ar.boost + wThermal_ * at.boost);

    // 3) 回报估计 + Hedge 更新（首帧无历史 -> 跳过；公式见文件头注释）。
    if (hasPrev_) {
        double curProx = 0.0;
        double prevProx = 0.0;
        for (int i = 0; i < 3; ++i) {
            const double c = clamp01(s[6 + i]);
            if (c > curProx) curProx = c;
            const double p = clamp01(prevS_[6 + i]);
            if (p > prevProx) prevProx = p;
        }
        const double speed = clamp01(s[12]);
        const double gain = curProx - prevProx;
        const double rEnv = clampSym(0.6 * gain + 0.4 * speed * (2.0 * curProx - 1.0), 1.0);

        double puRx, puRy, puTx, puTy;
        unitOrZero(prevARule_.dirX, prevARule_.dirY, puRx, puRy);
        unitOrZero(prevAThermal_.dirX, prevAThermal_.dirY, puTx, puTy);
        // 信用分配以上一帧的"实际执行方向"为基准（弃权臂/零输出自动记 0）。
        const double agreeR = clampSym(puRx * prevOut_.dirX + puRy * prevOut_.dirY, 1.0);
        const double agreeT = clampSym(puTx * prevOut_.dirX + puTy * prevOut_.dirY, 1.0);

        double sumProx = 0.0;
        for (int i = 0; i < 3; ++i) sumProx += clamp01(prevS_[6 + i]);
        double potR = 0.5; // 无候选 -> 中性势能（不奖不罚）
        double potT = 0.5;
        if (sumProx > 1e-12) {
            double accR = 0.0;
            double accT = 0.0;
            for (int i = 0; i < 3; ++i) {
                const double pj = clamp01(prevS_[6 + i]);
                if (!(pj > 0.0)) continue;
                double djx, djy;
                unitOrZero(prevS_[2 * i], prevS_[2 * i + 1], djx, djy);
                accR += pj * posPart(puRx * djx + puRy * djy);
                accT += pj * posPart(puTx * djx + puTy * djy);
            }
            potR = accR / sumProx;
            potT = accT / sumProx;
        }

        const double rR = clampSym(0.5 * rEnv * agreeR + (potR - 0.5), 1.0);
        const double rT = clampSym(0.5 * rEnv * agreeT + (potT - 0.5), 1.0);

        // Hedge 指数加权（对数空间）：logW += eta * r。
        logWR_ += clampSym(eta_ * rR, kMaxLogStep);
        logWT_ += clampSym(eta_ * rT, kMaxLogStep);

        // softmax（max-shift 数值稳定）+ 反塌零地板 + 重归一化。
        const double m = logWR_ > logWT_ ? logWR_ : logWT_;
        double pR = std::exp(logWR_ - m);
        double pT = std::exp(logWT_ - m);
        double z = pR + pT;
        wRule_ = pR / z;
        wThermal_ = pT / z;
        if (wRule_ < kWeightFloor) wRule_ = kWeightFloor;
        if (wThermal_ < kWeightFloor) wThermal_ = kWeightFloor;
        z = wRule_ + wThermal_;
        wRule_ /= z;
        wThermal_ /= z;
    }

    // 4) 滚动历史（下一帧的回报要用）。hasPrev_ 置位后，回报估计 + Hedge
    // 更新从下一帧开始真实运转（缺了这行权重将永久冻结在 0.5/0.5——R2-C 实测踩坑）。
    prevS_ = s;
    prevARule_ = ar;
    prevAThermal_ = at;
    prevOut_ = out;
    hasPrev_ = true;

    // 5) R2 探索调制：决策不确定度 -> mod[3]（聚拢）。uncertainty = 4*wR*wT，
    // 0.5/0.5 时 = 1（最不确定、最该收集信息），权重尖锐后趋 0。k 默认 0 = 关闭。
    if (kModExplore_ != 0.0) {
        const double uncertainty = 4.0 * wRule_ * wThermal_;
        out.mod[3] = clampSym(kModExplore_ * uncertainty, 1.0);
    }
    // forceSlot 保持 -1：本策略不施力（设计决定，见文件头）。
    return out;
}

} // namespace agentsim

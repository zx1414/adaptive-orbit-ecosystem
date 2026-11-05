// agentsim - decision/ThermalPolicy.h
// 趋温策略（第三个可选策略，与 rule / perceptron 互补）：
// 温度是资源品质的代理信号。RulePolicy 只看距离+对齐，PerceptronPolicy 是黑箱权重；
// ThermalPolicy 显式消费 s[16]（温度失真度）与 s[17]（归一化温度采样）做可解释推理。
// 传感器契约（perception/AgentSensors.cpp）：s[16]/s[17] 是"自身所在点"的标量，
// 因此温度项是全局置信项（调制追击与否 + boost），候选间差异仍由接近度决定。
//
// R2 生态位假设：一个"温水慢爬者"——它把温度适宜度当成资源品质的代理：宁可绕开
// 失真/贫瘠的热区，也要留在自己代谢最优的温带。它活得过环境剧变（温度门控让它
// 在噪声区不乱动），但会被高失真地形拖慢到饿死；食物只在温带出现时它是最优解。
//   R2 通道用法（本策略全部消费，但全部保持"低调"）：
//   · mod[] —— 用自己的"舒适度"调制社会参数：舒适时收窄视野半径并放大个人空间
//     （独行慢爬、不聚集），失真/失适时反转（视野放大找同伴/资源，个人空间收窄
//     抱团取暖）。每个 mod 有独立强度参数，全部默认 0 = 关闭（回归安全）。
//   · 不点名施力（慢爬者不花能量搬球，理由：boost 已被失真门控，施力意图与其
//     保守性格冲突）。
// 红线：决策期绝不使用 RNG，不 include environment/ sim/ ledger/。
#pragma once
#include "decision/IDecisionPolicy.h"

namespace agentsim {

class ThermalPolicy : public IDecisionPolicy {
public:
    struct Params {
        double wProximity = 1.0;    // 距离项权重：越近分越高
        double wThermal = 1.2;      // 温度适宜度权重：越接近最适温度分越高
        double wCaution = 0.8;      // 失真规避权重：失真越大越谨慎
        double preferredTemp = 0.5; // 最适温度（对应归一化采样 s[17] ∈ [0,1]）
        // ---- R2 社会调制（表型可塑，基因不改写；行为层按 eff=base*(1+0.4*mod) 消费）----
        // 舒适度 suitability 高（温和不失真）时的社会调制；低/失真时自动取反。
        // 全部默认 0：不传参数 = 行为与旧版逐位一致（mod 全 0）。
        double kModSight = 0.0;   // 舒适时视野半径调制强度（负 = 收窄）
        double kModSpace = 0.0;   // 舒适时个人空间调制强度（正 = 放大）
        double kModCohesion = 0.0;// 舒适时聚拢倾向调制强度（负 = 独行）
    };
    explicit ThermalPolicy(const Params& p = {});

    // 纯确定性算术，绝不使用 RNG；全 NaN 输入也不产生 NaN 输出。
    ActionVector decide(const SensorVector& s, const TargetList& targets) override;
    using IDecisionPolicy::decide; // 旧单参调用面（基类非虚重载）

    const char* name() const override { return "thermal"; }

    // ---- 可解释性接口（也供单测直接验证各项生效）----
    // 温度适宜度：1 - |s[17]-preferredTemp|/0.5，clamp 到 [0,1]（NaN 归 0）。
    double thermalSuitability(const SensorVector& s) const;
    // 失真规避因子：1 - clamp01(s[16])，失真越大越小。
    double cautionFactor(const SensorVector& s) const;
    // 候选 i 得分（候选级信任调制式，与 ThermalPolicy.cpp 实现同步）：
    //   doubt     = 0.5 * wCaution * (1 - cautionFactor(s))               (>= 0)
    //   trustFood = 1/(1+doubt) ∈ (0,1]；trustStay = 1+doubt >= 1（均恒正）
    //   score     = wProximity*s[6+i] * trustFood
    //             + wThermal * suitability*(0.5+0.5*stay_i) * trustStay
    //   其中 stay_i = 候选方向 i 与当前速度方向的夹角余量 ∈ [-1,1]（速度为 0 取 0）。
    double candidateScore(const SensorVector& s, int i) const;
    // R2：mod[4] 调制输出（可解释性接口；decide 内部同一函数）。
    // comfort = suitability 失真加权（舒适时为正，失真/失适时为负），∈ [-1,1]。
    double comfort(const SensorVector& s) const;

private:
    Params p_;
};

} // namespace agentsim

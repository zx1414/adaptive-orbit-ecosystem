// agentsim - decision/RiskAversePolicy.h
// 风险规避 + 能量预算策略（用户授权的策略创新第 5 条）。
// 动机：现有策略都不"记账"——它们不知道自己为移动花了多少能量。本策略把
// 能量回报率（EROI, Energy Return On Investment）显式纳入决策：
//
//   eroi = (接近度增益 * 单位食物能量) / (移动耗能 + 呼吸)
//
// 维护 eroi_ 的 EMA 滑动平均（有效窗口 N=32 -> alpha=2/(N+1)=0.125）：
//   每帧观测 gain = kFoodEnergy * max(0, s[6]_t - s[6]_{t-1})
//                 （单位食物能量取 Config FoodConfig.foodEnergy=10 的真实语义；
//                   接近度增益 = 觅食回报的代理：越接近食物，吃到的期望越大）
//   每帧预算 cost = (kMoveCostRate * v^2 + kRespRate * (1 + kRespTempK * s[16])) / N
//                 （移动耗能 ∝ speed^2 与 AgentBehavior 真实公式同形；呼吸按
//                   AgentBehavior 的温度因子 (1 + respirationTempK * d) 计入，
//                   s[16] 温度失真抬高能耗 -> 观测 eroi 按比例打折）
//   sm = alpha * (gain / cost) + (1 - alpha) * sm   （每帧一步 EMA，确定性无 RNG）
//   温度折扣：sm *= (1 - 0.5 * s[16])  （s[16] 必须参与，失真大时 EROI 直接打折）
//   输出前 clamp 到 [0, 内部上界]（不可能是负值；eroi_ 恒 >= 0 是可测不变量）。
//
// 决策规则（滞回带 0.1，防阈值边界抖动）：
//   · mode=Earn（eroi_ > eroiHigh=1.2 进入）：环境肥沃，boost=1.0 全速觅食，
//     方向 = 最近食物方向 (s[0], s[1])，每帧更新（抓住窗口）。
//   · mode=Conserve（eroi_ < eroiLow=0.6 进入）：环境贫瘠，省能巡航：
//     boost=0.3，方向仍指向最近食物但每 3 帧只更新一次方向（减少转向损耗）。
//   · 中间带：boost 沿 eroi_ 线性插值（eroiLow -> 0.3，eroiHigh -> 1.0），
//     方向每帧更新；模式切换只在穿越滞回带外的阈值时发生。
//
// R2 生态位假设：一个"精打细算的会计师"——它只关心投入产出比：回报率高的
// 地段全速抢收（Earn），回报率崩塌的地段立刻进入省电模式（Conserve）。它是
// 资源枯竭期最后倒下的策略（省电模式几乎不亏能量），但也是丰产期吃得最少的
//（插值 boost 永远比纯贪心保守）。R2 通道用法（两个都占满）：
//   · forceSlot：**推开挡路的球**——只在 Earn/施力阈值之上才有余裕做这种"环境
//     整理"：取 TargetList 里 dist 最小的球，若它基本挡在本帧期望方向上
//    （dot(球方向, 期望方向) 越大越挡路），沿"球远离自己"的镜像方向施加
//     侧向偏置加速度，把路扫开。EROI 不足时绝不动球（Conserve/Mid 不施力）。
//   · mod[]：Earn 态正调制聚拢（mod[3]，富矿区值得呼朋引伴一起收割）；
//     Conserve 态负调制聚拢 + 负调制视野（疏散 + 少看省心）。强度可配，默认 0。
//
// decide() 禁 RNG、禁读世界；全部状态只有 eroi_、lastDirX/Y（含 Conserve 低频
// 刷新缓存 cachedDir*）、prevProx_、mode_、frameInMode_。非有限输入一律清洗为 0，
// 全 NaN 输入也不产生 NaN 输出。纯确定性算术，绝不 include environment/ sim/
// ledger/（红线 S4）。
#pragma once
#include "decision/IDecisionPolicy.h"

namespace agentsim {

class RiskAversePolicy : public IDecisionPolicy {
public:
    struct Params {
        double eroiHigh = 1.2;          // 高于此值（含滞回）-> Earn 全速觅食
        double eroiLow = 0.6;           // 低于此值（含滞回）-> Conserve 省能巡航
        double alpha = 0.125;           // EMA 系数：有效窗口 N=32 -> 2/(N+1)
        double hysteresis = 0.1;        // 阈值穿越滞回带宽度
        double cruiseBoost = 0.3;       // Conserve 巡航冲刺意愿
        int dirRefreshInterval = 3;     // Conserve 态每 3 帧才更新一次方向
        double kFoodEnergy = 10.0;      // 单位食物能量（Config FoodConfig.foodEnergy）
        double kMoveCostRate = 1.0;     // 移动耗能率（与真实公式 speed^2 同形，量纲自洽）
        double kRespRate = 0.05;        // 基础呼吸率（每帧）
        double kRespTempK = 2.0;        // 呼吸温度因子 K（与 AgentBehavior respirationTempK 同形）
        double kTempDiscount = 0.5;     // 温度失真 EROI 折扣：eroi_ *= (1 - k*s[16])
        // ---- R2 新增（默认 0 = 回归安全）----
        double kPushBoost = 0.0;        // Earn 态"扫路"施力强度（>0 时 forceSlot 生效）
        double kPushAlignMin = 0.3;     // 球方向与期望方向的点积阈值（挡路判定下限）
        double kModCohesion = 0.0;      // 聚拢调制强度（Earn +、Conserve -）
        double kModSight = 0.0;         // 视野调制强度（Earn +、Conserve -）
    };
    explicit RiskAversePolicy(const Params& p = {});

    // 纯确定性；非有限输入清洗为 0，全 NaN 输入也不产生 NaN 输出。
    ActionVector decide(const SensorVector& s, const TargetList& targets) override;
    using IDecisionPolicy::decide; // 旧单参调用面（基类非虚重载）
    const char* name() const override { return "riskaverse"; }

    // ---- 可解释性接口（供单测验证 EROI 状态机）----
    // 当前 EROI 估计（EMA 后含温度折扣，恒 >= 0）。
    double eroi() const { return eroi_; }
    // 当前模式：0=Conserve（省能巡航），1=Mid（插值），2=Earn（全速觅食）。
    int mode() const { return mode_; }
    // 当前模式内已持续帧数（decide 内自增，首帧调用后 = 1）。
    int frameInMode() const { return frameInMode_; }
    // 清空全部记忆（单测确定性验证用）。
    void reset();

private:
    Params p_;
    double eroi_ = 0.0;        // EMA 后含温度折扣的 EROI 估计，恒 >= 0
    double prevProx_ = 0.0;    // 上一帧候选 0 接近度（增益 = 本帧 - 上帧，只取正部）
    double lastDirX_ = 0.0;    // 最近一次有效的食物方向（每帧维护）
    double lastDirY_ = 0.0;
    double cachedDirX_ = 0.0;  // Conserve 态低频刷新的方向缓存
    double cachedDirY_ = 0.0;
    bool hasCachedDir_ = false;// Conserve 缓存是否已建立
    int mode_ = 1;             // 0=Conserve 1=Mid 2=Earn；中性 Mid 起步（保守并不消极）
    int frameInMode_ = 0;      // 当前模式内帧计数（迁移时清零重计）
    int frameInModeMode_ = 1;  // frameInMode_ 所属模式
};

} // namespace agentsim

// agentsim - perception/AgentSensors.h
// 感知层：把环境只读快照压缩成 24 维 SensorVector。纯函数，不写任何状态。
// G1 修复：向量类型来自中立头 core/SensorTypes.h，探测层不再 include 决策层头文件，
// 依赖链恢复单向：environment -> perception -> decision（GOAL G2 / S4）。
//
// R1-B（蓝细菌取代食物）：Food 实体已不存在，本层"食物特征"维 [0..9] 的语义
// 改为 **蓝细菌节点特征**（最近的蓝细菌节点 = 唯一食物来源）：
//   s[0..5] 最近 3 个蓝细菌节点的相对环距方向（单位向量）
//   s[6..8] 对应归一化接近度 1 - dist/R
//   s[9]    最近蓝细菌节点的归一化距离 dist/R
// 维数、噪声模型、抽取次数与 rng 消耗序与旧食物版逐位同构（候选/噪声按同一
// 顺序抽取），决策层（Rule/Thermal/Bandit/RiskAverse）零改动即保持语义。
//
// S17 感知来源契约（用户明确纠正过的语义，不可回退）：
//   侦察球（Scout）是菌落对外感知的唯一感知来源；资源球/护盾球对感知【零影响】：
//   不提供感知来源，也不产生任何噪声惩罚。因此 sense() 签名里【没有】任何
//   resource/resourceCount/ballCount 形参——资源球数量变化时 18 维输出逐位不变
//   是构造性成立（没有入口就不可能被消费），并有单测回归保护
//   （tests/unit/test_sensors.cpp: sense_resource_balls_have_no_parameter）。
//   scoutCount 只表达侦察球数量；<=0（无侦察球）时退到"原生盲区"=
//   senseRadius 的 1/4（核保留最基本的接触感知，不至于完全瞎），而非 0。
#pragma once
#include "core/Vec2.h"
#include "core/Config.h"
#include "core/SensorTypes.h"
#include "environment/AgentEnvironment.h"
#include "environment/CyanoNode.h"
#include "environment/TemperatureField.h"
#include "environment/Terrain.h"

namespace agentsim {

class PCG32;

// R2：最近邻核信息包（s[18..20] 的来源）。组合根扫一遍邻居后填好传入；
// 感知层只做归一化与哨兵处理，不做任何查找（保持纯函数、零内部状态）。
//   found        是否存在视野内最近邻（false 时 s[18]=1、s[19]=s[20]=0）
//   dist         核间环面距离（世界单位）
//   dirX/dirY    self -> 邻居 的环面位移单位方向（跨接缝语义正确，组合根负责）
//   sightRadius  归一化分母 = 该核的 nucleusSightRadius 基因（s[18] = dist/sightRadius，
//                >1 即视野外；1 = 无邻居或恰在视野边缘外）。默认 1.0 保证
//                "无信息"时除法安全。
struct NeighborInfo {
    bool found = false;
    double dist = 0.0;     // 环面距离
    double dirX = 0.0;     // 单位方向
    double dirY = 0.0;
    double sightRadius = 1.0;  // 归一化分母
};

// S17：有效感知半径（公开纯函数，供决策/单测直测）。
// scoutCount<=0：baseRadius * 0.25（原生盲区）；否则 baseRadius * sqrt(1+n)
//（边际递减扩张），上限 baseRadius * 4。资源球在此函数中没有入口。
double effectiveSenseRadius(double baseRadius, int scoutCount);

// 纯函数：只读环境，不写任何状态。
// 读噪声幅度 = readNoiseK * speed / (1 + speed)，由传入 rng 产生（rng 可为 nullptr 表示无噪声）。
// G2 修复：读噪声叠加到有语义的维（候选方向 s[2i]/s[2i+1] 与接近度 s[6+i]），
// s[14] 固定写"本帧读噪声幅度 amp"（元信息维，非随机数），s[15] 恒 0 保留。
// GOAL 温度接线：temp 为末位参数，默认实参 = 默认构造 TemperatureField
//（无源平场：distortion/sample 恒 0，等价旧 16 维行为，旧 6 参调用方不用改）。
// s[16] = temp.distortion(self) ∈ [0,1]，s[17] = temp.sample(self) ∈ [0,1]；
// 温度失真放大读噪声：amp *= (1 + kTempGain * s[16])，speed=0 且 d=0 时 amp 仍为 0。
// T43：terrain 为末位参数，默认 nullptr = 无地形（s[16]/s[17] 行为与原来逐位一致）。
// 有地形时 s[17] 写 clamp01(temp.sample(self) + terrain->temperatureOffset(self))。
// 取舍：s[16] 仍写 temp.distortion(self)（温度场失真只能由 TemperatureField 计算，
// 地形温度区只做采样偏移、不改温度场结构，故不失真路径保持原语义）。
// S17：scoutCount 为末位参数（默认 0 = 无侦察球）。查询半径与归一化分母一律用
// effectiveSenseRadius(cfg.senseRadius, scoutCount)，不再直接用 cfg.senseRadius。
// P2-8：s[9] 的读噪声独立抽取（不再复用候选 0 的偏移，见 cpp 文件头注释）。
// R1-B：环境引用为 AgentEnvironment（环面几何 + SimConfig 只读），蓝细菌场由
// 调用方（组合根）以 cyanoNodes 形参显式传入——感知层只读节点容器，不持有它。
// R1-B 注：cyanoNodes 为默认空容器 = "原生盲区外无候选"（旧 6 参调用方零改动，
// 单测的构造性断言直接覆盖该路径）。调用方传入 sim.cyanoNodes() 时即为生产语义。
//
// R2 追加（V2-SPEC-AMEND-1，末位形参、全部带默认值，旧调用点零改动）：
//   nearest      最近邻核信息（填 s[18..20]，见 SensorTypes.h 维度契约）：
//                s[18] = dist/sightRadius（clamp [0,1]；无邻居恒 1 = "视野内无同伴"）
//                s[19]/s[20] = 单位方向 x/y（无邻居恒 0 —— **方向零向量即"无邻居"
//                哨兵**，决策层据此区分"没有邻居"与"邻居在正方向"）
//   energyRatio  自身能量/容量 ∈ [0,1]（填 s[21]，clamp01 + 非有限清洗；
//                容量 <= 0 由调用方传 0 —— 感知层不做能量策略，只做数值归一）
//   targets      可施力球清单（填 s[22..23]）：
//                s[22] = count / kMaxForceTargets（恒 ∈ [0,1]，0 = 视野内无球）
//                s[23] = items[0].dist / R（clamp [0,1]；无目标恒 1 = "没有可施力球"）
//                分母用 R（有效感知半径）而非裸 senseRadius：与 s[9] 的归一化
//                同口径（S17 契约"归一化分母一律用有效半径"），且 scoutCount=0
//                的原生盲区下数值语义自洽。与 SensorTypes.h 注释中 "senseRadius"
//                的差异在此说明；两口径在有侦察球时数值不同但单调等价。
// 归一化方式（R2 六维，全部恒 ∈ [0,1] / 方向维 ∈ [-1,1]）：
//   s[18] 邻居距离：dist / sightRadius（基因视距做分母 = "占视距的比例"），
//         clamp01；!found 恒 1（哨兵：没有信息 = 当作看不见）。
//   s[19..20] 邻居方向：组合根给定的环面单位方向，感知层再防御性归一化
//         （长度 <= 1e-12 视为零向量 = 无邻居哨兵）；!found 恒 0。
//   s[21] 能量比：clamp01(energyRatio)（容量归一在组合根完成）。
//   s[22] 目标密度：count / kMaxForceTargets=8。
//   s[23] 最近目标距离：items[0].dist / R，clamp01；count==0 恒 1。
//   s[0..17] 逐位不变（回归安全）：本函数对 rng 的消耗序不变、对 s[0..17] 的
//   写入序不变，R2 六维全部在尾部追加。
SensorVector sense(const AgentEnvironment& env, Vec2 self, Vec2 vel, double speed,
                   const PolicyConfig& cfg, PCG32* rng,
                   const std::vector<CyanoNode>& cyanoNodes = std::vector<CyanoNode>{},
                   const TemperatureField& temp = TemperatureField{},
                   const Terrain* terrain = nullptr,
                   int scoutCount = 0,
                   const NeighborInfo& nearest = NeighborInfo{},   // s[18..20]
                   double energyRatio = 0.0,                        // s[21]
                   const TargetList& targets = TargetList{});       // s[22..23]

} // namespace agentsim

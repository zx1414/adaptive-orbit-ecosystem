// agentsim - core/Genome.h
// 核基因组模块（任务卡 R1-A，经 R1-AMEND-1 修订）。
// 把旧版 v2.3.1 的 22 参数遗传系统 + 3 个蓝细菌参数 + 遗传色相重写为 agentsim
// 自己的独立可测模块；AMEND-1 删除战斗/避战四参数（attack*/avoid* 整链路），
// 新增对核社会参数 4 项（nucleus*）与施力能力 2 项（forceRange/forceCostK），
// 参数总数 22 -> 24（另有遗传色相 hue，不计入参数）。
//
// 纯数据 + 自由函数；除 PCG32（仅 Genome.cpp 内使用）外零依赖，任何层都可 include。
// 确定性契约：所有消耗 RNG 的函数拥有固定抽取顺序与固定次数，见 Genome.cpp
// 顶部「RNG 消耗序」注释；同 seed 双跑逐字节可复现。
//
// 语义备忘（AMEND-1 §3，接线时必须遵守）：
//   - nucleus* 四参数是「对其他核的社会参数」，不是战斗参数：物理上只产生
//     斥力 / 聚拢 / 硬分离三种作用，不发生任何能量转移；
//   - nucleusCohesion = 0 表示独行（只排斥不聚拢），其 Clamp 下限因此是 0；
//   - forceRange / forceCostK：核能对多远的球施力、每单位加速度平方花多少能量
//     （代价 cost = forceCostK * |a|^2，接线时走 ledger.dissipate("applyForce")）。
#pragma once

namespace agentsim {

class PCG32;  // 前向声明；Genome.cpp 内 include "PCG32.h"

// 球类型下标数：0=Resource 1=Scout 2=Shield（顺序与 behavior/AgentBehavior.h 的
// enum class BallType 对齐，接线时不得重排）。
constexpr int kBallTypeCount = 3;

// 遗传参数的随机初始化（Init）与变异裁剪（Clamp）范围，全部字段给默认值。
struct GenomeRanges {
    // —— 四个按类型数组 ——
    double affinityInitMin = 0.1, affinityInitMax = 1.0;   // 归一化前的原始抽取区间
    double affinityClampMin = 1e-4, affinityClampMax = 10.0;
    double orbitRadiusInitMin = 30.0, orbitRadiusInitMax = 200.0;
    double orbitRadiusClampMin = 10.0, orbitRadiusClampMax = 300.0;
    double absorbPreferenceInitMin = 0.5, absorbPreferenceInitMax = 1.5;
    double absorbPreferenceClampMin = 0.1, absorbPreferenceClampMax = 3.0;
    double repelStrengthInitMin = 0.5, repelStrengthInitMax = 3.0;
    double repelStrengthClampMin = 0.1, repelStrengthClampMax = 6.0;

    // —— 自身机动（AMEND-1：attack/avoid 四参数整链路删除）——
    double maxSpeedInitMin = 30.0, maxSpeedInitMax = 100.0;
    double maxSpeedClampMin = 5.0, maxSpeedClampMax = 300.0;
    double maxAccelInitMin = 1.0, maxAccelInitMax = 20.0;
    double maxAccelClampMin = 0.5, maxAccelClampMax = 50.0;
    double energyThresholdInitMin = 80.0, energyThresholdInitMax = 200.0;
    double energyThresholdClampMin = 20.0, energyThresholdClampMax = 1000.0;
    double mutationRateInitMin = 0.01, mutationRateInitMax = 0.2;
    double mutationRateClampMin = 0.001, mutationRateClampMax = 0.5;

    // —— 蓝细菌（新生态里是核的唯一食物，V2-SPEC §E）——
    double cyanoPreferenceInitMin = 0.0, cyanoPreferenceInitMax = 1.0;  // 0 = 完全不捕食
    double cyanoPreferenceClampMin = 0.0, cyanoPreferenceClampMax = 1.0;
    double cyanoDigestionInitMin = 0.1, cyanoDigestionInitMax = 1.0;
    double cyanoDigestionClampMin = 0.05, cyanoDigestionClampMax = 1.0;

    // —— 对核社会参数（AMEND-1；非战斗，零能量转移）——
    double nucleusSightRadiusInitMin = 40.0, nucleusSightRadiusInitMax = 300.0;
    double nucleusSightRadiusClampMin = 10.0, nucleusSightRadiusClampMax = 600.0;
    double nucleusPersonalSpaceInitMin = 15.0, nucleusPersonalSpaceInitMax = 80.0;
    double nucleusPersonalSpaceClampMin = 5.0, nucleusPersonalSpaceClampMax = 200.0;
    double nucleusRepelStrengthInitMin = 0.5, nucleusRepelStrengthInitMax = 6.0;
    double nucleusRepelStrengthClampMin = 0.1, nucleusRepelStrengthClampMax = 20.0;
    double nucleusCohesionInitMin = 0.0, nucleusCohesionInitMax = 3.0;  // 0 = 独行
    double nucleusCohesionClampMin = 0.0, nucleusCohesionClampMax = 10.0;

    // —— 施力能力（AMEND-1）——
    double forceRangeInitMin = 30.0, forceRangeInitMax = 150.0;
    double forceRangeClampMin = 10.0, forceRangeClampMax = 400.0;
    double forceCostKInitMin = 0.005, forceCostKInitMax = 0.05;
    double forceCostKClampMin = 1e-4, forceCostKClampMax = 1.0;

    // —— 遗传色相 ——
    double hueInitMin = 0.0, hueInitMax = 360.0;
    double hueJitterSigma = 8.0;  // 子代色相高斯扰动标准差（度）
};

// 核的可遗传基因组：24 个实数参数 + 遗传色相。
// 字段声明顺序 = 随机化/变异的固定遍历顺序（数组按下标 0..2、标量按声明序），
// 不得重排，否则破坏同 seed 双跑确定性。
// 默认成员初始化为「旧版默认值 + 新参数 Init 区间中点」，保证默认构造的 Genome
// 合法：全部字段落在 Clamp 区间、affinity 和为 1（浮点下相差 <1ulp）、hue 在 [0,360)。
struct Genome {
    double affinity[kBallTypeCount] = {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0};  // 和为 1
    double orbitRadius[kBallTypeCount] = {120.0, 120.0, 120.0};           // Init 中点
    double absorbPreference[kBallTypeCount] = {1.0, 1.0, 1.0};            // Init 中点
    double repelStrength[kBallTypeCount] = {1.5, 1.5, 1.5};            // Init 中点

    double maxSpeed = 65.0;          // 旧版默认值
    double maxAccel = 10.0;           // 旧版默认值
    double energyThreshold = 140.0;  // 旧版默认值
    double mutationRate = 0.1;      // 旧版默认值
    double cyanoPreference = 0.5;    // 旧版字面默认 0.0 是「关闭蓝细菌」哨兵值；新生态
                                     // 蓝细菌是唯一食物，取 Init 中性中点 0.5（见报告）
    double cyanoDigestion = 0.55;     // 旧版默认值
    double nucleusSightRadius = 170.0;   // Init 中点 40..300
    double nucleusPersonalSpace = 47.0;  // Init 中点 15..80
    double nucleusRepelStrength = 3.0;  // Init 中点 0.5..6
    double nucleusCohesion = 1.5;        // Init 中点 0..3（>0：默认有聚拢倾向）
    double forceRange = 90.0;            // Init 中点 30..150
    double forceCostK = 0.027;          // Init 中点 0.005..0.05

    double hue = 0.0;  // [0,360)，Init 区间中点
};

// 在 [InitMin, InitMax) 内均匀随机初始化全部字段。affinity 是原始抽取、不在此处
// 归一化（归一化会把分量压到 affinityInitMin 之下，破坏「randomize 后落在 Init
// 区间」契约）；需要和为 1 的调用方（行为层出生逻辑）在 randomize 之后调用
// genomeNormalizeAffinity。
// RNG 消耗：恰好 25 次 nextDouble()，顺序见 Genome.cpp。
void genomeRandomize(Genome& g, PCG32& rng, const GenomeRanges& r);

// 把全部字段钳到 Clamp 区间；hue 例外：环绕到 [0,360) 而不是钳。
// 不消耗 RNG；不负责 affinity 归一化（那是 genomeNormalizeAffinity 的职责）。
void genomeClamp(Genome& g, const GenomeRanges& r);

// affinity 归一化：先把越界分量钳到 [affinityClampMin, affinityClampMax]（负数先钳
// 下限，任务卡要求），再除以总和使三项目和为 1；钳后总和仍 <= 0 或非有限（仅当
// 配置 affinityClampMin <= 0 才可能）时均分为 1/kBallTypeCount。
// 注意：归一化后单个分量可能低于 affinityClampMin（除法所致，规格允许），因此
// 调用序必须是 … -> genomeClamp -> genomeNormalizeAffinity，之后不得再 clamp affinity。
// 不消耗 RNG。
void genomeNormalizeAffinity(Genome& g, const GenomeRanges& r);

// 变异（子代出生时由行为层调用）：对每个数组项与标量（含 mutationRate 自身）加
//   delta = gauss01 * (mutationRate * (clampMax - clampMin)) * coeff
// 的加性高斯扰动；mutationRate 自身也被扰动（否则进化卡死在初始值），越界由收尾
// 的 genomeClamp 钳回其 Clamp 区间。色相单独处理：
//   hue += gauss01 * hueJitterSigma * coeff（不乘 mutationRate），随后环绕到 [0,360)。
// 收尾固定执行 genomeClamp + genomeNormalizeAffinity（affinity 和回到 1）。
// RNG 消耗：恰好 25 个高斯 = 50 次 nextDouble()，顺序见 Genome.cpp。
void genomeMutate(Genome& g, PCG32& rng, double coeff, const GenomeRanges& r);

} // namespace agentsim

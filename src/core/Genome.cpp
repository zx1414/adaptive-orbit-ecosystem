// agentsim - core/Genome.cpp
// 基因组自由函数实现（任务卡 R1-A，经 R1-AMEND-1 修订）。
// 依赖：仅 PCG32 与 <cmath>；禁 <random>/rand()/random_device/chrono（门禁扫描）。
#include "Genome.h"

#include <cmath>

#include "PCG32.h"

namespace agentsim {
namespace {

constexpr double kTwoPi = 6.283185307179586;  // 2π（Box-Muller 相位角系数）

// 钳位。NaN 防御：!(v >= lo) 同时覆盖 v < lo 与 NaN，统一落到下界（最保守值），
// 阻止 NaN 沿 环境→感知→决策→行为 管道扩散；有限输入下等价 max(lo, min(hi, v))。
double clampd(double v, double lo, double hi) {
    if (!(v >= lo)) return lo;
    if (v > hi) return hi;
    return v;
}

// 色相环绕到 [0,360)：fmod 保号，负值补一整圈；360 恰好绕回 0。
double wrapHue(double hue) {
    hue = std::fmod(hue, 360.0);
    if (hue < 0.0) hue += 360.0;
    return hue;
}

// ===========================================================================
// RNG 消耗序契约（同 seed 双跑逐字节一致的基础；新增随机性必须先更新这里）
// ===========================================================================
// 基元：
//   uniformRange(lo,hi)  恰好消耗 1 次 PCG32::nextDouble()
//   gauss01()            恰好消耗 2 次 PCG32::nextDouble()（Box-Muller，见下）
// 共用的固定遍历顺序（genomeRandomize 与 genomeMutate 一致，不得重排）：
//   1) affinity[0] affinity[1] affinity[2]
//   2) orbitRadius[0] orbitRadius[1] orbitRadius[2]
//   3) absorbPreference[0..2]
//   4) repelStrength[0..2]
//   5) 标量按 Genome 声明顺序：maxSpeed, maxAccel, energyThreshold, mutationRate,
//      cyanoPreference, cyanoDigestion, nucleusSightRadius, nucleusPersonalSpace,
//      nucleusRepelStrength, nucleusCohesion, forceRange, forceCostK
//   6) hue（最后）
// 因此 genomeRandomize 恰好消耗 25 次 nextDouble()；
//      genomeMutate   恰好消耗 25 个高斯 = 50 次 nextDouble()，之后不再触碰 rng。
// ===========================================================================

// 标准正态样本（Box-Muller）。每次调用恰好消耗 2 次 nextDouble，且与返回值无关：
// 不缓存配对的第二个样本——缓存会让消耗次数依赖调用序列，破坏固定消耗序审计。
// u1 取 1 - nextDouble() ∈ (0,1]：nextDouble 可能返回 0，直接取对数会得到 -inf。
double gauss01(PCG32& rng) {
    const double u1 = 1.0 - rng.nextDouble();  // (0,1]
    const double u2 = rng.nextDouble();        // [0,1)
    const double radius = std::sqrt(-2.0 * std::log(u1));
    return radius * std::cos(kTwoPi * u2);
}

// [lo,hi) 均匀样本，恰好消耗 1 次 nextDouble。
double uniformRange(PCG32& rng, double lo, double hi) {
    return lo + (hi - lo) * rng.nextDouble();
}

} // namespace

void genomeRandomize(Genome& g, PCG32& rng, const GenomeRanges& r) {
    // 数组按类型下标（原始抽取、不在此处归一化——归一化会把分量压到
    // affinityInitMin 之下，破坏「randomize 后落在 Init 区间」契约；
    // 需要和为 1 的调用方在 randomize 之后调用 genomeNormalizeAffinity）。
    for (int i = 0; i < kBallTypeCount; ++i) {
        g.affinity[i] = uniformRange(rng, r.affinityInitMin, r.affinityInitMax);
    }
    for (int i = 0; i < kBallTypeCount; ++i) {
        g.orbitRadius[i] = uniformRange(rng, r.orbitRadiusInitMin, r.orbitRadiusInitMax);
    }
    for (int i = 0; i < kBallTypeCount; ++i) {
        g.absorbPreference[i] = uniformRange(rng, r.absorbPreferenceInitMin, r.absorbPreferenceInitMax);
    }
    for (int i = 0; i < kBallTypeCount; ++i) {
        g.repelStrength[i] = uniformRange(rng, r.repelStrengthInitMin, r.repelStrengthInitMax);
    }
    // 标量按声明顺序。
    g.maxSpeed = uniformRange(rng, r.maxSpeedInitMin, r.maxSpeedInitMax);
    g.maxAccel = uniformRange(rng, r.maxAccelInitMin, r.maxAccelInitMax);
    g.energyThreshold = uniformRange(rng, r.energyThresholdInitMin, r.energyThresholdInitMax);
    g.mutationRate = uniformRange(rng, r.mutationRateInitMin, r.mutationRateInitMax);
    g.cyanoPreference = uniformRange(rng, r.cyanoPreferenceInitMin, r.cyanoPreferenceInitMax);
    g.cyanoDigestion = uniformRange(rng, r.cyanoDigestionInitMin, r.cyanoDigestionInitMax);
    g.nucleusSightRadius = uniformRange(rng, r.nucleusSightRadiusInitMin, r.nucleusSightRadiusInitMax);
    g.nucleusPersonalSpace = uniformRange(rng, r.nucleusPersonalSpaceInitMin, r.nucleusPersonalSpaceInitMax);
    g.nucleusRepelStrength = uniformRange(rng, r.nucleusRepelStrengthInitMin, r.nucleusRepelStrengthInitMax);
    g.nucleusCohesion = uniformRange(rng, r.nucleusCohesionInitMin, r.nucleusCohesionInitMax);
    g.forceRange = uniformRange(rng, r.forceRangeInitMin, r.forceRangeInitMax);
    g.forceCostK = uniformRange(rng, r.forceCostKInitMin, r.forceCostKInitMax);
    // hue 最后；均匀抽取后环绕（默认区间 [0,360) 下环绕是恒等变换）。
    g.hue = wrapHue(uniformRange(rng, r.hueInitMin, r.hueInitMax));
}

void genomeClamp(Genome& g, const GenomeRanges& r) {
    for (int i = 0; i < kBallTypeCount; ++i) {
        g.affinity[i] = clampd(g.affinity[i], r.affinityClampMin, r.affinityClampMax);
        g.orbitRadius[i] = clampd(g.orbitRadius[i], r.orbitRadiusClampMin, r.orbitRadiusClampMax);
        g.absorbPreference[i] = clampd(g.absorbPreference[i], r.absorbPreferenceClampMin, r.absorbPreferenceClampMax);
        g.repelStrength[i] = clampd(g.repelStrength[i], r.repelStrengthClampMin, r.repelStrengthClampMax);
    }
    g.maxSpeed = clampd(g.maxSpeed, r.maxSpeedClampMin, r.maxSpeedClampMax);
    g.maxAccel = clampd(g.maxAccel, r.maxAccelClampMin, r.maxAccelClampMax);
    g.energyThreshold = clampd(g.energyThreshold, r.energyThresholdClampMin, r.energyThresholdClampMax);
    g.mutationRate = clampd(g.mutationRate, r.mutationRateClampMin, r.mutationRateClampMax);
    g.cyanoPreference = clampd(g.cyanoPreference, r.cyanoPreferenceClampMin, r.cyanoPreferenceClampMax);
    g.cyanoDigestion = clampd(g.cyanoDigestion, r.cyanoDigestionClampMin, r.cyanoDigestionClampMax);
    g.nucleusSightRadius = clampd(g.nucleusSightRadius, r.nucleusSightRadiusClampMin, r.nucleusSightRadiusClampMax);
    g.nucleusPersonalSpace = clampd(g.nucleusPersonalSpace, r.nucleusPersonalSpaceClampMin, r.nucleusPersonalSpaceClampMax);
    g.nucleusRepelStrength = clampd(g.nucleusRepelStrength, r.nucleusRepelStrengthClampMin, r.nucleusRepelStrengthClampMax);
    g.nucleusCohesion = clampd(g.nucleusCohesion, r.nucleusCohesionClampMin, r.nucleusCohesionClampMax);
    g.forceRange = clampd(g.forceRange, r.forceRangeClampMin, r.forceRangeClampMax);
    g.forceCostK = clampd(g.forceCostK, r.forceCostKClampMin, r.forceCostKClampMax);
    g.hue = wrapHue(g.hue);  // hue 环绕而不是钳（任务卡规定）
}

void genomeNormalizeAffinity(Genome& g, const GenomeRanges& r) {
    // 先钳到 [affinityClampMin, affinityClampMax]（负数/野值不许吞权重），再归一化。
    for (int i = 0; i < kBallTypeCount; ++i) {
        g.affinity[i] = clampd(g.affinity[i], r.affinityClampMin, r.affinityClampMax);
    }
    double sum = 0.0;
    for (int i = 0; i < kBallTypeCount; ++i) sum += g.affinity[i];
    if (!(sum > 0.0)) {
        // 钳后总和仍 <= 0 或非有限：仅当配置 affinityClampMin <= 0 才可能到达；
        // !(sum > 0.0) 同时拦截 NaN，均分兜底。
        for (int i = 0; i < kBallTypeCount; ++i) g.affinity[i] = 1.0 / kBallTypeCount;
        return;
    }
    for (int i = 0; i < kBallTypeCount; ++i) g.affinity[i] /= sum;
}

void genomeMutate(Genome& g, PCG32& rng, double coeff, const GenomeRanges& r) {
    // 变异幅度公式：delta = gauss01 * sigma * coeff，sigma = mutationRate（变异前
    // 快照）× (clampMax - clampMin)。选 Clamp 区间宽度做尺度：变异步长与该参数的
    // 合法取值域成正比，同一个 mutationRate 在量纲悬殊的参数（forceCostK 宽约 1 vs
    // energyThreshold 宽约 980）上具有一致的相对探索强度；加性扰动（而非旧版乘法
    // p *= 1+rate*N）保证参数贴近边界时仍能跨出去，避免搜索空间早期锁死。
    // mutationRate 自身也参与变异（否则进化卡死在初始值），越界由收尾 genomeClamp 钳回。
    const double rate = g.mutationRate;  // 变异前快照，全部 sigma 共用

    for (int i = 0; i < kBallTypeCount; ++i) {
        g.affinity[i] += gauss01(rng) * (rate * (r.affinityClampMax - r.affinityClampMin)) * coeff;
    }
    for (int i = 0; i < kBallTypeCount; ++i) {
        g.orbitRadius[i] += gauss01(rng) * (rate * (r.orbitRadiusClampMax - r.orbitRadiusClampMin)) * coeff;
    }
    for (int i = 0; i < kBallTypeCount; ++i) {
        g.absorbPreference[i] += gauss01(rng) * (rate * (r.absorbPreferenceClampMax - r.absorbPreferenceClampMin)) * coeff;
    }
    for (int i = 0; i < kBallTypeCount; ++i) {
        g.repelStrength[i] += gauss01(rng) * (rate * (r.repelStrengthClampMax - r.repelStrengthClampMin)) * coeff;
    }
    g.maxSpeed += gauss01(rng) * (rate * (r.maxSpeedClampMax - r.maxSpeedClampMin)) * coeff;
    g.maxAccel += gauss01(rng) * (rate * (r.maxAccelClampMax - r.maxAccelClampMin)) * coeff;
    g.energyThreshold += gauss01(rng) * (rate * (r.energyThresholdClampMax - r.energyThresholdClampMin)) * coeff;
    g.mutationRate += gauss01(rng) * (rate * (r.mutationRateClampMax - r.mutationRateClampMin)) * coeff;
    g.cyanoPreference += gauss01(rng) * (rate * (r.cyanoPreferenceClampMax - r.cyanoPreferenceClampMin)) * coeff;
    g.cyanoDigestion += gauss01(rng) * (rate * (r.cyanoDigestionClampMax - r.cyanoDigestionClampMin)) * coeff;
    g.nucleusSightRadius += gauss01(rng) * (rate * (r.nucleusSightRadiusClampMax - r.nucleusSightRadiusClampMin)) * coeff;
    g.nucleusPersonalSpace += gauss01(rng) * (rate * (r.nucleusPersonalSpaceClampMax - r.nucleusPersonalSpaceClampMin)) * coeff;
    g.nucleusRepelStrength += gauss01(rng) * (rate * (r.nucleusRepelStrengthClampMax - r.nucleusRepelStrengthClampMin)) * coeff;
    g.nucleusCohesion += gauss01(rng) * (rate * (r.nucleusCohesionClampMax - r.nucleusCohesionClampMin)) * coeff;
    g.forceRange += gauss01(rng) * (rate * (r.forceRangeClampMax - r.forceRangeClampMin)) * coeff;
    g.forceCostK += gauss01(rng) * (rate * (r.forceCostKClampMax - r.forceCostKClampMin)) * coeff;
    // 色相单独处理：不乘 mutationRate（色相是视觉表型，抖动幅度只由 hueJitterSigma
    // 与全局 coeff 决定，低变异个体的颜色也不会冻结）；环绕在收尾 genomeClamp 里做。
    g.hue += gauss01(rng) * r.hueJitterSigma * coeff;

    genomeClamp(g, r);              // 全字段钳回 Clamp；hue 在此环绕到 [0,360)
    genomeNormalizeAffinity(g, r);  // affinity 变异后重新归一化（和回到 1）
}

} // namespace agentsim

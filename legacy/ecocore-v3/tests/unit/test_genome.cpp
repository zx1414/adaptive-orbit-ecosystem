// agentsim - tests/unit/test_genome.cpp
// R1-A 基因组模块单测：区间契约、钳位、归一化、变异有效性、同 seed 确定性。
// 覆盖任务卡要求 + AMEND-1 新参数（4 个 nucleus* + 2 个 force*）。
#include "test_framework.h"

#include <cmath>
#include <cstdint>

#include "../../src/core/Genome.h"
#include "../../src/core/PCG32.h"

namespace {

using agentsim::Genome;
using agentsim::GenomeRanges;
using agentsim::PCG32;
using agentsim::kBallTypeCount;
using agentsim::genomeClamp;
using agentsim::genomeMutate;
using agentsim::genomeNormalizeAffinity;
using agentsim::genomeRandomize;

bool inRange(double v, double lo, double hi) { return v >= lo && v <= hi; }

// 全字段（含 hue 的 [0,360) 环绕域）是否都落在 Clamp 区间。
bool inClampAll(const Genome& g, const GenomeRanges& r) {
    for (int i = 0; i < kBallTypeCount; ++i) {
        if (!inRange(g.affinity[i], r.affinityClampMin, r.affinityClampMax)) return false;
        if (!inRange(g.orbitRadius[i], r.orbitRadiusClampMin, r.orbitRadiusClampMax)) return false;
        if (!inRange(g.absorbPreference[i], r.absorbPreferenceClampMin, r.absorbPreferenceClampMax)) return false;
        if (!inRange(g.repelStrength[i], r.repelStrengthClampMin, r.repelStrengthClampMax)) return false;
    }
    if (!inRange(g.maxSpeed, r.maxSpeedClampMin, r.maxSpeedClampMax)) return false;
    if (!inRange(g.maxAccel, r.maxAccelClampMin, r.maxAccelClampMax)) return false;
    if (!inRange(g.energyThreshold, r.energyThresholdClampMin, r.energyThresholdClampMax)) return false;
    if (!inRange(g.mutationRate, r.mutationRateClampMin, r.mutationRateClampMax)) return false;
    if (!inRange(g.cyanoPreference, r.cyanoPreferenceClampMin, r.cyanoPreferenceClampMax)) return false;
    if (!inRange(g.cyanoDigestion, r.cyanoDigestionClampMin, r.cyanoDigestionClampMax)) return false;
    if (!inRange(g.nucleusSightRadius, r.nucleusSightRadiusClampMin, r.nucleusSightRadiusClampMax)) return false;
    if (!inRange(g.nucleusPersonalSpace, r.nucleusPersonalSpaceClampMin, r.nucleusPersonalSpaceClampMax)) return false;
    if (!inRange(g.nucleusRepelStrength, r.nucleusRepelStrengthClampMin, r.nucleusRepelStrengthClampMax)) return false;
    if (!inRange(g.nucleusCohesion, r.nucleusCohesionClampMin, r.nucleusCohesionClampMax)) return false;
    if (!inRange(g.forceRange, r.forceRangeClampMin, r.forceRangeClampMax)) return false;
    if (!inRange(g.forceCostK, r.forceCostKClampMin, r.forceCostKClampMax)) return false;
    return g.hue >= 0.0 && g.hue < 360.0;
}

// 变异收尾（genomeMutate = clamp -> normalize）之后 affinity 的合法域与严格 Clamp
// 不同：归一化除法可以把单个分量压到 affinityClampMin 之下（Genome.h 已注释该契约）。
// 因此变异后检查：非 affinity 字段仍在 Clamp；affinity 分量非负、<= affinityClampMax、
// 有限值；hue 在 [0,360)。总和为 1 由调用处另行 CHECK_NEAR。
bool inClampAfterMutate(const Genome& g, const GenomeRanges& r) {
    for (int i = 0; i < kBallTypeCount; ++i) {
        const double a = g.affinity[i];
        if (!(a >= 0.0) || !(a <= r.affinityClampMax) || !std::isfinite(a)) return false;
        if (!inRange(g.orbitRadius[i], r.orbitRadiusClampMin, r.orbitRadiusClampMax)) return false;
        if (!inRange(g.absorbPreference[i], r.absorbPreferenceClampMin, r.absorbPreferenceClampMax)) return false;
        if (!inRange(g.repelStrength[i], r.repelStrengthClampMin, r.repelStrengthClampMax)) return false;
    }
    if (!inRange(g.maxSpeed, r.maxSpeedClampMin, r.maxSpeedClampMax)) return false;
    if (!inRange(g.maxAccel, r.maxAccelClampMin, r.maxAccelClampMax)) return false;
    if (!inRange(g.energyThreshold, r.energyThresholdClampMin, r.energyThresholdClampMax)) return false;
    if (!inRange(g.mutationRate, r.mutationRateClampMin, r.mutationRateClampMax)) return false;
    if (!inRange(g.cyanoPreference, r.cyanoPreferenceClampMin, r.cyanoPreferenceClampMax)) return false;
    if (!inRange(g.cyanoDigestion, r.cyanoDigestionClampMin, r.cyanoDigestionClampMax)) return false;
    if (!inRange(g.nucleusSightRadius, r.nucleusSightRadiusClampMin, r.nucleusSightRadiusClampMax)) return false;
    if (!inRange(g.nucleusPersonalSpace, r.nucleusPersonalSpaceClampMin, r.nucleusPersonalSpaceClampMax)) return false;
    if (!inRange(g.nucleusRepelStrength, r.nucleusRepelStrengthClampMin, r.nucleusRepelStrengthClampMax)) return false;
    if (!inRange(g.nucleusCohesion, r.nucleusCohesionClampMin, r.nucleusCohesionClampMax)) return false;
    if (!inRange(g.forceRange, r.forceRangeClampMin, r.forceRangeClampMax)) return false;
    if (!inRange(g.forceCostK, r.forceCostKClampMin, r.forceCostKClampMax)) return false;
    return g.hue >= 0.0 && g.hue < 360.0;
}

double affinitySum(const Genome& g) {
    double s = 0.0;
    for (int i = 0; i < kBallTypeCount; ++i) s += g.affinity[i];
    return s;
}

// 逐字段（25 个槽位）完全相等（按位比较，用于确定性验证）。
bool sameGenome(const Genome& a, const Genome& b) {
    for (int i = 0; i < kBallTypeCount; ++i) {
        if (a.affinity[i] != b.affinity[i]) return false;
        if (a.orbitRadius[i] != b.orbitRadius[i]) return false;
        if (a.absorbPreference[i] != b.absorbPreference[i]) return false;
        if (a.repelStrength[i] != b.repelStrength[i]) return false;
    }
    return a.maxSpeed == b.maxSpeed && a.maxAccel == b.maxAccel &&
           a.energyThreshold == b.energyThreshold && a.mutationRate == b.mutationRate &&
           a.cyanoPreference == b.cyanoPreference && a.cyanoDigestion == b.cyanoDigestion &&
           a.nucleusSightRadius == b.nucleusSightRadius &&
           a.nucleusPersonalSpace == b.nucleusPersonalSpace &&
           a.nucleusRepelStrength == b.nucleusRepelStrength &&
           a.nucleusCohesion == b.nucleusCohesion &&
           a.forceRange == b.forceRange && a.forceCostK == b.forceCostK && a.hue == b.hue;
}

// 发生变化的字段槽位数（数组项各算一槽）。
int countFieldDiffs(const Genome& a, const Genome& b) {
    int diffs = 0;
    for (int i = 0; i < kBallTypeCount; ++i) {
        if (a.affinity[i] != b.affinity[i]) ++diffs;
        if (a.orbitRadius[i] != b.orbitRadius[i]) ++diffs;
        if (a.absorbPreference[i] != b.absorbPreference[i]) ++diffs;
        if (a.repelStrength[i] != b.repelStrength[i]) ++diffs;
    }
    if (a.maxSpeed != b.maxSpeed) ++diffs;
    if (a.maxAccel != b.maxAccel) ++diffs;
    if (a.energyThreshold != b.energyThreshold) ++diffs;
    if (a.mutationRate != b.mutationRate) ++diffs;
    if (a.cyanoPreference != b.cyanoPreference) ++diffs;
    if (a.cyanoDigestion != b.cyanoDigestion) ++diffs;
    if (a.nucleusSightRadius != b.nucleusSightRadius) ++diffs;
    if (a.nucleusPersonalSpace != b.nucleusPersonalSpace) ++diffs;
    if (a.nucleusRepelStrength != b.nucleusRepelStrength) ++diffs;
    if (a.nucleusCohesion != b.nucleusCohesion) ++diffs;
    if (a.forceRange != b.forceRange) ++diffs;
    if (a.forceCostK != b.forceCostK) ++diffs;
    if (a.hue != b.hue) ++diffs;
    return diffs;
}

} // namespace

// ---- 契约基线：默认值与默认构造 ----

TEST(genome_ranges_defaults_match_card) {
    GenomeRanges r;
    CHECK(r.affinityInitMin == 0.1 && r.affinityInitMax == 1.0);
    CHECK(r.affinityClampMin == 1e-4 && r.affinityClampMax == 10.0);
    CHECK(r.orbitRadiusInitMin == 30.0 && r.orbitRadiusInitMax == 200.0);
    CHECK(r.orbitRadiusClampMin == 10.0 && r.orbitRadiusClampMax == 300.0);
    CHECK(r.absorbPreferenceInitMin == 0.5 && r.absorbPreferenceInitMax == 1.5);
    CHECK(r.absorbPreferenceClampMin == 0.1 && r.absorbPreferenceClampMax == 3.0);
    CHECK(r.repelStrengthInitMin == 0.5 && r.repelStrengthInitMax == 3.0);
    CHECK(r.repelStrengthClampMin == 0.1 && r.repelStrengthClampMax == 6.0);
    CHECK(r.maxSpeedInitMin == 30.0 && r.maxSpeedInitMax == 100.0);
    CHECK(r.maxSpeedClampMin == 5.0 && r.maxSpeedClampMax == 300.0);
    CHECK(r.maxAccelInitMin == 1.0 && r.maxAccelInitMax == 20.0);
    CHECK(r.maxAccelClampMin == 0.5 && r.maxAccelClampMax == 50.0);
    CHECK(r.energyThresholdInitMin == 80.0 && r.energyThresholdInitMax == 200.0);
    CHECK(r.energyThresholdClampMin == 20.0 && r.energyThresholdClampMax == 1000.0);
    CHECK(r.mutationRateInitMin == 0.01 && r.mutationRateInitMax == 0.2);
    CHECK(r.mutationRateClampMin == 0.001 && r.mutationRateClampMax == 0.5);
    CHECK(r.cyanoPreferenceInitMin == 0.0 && r.cyanoPreferenceInitMax == 1.0);
    CHECK(r.cyanoPreferenceClampMin == 0.0 && r.cyanoPreferenceClampMax == 1.0);
    CHECK(r.cyanoDigestionInitMin == 0.1 && r.cyanoDigestionInitMax == 1.0);
    CHECK(r.cyanoDigestionClampMin == 0.05 && r.cyanoDigestionClampMax == 1.0);
    // AMEND-1 新增：对核社会参数（注意 nucleusCohesion 下限是 0，不是 1e-4）。
    CHECK(r.nucleusSightRadiusInitMin == 40.0 && r.nucleusSightRadiusInitMax == 300.0);
    CHECK(r.nucleusSightRadiusClampMin == 10.0 && r.nucleusSightRadiusClampMax == 600.0);
    CHECK(r.nucleusPersonalSpaceInitMin == 15.0 && r.nucleusPersonalSpaceInitMax == 80.0);
    CHECK(r.nucleusPersonalSpaceClampMin == 5.0 && r.nucleusPersonalSpaceClampMax == 200.0);
    CHECK(r.nucleusRepelStrengthInitMin == 0.5 && r.nucleusRepelStrengthInitMax == 6.0);
    CHECK(r.nucleusRepelStrengthClampMin == 0.1 && r.nucleusRepelStrengthClampMax == 20.0);
    CHECK(r.nucleusCohesionInitMin == 0.0 && r.nucleusCohesionInitMax == 3.0);
    CHECK(r.nucleusCohesionClampMin == 0.0 && r.nucleusCohesionClampMax == 10.0);
    // AMEND-1 新增：施力能力。
    CHECK(r.forceRangeInitMin == 30.0 && r.forceRangeInitMax == 150.0);
    CHECK(r.forceRangeClampMin == 10.0 && r.forceRangeClampMax == 400.0);
    CHECK(r.forceCostKInitMin == 0.005 && r.forceCostKInitMax == 0.05);
    CHECK(r.forceCostKClampMin == 1e-4 && r.forceCostKClampMax == 1.0);
    CHECK(r.hueInitMin == 0.0 && r.hueInitMax == 360.0);
    CHECK(r.hueJitterSigma == 8.0);
}

TEST(genome_default_constructed_is_legal) {
    GenomeRanges r;
    Genome g;
    CHECK(inClampAll(g, r));  // 严格版：默认 affinity {1/3,1/3,1/3} 必须落在严格 Clamp 内
    CHECK_NEAR(affinitySum(g), 1.0, 1e-12);
    CHECK(g.hue >= 0.0 && g.hue < 360.0);
}

// ---- genomeRandomize ----

TEST(genome_randomize_fields_in_init_range) {
    GenomeRanges r;
    Genome g;
    PCG32 rng(20261101);
    for (int trial = 0; trial < 8; ++trial) {
        genomeRandomize(g, rng, r);
        for (int i = 0; i < kBallTypeCount; ++i) {
            CHECK(inRange(g.affinity[i], r.affinityInitMin, r.affinityInitMax));
            CHECK(inRange(g.orbitRadius[i], r.orbitRadiusInitMin, r.orbitRadiusInitMax));
            CHECK(inRange(g.absorbPreference[i], r.absorbPreferenceInitMin, r.absorbPreferenceInitMax));
            CHECK(inRange(g.repelStrength[i], r.repelStrengthInitMin, r.repelStrengthInitMax));
        }
        CHECK(inRange(g.maxSpeed, r.maxSpeedInitMin, r.maxSpeedInitMax));
        CHECK(inRange(g.maxAccel, r.maxAccelInitMin, r.maxAccelInitMax));
        CHECK(inRange(g.energyThreshold, r.energyThresholdInitMin, r.energyThresholdInitMax));
        CHECK(inRange(g.mutationRate, r.mutationRateInitMin, r.mutationRateInitMax));
        CHECK(inRange(g.cyanoPreference, r.cyanoPreferenceInitMin, r.cyanoPreferenceInitMax));
        CHECK(inRange(g.cyanoDigestion, r.cyanoDigestionInitMin, r.cyanoDigestionInitMax));
        CHECK(inRange(g.nucleusSightRadius, r.nucleusSightRadiusInitMin, r.nucleusSightRadiusInitMax));
        CHECK(inRange(g.nucleusPersonalSpace, r.nucleusPersonalSpaceInitMin, r.nucleusPersonalSpaceInitMax));
        CHECK(inRange(g.nucleusRepelStrength, r.nucleusRepelStrengthInitMin, r.nucleusRepelStrengthInitMax));
        CHECK(inRange(g.nucleusCohesion, r.nucleusCohesionInitMin, r.nucleusCohesionInitMax));
        CHECK(inRange(g.forceRange, r.forceRangeInitMin, r.forceRangeInitMax));
        CHECK(inRange(g.forceCostK, r.forceCostKInitMin, r.forceCostKInitMax));
        CHECK(g.hue >= r.hueInitMin && g.hue < r.hueInitMax);
    }
}

TEST(genome_randomize_deterministic_and_seed_sensitive) {
    GenomeRanges r;
    Genome a, b, c;
    PCG32 r1(777), r2(777), r3(778);
    genomeRandomize(a, r1, r);
    genomeRandomize(b, r2, r);
    genomeRandomize(c, r3, r);
    CHECK(sameGenome(a, b));      // 同 seed：逐字段完全相等
    CHECK(!sameGenome(a, c));     // 不同 seed：必须不同
}

TEST(genome_randomize_statistical_spread) {
    // 防呆：uniform 退化成常数/单向偏置时这里会炸。maxSpeed Init 宽 70，
    // 100 个样本的极差与样本标准差都必须达到正常均匀分布的量级（理论 σ≈20.2）。
    GenomeRanges r;
    PCG32 rng(4242);
    double mn = 1e300, mx = -1e300, sum = 0.0, sumSq = 0.0;
    const int n = 100;
    for (int i = 0; i < n; ++i) {
        Genome g;
        genomeRandomize(g, rng, r);
        mn = mn < g.maxSpeed ? mn : g.maxSpeed;
        mx = mx > g.maxSpeed ? mx : g.maxSpeed;
        sum += g.maxSpeed;
        sumSq += g.maxSpeed * g.maxSpeed;
    }
    const double mean = sum / static_cast<double>(n);
    const double var = sumSq / static_cast<double>(n) - mean * mean;
    CHECK(mx - mn > 40.0);
    CHECK(std::sqrt(var) > 10.0);
    CHECK(mean > r.maxSpeedInitMin && mean < r.maxSpeedInitMax);
}

// ---- genomeClamp ----

TEST(genome_clamp_pulls_back_every_field_group) {
    GenomeRanges r;
    Genome g;
    // 每类字段至少一例：半数打穿下界、半数打穿上界。
    for (int i = 0; i < kBallTypeCount; ++i) {
        g.affinity[i] = (i % 2 == 0) ? -1.0 : 999.0;
        g.orbitRadius[i] = (i % 2 == 0) ? 0.0 : 1e6;
        g.absorbPreference[i] = (i % 2 == 0) ? -5.0 : 99.0;
        g.repelStrength[i] = (i % 2 == 0) ? 0.0 : 1e3;
    }
    g.maxSpeed = -1.0;
    g.maxAccel = 1e4;
    g.energyThreshold = 0.0;
    g.mutationRate = 7.5;
    g.cyanoPreference = -0.5;
    g.cyanoDigestion = 42.0;
    g.nucleusSightRadius = 1e5;
    g.nucleusPersonalSpace = -3.0;
    g.nucleusRepelStrength = 0.0;
    g.nucleusCohesion = -1.0;
    g.forceRange = 1e6;
    g.forceCostK = 0.0;
    g.hue = 370.0;
    genomeClamp(g, r);
    for (int i = 0; i < kBallTypeCount; ++i) {
        CHECK(inRange(g.affinity[i], r.affinityClampMin, r.affinityClampMax));
        CHECK(inRange(g.orbitRadius[i], r.orbitRadiusClampMin, r.orbitRadiusClampMax));
        CHECK(inRange(g.absorbPreference[i], r.absorbPreferenceClampMin, r.absorbPreferenceClampMax));
        CHECK(inRange(g.repelStrength[i], r.repelStrengthClampMin, r.repelStrengthClampMax));
    }
    CHECK(inRange(g.maxSpeed, r.maxSpeedClampMin, r.maxSpeedClampMax));
    CHECK(inRange(g.maxAccel, r.maxAccelClampMin, r.maxAccelClampMax));
    CHECK(inRange(g.energyThreshold, r.energyThresholdClampMin, r.energyThresholdClampMax));
    CHECK(inRange(g.mutationRate, r.mutationRateClampMin, r.mutationRateClampMax));
    CHECK(inRange(g.cyanoPreference, r.cyanoPreferenceClampMin, r.cyanoPreferenceClampMax));
    CHECK(inRange(g.cyanoDigestion, r.cyanoDigestionClampMin, r.cyanoDigestionClampMax));
    CHECK(inRange(g.nucleusSightRadius, r.nucleusSightRadiusClampMin, r.nucleusSightRadiusClampMax));
    CHECK(inRange(g.nucleusPersonalSpace, r.nucleusPersonalSpaceClampMin, r.nucleusPersonalSpaceClampMax));
    CHECK(inRange(g.nucleusRepelStrength, r.nucleusRepelStrengthClampMin, r.nucleusRepelStrengthClampMax));
    CHECK(inRange(g.nucleusCohesion, r.nucleusCohesionClampMin, r.nucleusCohesionClampMax));
    CHECK(inRange(g.forceRange, r.forceRangeClampMin, r.forceRangeClampMax));
    CHECK(inRange(g.forceCostK, r.forceCostKClampMin, r.forceCostKClampMax));
    CHECK_NEAR(g.nucleusCohesion, 0.0, 0.0);  // AMEND-1：cohesion 允许钳到 0（独行）
    // hue 环绕而不是钳：四个方向各一例。
    CHECK_NEAR(g.hue, 10.0, 0.0);   // 370 -> 10
    g.hue = -10.0;
    genomeClamp(g, r);
    CHECK_NEAR(g.hue, 350.0, 0.0);  // -10 -> 350
    g.hue = 360.0;
    genomeClamp(g, r);
    CHECK_NEAR(g.hue, 0.0, 0.0);    // 360 -> 0
    g.hue = 720.5;
    genomeClamp(g, r);
    CHECK_NEAR(g.hue, 0.5, 0.0);    // 多圈 -> 0.5
}

// ---- genomeNormalizeAffinity ----

TEST(genome_normalize_affinity_sums_to_one) {
    GenomeRanges r;
    Genome g;
    g.affinity[0] = 0.3;
    g.affinity[1] = 0.5;
    g.affinity[2] = 0.7;
    genomeNormalizeAffinity(g, r);
    CHECK_NEAR(affinitySum(g), 1.0, 1e-12);
    for (int i = 0; i < kBallTypeCount; ++i) CHECK(g.affinity[i] > 0.0);
}

TEST(genome_normalize_zero_and_negative_fallback) {
    GenomeRanges r;
    Genome g;
    // 全 0：均分（默认 clampMin=1e-4 > 0，钳下限后即可归一化）。
    g.affinity[0] = 0.0;
    g.affinity[1] = 0.0;
    g.affinity[2] = 0.0;
    genomeNormalizeAffinity(g, r);
    CHECK_NEAR(affinitySum(g), 1.0, 1e-12);
    CHECK_NEAR(g.affinity[0], 1.0 / 3.0, 1e-12);
    CHECK_NEAR(g.affinity[1], 1.0 / 3.0, 1e-12);
    CHECK_NEAR(g.affinity[2], 1.0 / 3.0, 1e-12);

    // 负数先钳下限，再归一化。
    g.affinity[0] = -2.0;
    g.affinity[1] = -0.5;
    g.affinity[2] = 3.0;
    genomeNormalizeAffinity(g, r);
    CHECK_NEAR(affinitySum(g), 1.0, 1e-12);
    for (int i = 0; i < kBallTypeCount; ++i) CHECK(g.affinity[i] >= 0.0);

    // 配置 clampMin=0 时的真零和：走均分兜底分支。
    GenomeRanges zr;
    zr.affinityClampMin = 0.0;
    g.affinity[0] = 0.0;
    g.affinity[1] = 0.0;
    g.affinity[2] = 0.0;
    genomeNormalizeAffinity(g, zr);
    CHECK_NEAR(affinitySum(g), 1.0, 1e-12);
    CHECK_NEAR(g.affinity[0], 1.0 / 3.0, 1e-12);
}

// ---- genomeMutate：区间与归一化不变量 ----

TEST(genome_mutate_keeps_clamp_and_affinity_normalized) {
    GenomeRanges r;
    Genome g;
    PCG32 rng(99);
    genomeRandomize(g, rng, r);  // 此时 affinity 是原始抽取（和 != 1）
    genomeMutate(g, rng, 1.0, r);
    CHECK(inClampAfterMutate(g, r));
    CHECK_NEAR(affinitySum(g), 1.0, 1e-12);
    for (int k = 0; k < 20; ++k) genomeMutate(g, rng, 1.0, r);
    CHECK(inClampAfterMutate(g, r));
    CHECK_NEAR(affinitySum(g), 1.0, 1e-12);
}

// ---- genomeMutate：确定性（同 seed 双跑逐位一致）----

TEST(genome_mutate_deterministic_same_seed) {
    GenomeRanges r;
    Genome a, b;
    PCG32 r1(1234), r2(1234);
    genomeRandomize(a, r1, r);
    genomeRandomize(b, r2, r);
    CHECK(sameGenome(a, b));
    for (int k = 0; k < 20; ++k) {
        genomeMutate(a, r1, 1.0, r);
        genomeMutate(b, r2, 1.0, r);
        CHECK(sameGenome(a, b));
    }
    // 20 次变异后两条 PCG32 流的内部状态也必须一致（消耗序完全固定的旁证）。
    std::uint64_t s1 = 0, i1 = 0, s2 = 0, i2 = 0;
    r1.getState(s1, i1);
    r2.getState(s2, i2);
    CHECK(s1 == s2 && i1 == i2);
}

// ---- genomeMutate：变异有效性 ----

TEST(genome_mutate_changes_something) {
    GenomeRanges r;
    Genome g, before;
    PCG32 rng(555);
    genomeRandomize(g, rng, r);
    before = g;
    for (int k = 0; k < 20; ++k) genomeMutate(g, rng, 1.0, r);
    CHECK(countFieldDiffs(before, g) >= 1);
}

TEST(genome_mutate_moves_both_directions) {
    // 防呆：扰动若退化成单向漂移/常数偏移，这里会炸。
    GenomeRanges r;
    Genome g;
    PCG32 rng(707);
    genomeRandomize(g, rng, r);
    int ups = 0, downs = 0;
    for (int k = 0; k < 500; ++k) {
        const double prev = g.orbitRadius[0];
        genomeMutate(g, rng, 1.0, r);
        if (g.orbitRadius[0] > prev) ++ups;
        else if (g.orbitRadius[0] < prev) ++downs;
    }
    CHECK(ups > 0);
    CHECK(downs > 0);
}

// ---- genomeMutate：coeff=0 必须是恒等变换（隔离归一化的最后一位舍入）----

TEST(genome_mutate_coeff_zero_is_identity) {
    GenomeRanges r;
    Genome g, before;
    PCG32 rng(31);
    genomeRandomize(g, rng, r);
    genomeClamp(g, r);
    genomeNormalizeAffinity(g, r);  // 先做成合法态，隔离「归一化本身带来的变化」
    before = g;
    for (int k = 0; k < 5; ++k) genomeMutate(g, rng, 0.0, r);
    // 扰动幅度乘 0 后所有增量都是 +0.0：全部标量必须逐位不变。
    CHECK(g.maxSpeed == before.maxSpeed);
    CHECK(g.maxAccel == before.maxAccel);
    CHECK(g.energyThreshold == before.energyThreshold);
    CHECK(g.mutationRate == before.mutationRate);
    CHECK(g.cyanoPreference == before.cyanoPreference);
    CHECK(g.cyanoDigestion == before.cyanoDigestion);
    CHECK(g.nucleusSightRadius == before.nucleusSightRadius);
    CHECK(g.nucleusPersonalSpace == before.nucleusPersonalSpace);
    CHECK(g.nucleusRepelStrength == before.nucleusRepelStrength);
    CHECK(g.nucleusCohesion == before.nucleusCohesion);
    CHECK(g.forceRange == before.forceRange);
    CHECK(g.forceCostK == before.forceCostK);
    CHECK(g.hue == before.hue);
    for (int i = 0; i < kBallTypeCount; ++i) {
        CHECK(g.orbitRadius[i] == before.orbitRadius[i]);
        CHECK(g.absorbPreference[i] == before.absorbPreference[i]);
        CHECK(g.repelStrength[i] == before.repelStrength[i]);
        // affinity 仅允许归一化的最后一位舍入抖动。
        CHECK(std::fabs(g.affinity[i] - before.affinity[i]) < 1e-12);
    }
    CHECK_NEAR(affinitySum(g), 1.0, 1e-12);
}

// ---- genomeMutate：色相环绕 ----

TEST(genome_mutate_hue_wraps_in_unit_circle) {
    GenomeRanges r;
    Genome g;
    g.hue = 359.9;
    PCG32 rng(2026);
    const double start = g.hue;
    bool moved = false;
    for (int k = 0; k < 500; ++k) {
        genomeMutate(g, rng, 1.0, r);
        CHECK(g.hue >= 0.0 && g.hue < 360.0);  // 每一次变异后都必须在环上
        if (g.hue != start) moved = true;
    }
    CHECK(moved);
}

// ---- genomeMutate：mutationRate 自身演化 ----

TEST(genome_mutate_rate_itself_evolves) {
    GenomeRanges r;
    Genome g;
    PCG32 rng(808);
    genomeRandomize(g, rng, r);
    genomeClamp(g, r);
    int changes = 0;
    for (int k = 0; k < 200; ++k) {
        const double prev = g.mutationRate;
        genomeMutate(g, rng, 1.0, r);
        if (g.mutationRate != prev) ++changes;
        CHECK(inRange(g.mutationRate, r.mutationRateClampMin, r.mutationRateClampMax));
    }
    CHECK(changes >= 1);  // 「可能变化」在 200 次里必须真的发生
}

// ---- genomeMutate：极端 coeff 的鲁棒性 ----

TEST(genome_mutate_huge_coeff_still_in_clamp) {
    GenomeRanges r;
    Genome g;
    PCG32 rng(606);
    genomeRandomize(g, rng, r);
    for (int k = 0; k < 10; ++k) {
        genomeMutate(g, rng, 50.0, r);
        CHECK(inClampAfterMutate(g, r));
        CHECK_NEAR(affinitySum(g), 1.0, 1e-12);
    }
}

// ---- 接线序列演示：randomize -> normalize（行为层出生逻辑应照此顺序）----

TEST(genome_randomize_then_normalize_wiring_sequence) {
    GenomeRanges r;
    Genome g;
    PCG32 rng(1);
    genomeRandomize(g, rng, r);
    genomeNormalizeAffinity(g, r);
    CHECK_NEAR(affinitySum(g), 1.0, 1e-12);
    // 归一化不触碰其它字段：仍是 Init 区间抽样值。
    CHECK(inRange(g.maxSpeed, r.maxSpeedInitMin, r.maxSpeedInitMax));
    CHECK(inRange(g.forceCostK, r.forceCostKInitMin, r.forceCostKInitMax));
    CHECK(inRange(g.nucleusCohesion, r.nucleusCohesionInitMin, r.nucleusCohesionInitMax));
    CHECK(inRange(g.nucleusSightRadius, r.nucleusSightRadiusInitMin, r.nucleusSightRadiusInitMax));
}


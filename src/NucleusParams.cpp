#include "NucleusParams.h"
#include <algorithm>
#include <cmath>

namespace {
double uniform(std::mt19937& rng, double lo, double hi) {
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(rng);
}
double clampd(double v, double lo, double hi) {
    return std::max(lo, std::min(hi, v));
}
}  // namespace

void NucleusParams::randomize(std::mt19937& rng, const ParamRanges& r) {
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) {
        affinity[i] = uniform(rng, r.affinityInitMin, r.affinityInitMax);
        orbitRadius[i] = uniform(rng, r.orbitRadiusInitMin, r.orbitRadiusInitMax);
        absorbPreference[i] = uniform(rng, r.absorbPreferenceInitMin, r.absorbPreferenceInitMax);
        repelStrength[i] = uniform(rng, r.repelStrengthInitMin, r.repelStrengthInitMax);
    }
    normalizeAffinity();

    attackRange = uniform(rng, r.attackRangeInitMin, r.attackRangeInitMax);
    attackStrength = uniform(rng, r.attackStrengthInitMin, r.attackStrengthInitMax);
    avoidRange = uniform(rng, r.avoidRangeInitMin, r.avoidRangeInitMax);
    avoidStrength = uniform(rng, r.avoidStrengthInitMin, r.avoidStrengthInitMax);
    maxSpeed = uniform(rng, r.maxSpeedInitMin, r.maxSpeedInitMax);
    energyThreshold = uniform(rng, r.energyThresholdInitMin, r.energyThresholdInitMax);
    mutationRate = uniform(rng, r.mutationRateInitMin, r.mutationRateInitMax);
}

void NucleusParams::normalizeAffinity() {
    double sum = 0.0;
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) sum += affinity[i];
    if (sum <= 1e-12) {
        for (int i = 0; i < BALL_TYPE_COUNT; ++i) affinity[i] = 1.0 / BALL_TYPE_COUNT;
        return;
    }
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) affinity[i] /= sum;
}

void NucleusParams::mutate(std::mt19937& rng, double rate, const ParamRanges& r) {
    std::normal_distribution<double> gauss(0.0, 1.0);
    // 乘法扰动：p <- p * (1 + rate * N(0,1))，再裁剪到配置范围。
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) {
        affinity[i] = std::max(r.affinityClampMin, affinity[i] * (1.0 + rate * gauss(rng)));
        orbitRadius[i] = clampd(orbitRadius[i] * (1.0 + rate * gauss(rng)),
                                r.orbitRadiusClampMin, r.orbitRadiusClampMax);
        absorbPreference[i] = clampd(absorbPreference[i] * (1.0 + rate * gauss(rng)),
                                     r.absorbPreferenceClampMin, r.absorbPreferenceClampMax);
        repelStrength[i] = clampd(repelStrength[i] * (1.0 + rate * gauss(rng)),
                                  r.repelStrengthClampMin, r.repelStrengthClampMax);
    }
    normalizeAffinity();

    attackRange = clampd(attackRange * (1.0 + rate * gauss(rng)),
                         r.attackRangeClampMin, r.attackRangeClampMax);
    attackStrength = clampd(attackStrength * (1.0 + rate * gauss(rng)),
                            r.attackStrengthClampMin, r.attackStrengthClampMax);
    avoidRange = clampd(avoidRange * (1.0 + rate * gauss(rng)),
                        r.avoidRangeClampMin, r.avoidRangeClampMax);
    avoidStrength = clampd(avoidStrength * (1.0 + rate * gauss(rng)),
                           r.avoidStrengthClampMin, r.avoidStrengthClampMax);
    maxSpeed = clampd(maxSpeed * (1.0 + rate * gauss(rng)),
                      r.maxSpeedClampMin, r.maxSpeedClampMax);
    energyThreshold = clampd(energyThreshold * (1.0 + rate * gauss(rng)),
                             r.energyThresholdClampMin, r.energyThresholdClampMax);
    mutationRate = clampd(mutationRate * (1.0 + rate * gauss(rng)),
                          r.mutationRateClampMin, r.mutationRateClampMax);
}

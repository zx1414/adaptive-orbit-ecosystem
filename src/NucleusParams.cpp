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

void NucleusParams::randomize(std::mt19937& rng, const ParamRanges& r, bool withCyano) {
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

    // 红线 R1：新参数的抽取必须被显式门控 —— withCyano==false 时一次都不抽，
    // 否则会推进 mt19937 状态、破坏「关闭蓝细菌时与原有模拟一致」（验收 #13）。
    // 抽取顺序固定：cyanoPreference → cyanoDigestion → maxAccel，且必须在所有已有抽取之后。
    if (withCyano) {
        cyanoPreference = uniform(rng, r.cyanoPreferenceInitMin, r.cyanoPreferenceInitMax);
        cyanoDigestion  = uniform(rng, r.cyanoDigestionInitMin, r.cyanoDigestionInitMax);
        maxAccel        = uniform(rng, r.maxAccelInitMin, r.maxAccelInitMax);
    } else {
        cyanoPreference = 0.0;
        cyanoDigestion  = 0.5;
        maxAccel        = 6.0;
    }
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

void NucleusParams::mutate(std::mt19937& rng, double rate, const ParamRanges& r, bool withCyano) {
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

    // 红线 R1 同理：withCyano==false 时不得消耗任何 gauss(rng)，保持字段原值。
    // 顺序固定：cyanoPreference → cyanoDigestion → maxAccel，追加在所有已有扰动之后。
    if (withCyano) {
        cyanoPreference = clampd(cyanoPreference * (1.0 + rate * gauss(rng)),
                                 r.cyanoPreferenceClampMin, r.cyanoPreferenceClampMax);
        cyanoDigestion  = clampd(cyanoDigestion * (1.0 + rate * gauss(rng)),
                                 r.cyanoDigestionClampMin, r.cyanoDigestionClampMax);
        maxAccel        = clampd(maxAccel * (1.0 + rate * gauss(rng)),
                                 r.maxAccelClampMin, r.maxAccelClampMax);
    }
}
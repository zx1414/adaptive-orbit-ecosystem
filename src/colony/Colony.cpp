#include "colony/Colony.h"

#include "core/Math.h"

#include <algorithm>
#include <cmath>

namespace colony {
const char* ballTypeName(BallType type) {
    switch (type) {
        case BallType::Resource: return "resource";
        case BallType::Scout: return "scout";
        case BallType::Shield: return "shield";
    }
    return "unknown";
}

namespace {
double clampRange(double value, double minimum, double maximum) {
    if (!std::isfinite(value)) return minimum;
    return std::clamp(value, minimum, maximum);
}
} // namespace

void clampParams(NucleusParams& params, const ParamRanges& ranges) {
    params.orbitRadius = clampRange(params.orbitRadius, ranges.orbitRadiusMin, ranges.orbitRadiusMax);
    params.affinity = clampRange(params.affinity, ranges.affinityMin, ranges.affinityMax);
    params.maxSpeed = clampRange(params.maxSpeed, ranges.maxSpeedMin, ranges.maxSpeedMax);
    params.maxAccel = clampRange(params.maxAccel, ranges.maxAccelMin, ranges.maxAccelMax);
    params.energyThreshold =
        clampRange(params.energyThreshold, ranges.energyThresholdMin, ranges.energyThresholdMax);
    params.mutationRate =
        clampRange(params.mutationRate, ranges.mutationRateMin, ranges.mutationRateMax);
    params.harvestRate = clampRange(params.harvestRate, ranges.harvestRateMin, ranges.harvestRateMax);
    params.spotRadius = clampRange(params.spotRadius, ranges.spotRadiusMin, ranges.spotRadiusMax);
}

void randomizeParams(NucleusParams& params, core::Random& random, const ParamRanges& ranges) {
    params.orbitRadius = random.uniformReal(ranges.orbitRadiusMin, ranges.orbitRadiusMax);
    params.affinity = random.uniformReal(ranges.affinityMin, ranges.affinityMax);
    params.maxSpeed = random.uniformReal(ranges.maxSpeedMin, ranges.maxSpeedMax);
    params.maxAccel = random.uniformReal(ranges.maxAccelMin, ranges.maxAccelMax);
    params.energyThreshold = random.uniformReal(ranges.energyThresholdMin, ranges.energyThresholdMax);
    params.mutationRate = random.uniformReal(ranges.mutationRateMin, ranges.mutationRateMax);
    params.harvestRate = random.uniformReal(ranges.harvestRateMin, ranges.harvestRateMax);
    params.spotRadius = random.uniformReal(ranges.spotRadiusMin, ranges.spotRadiusMax);
    clampParams(params, ranges);
}

void mutateParams(NucleusParams& params, core::Random& random, double rate,
                  const ParamRanges& ranges) {
    const double safeRate = std::isfinite(rate) ? std::clamp(rate, 0.0, 1.0) : 0.0;
    // Scale each perturbation by the parameter's own range so large-range genes are not
    // numerically favoured over small-range ones.
    auto bump = [&](double value, double lo, double hi) {
        return value + random.normal(0.0, safeRate * (hi - lo));
    };
    params.orbitRadius = bump(params.orbitRadius, ranges.orbitRadiusMin, ranges.orbitRadiusMax);
    params.affinity = bump(params.affinity, ranges.affinityMin, ranges.affinityMax);
    params.maxSpeed = bump(params.maxSpeed, ranges.maxSpeedMin, ranges.maxSpeedMax);
    params.maxAccel = bump(params.maxAccel, ranges.maxAccelMin, ranges.maxAccelMax);
    params.energyThreshold =
        bump(params.energyThreshold, ranges.energyThresholdMin, ranges.energyThresholdMax);
    // mutationRate itself mutates more gently, otherwise it can run away in one step.
    params.mutationRate = bump(params.mutationRate, ranges.mutationRateMin, ranges.mutationRateMax);
    params.harvestRate = bump(params.harvestRate, ranges.harvestRateMin, ranges.harvestRateMax);
    params.spotRadius = bump(params.spotRadius, ranges.spotRadiusMin, ranges.spotRadiusMax);
    clampParams(params, ranges);
}

} // namespace colony

#pragma once
#include "core/Config.h"
#include "core/Random.h"
#include "core/Types.h"

#include <cstdint>

namespace colony {

// Ball roles. A ball carries no individual physics parameters: every ball orbits using the
// owning nucleus's single orbitRadius, and type only selects what the nucleus can do with it.
enum class BallType : std::uint8_t { Resource = 0, Scout = 1, Shield = 2 };
constexpr std::size_t kBallTypeCount = 3;

const char* ballTypeName(BallType type);

// A heritable parameter set. M6 keeps the minimal eight; M7 extends it toward the v2.3.1
// 22-parameter genome. Ranges live in ParamRanges so mutation clamps stay in one place.
struct NucleusParams {
    double orbitRadius = 120.0;     // single shared orbit radius for every owned ball
    double affinity = 0.6;          // radial spring strength toward that radius
    double maxSpeed = 60.0;
    double maxAccel = 6.0;
    double energyThreshold = 120.0; // reproduction gate
    double mutationRate = 0.05;
    double harvestRate = 1.0;       // predation strength multiplier
    double spotRadius = 220.0;      // scout sensing radius
};

struct ParamRanges {
    double orbitRadiusMin = 30.0, orbitRadiusMax = 300.0;
    double affinityMin = 0.05, affinityMax = 2.0;
    double maxSpeedMin = 10.0, maxSpeedMax = 200.0;
    double maxAccelMin = 0.5, maxAccelMax = 40.0;
    double energyThresholdMin = 40.0, energyThresholdMax = 600.0;
    double mutationRateMin = 0.001, mutationRateMax = 0.5;
    double harvestRateMin = 0.1, harvestRateMax = 5.0;
    double spotRadiusMin = 60.0, spotRadiusMax = 800.0;
};

// Clamps every parameter into range. Call after randomising or mutating.
void clampParams(NucleusParams& params, const ParamRanges& ranges);
// Uniform random initialisation in range.
void randomizeParams(NucleusParams& params, core::Random& random, const ParamRanges& ranges);
// Gaussian perturbation scaled by rates, then clamped.
void mutateParams(NucleusParams& params, core::Random& random, double rate,
                  const ParamRanges& ranges);

// An organ ball. Its energy is real: it is part of the audited world total, and detaching a
// ball only changes ownerId, so the energy travels with the ball and can be picked up by any
// nucleus. Balls never photosynthesise.
struct Ball {
    core::Vec2 pos{};
    core::Vec2 vel{};
    BallType type = BallType::Resource;
    core::EntityId id = core::kInvalidEntityId;
    core::EntityId ownerId = core::kInvalidEntityId;  // 0 = free floating
    // A ball's energy is real and audited. Detaching only clears ownerId, so the energy travels
    // with the ball and any nucleus can pick it up. cap is this ball's storage contribution.
    core::EnergyState energy{0.0, 1.0, true};
    bool alive() const { return energy.alive; }
};

// Storage a single Resource ball contributes to its owner's capacity. Scout and Shield balls
// store nothing, so hoarding storage and seeing far are competing investments.
constexpr double kResourceBallCap = 40.0;
// Capacity of a nucleus that owns no resource balls.
constexpr double kNucleusNativeCap = 30.0;

// The consumer and sole decision maker.
struct Nucleus {
    core::Vec2 pos{};
    core::Vec2 vel{};
    core::EntityId id = core::kInvalidEntityId;
    core::Tick age = 0;
    NucleusParams params{};
    // Energy above kNucleusNativeCap must live in resource balls; see colonyCap().
    core::EnergyState energy{0.0, kNucleusNativeCap, true};
    double wanderAngle = 0.0;
    bool alive() const { return energy.alive; }
};

} // namespace colony

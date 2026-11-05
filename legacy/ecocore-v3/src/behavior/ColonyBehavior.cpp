#include "behavior/ColonyBehavior.h"

#include "core/Math.h"
#include "perception/ColonySensors.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace agentsim {
namespace {
constexpr double kTwoPi = 6.28318530717958647692;

// Cytoplasm capacity check: a nucleus may only hold its colony capacity. Surplus must be
// parked in resource balls, which is what makes storage an investment.
// A nucleus may only hold its colony capacity; surplus must be parked in resource balls. The
// excess is destroyed, so it MUST go through the ledger: writing the field directly and calling
// dissipate with 0 would silently create an audit mismatch (found the hard way - the audit
// caught it at tick 52).
void clipToCapacity(colony::ColonySystem& colonies, colony::Nucleus& nucleus,
                    world::EnergyLedger& ledger) {
    const double cap = std::max(1e-9, colonies.colonyCap(nucleus));
    nucleus.energy.cap = cap;
    if (nucleus.energy.energy <= cap) return;
    const double overflow = nucleus.energy.energy - cap;
    ledger.dissipate(nucleus.energy, nucleus.id, overflow, world::EnergyReason::CapOverflow);
}
} // namespace

BehaviorResult executeColonyAction(colony::ColonySystem& colonies, std::size_t colonyIndex,
                                   const decision::Action& action,
                                   environment::Cyanobacteria& cyano,
                                   world::EnergyLedger& ledger, core::Random& random,
                                   const core::SimConfig& config) {
    BehaviorResult result;
    if (colonyIndex >= colonies.colonies().size()) return result;
    colony::Nucleus& nucleus = colonies.colonies()[colonyIndex].nucleus;
    if (!nucleus.alive()) return result;

    const core::Bounds bounds = config.world.bounds();
    const double maxSpeed = std::max(0.0, nucleus.params.maxSpeed);
    const double maxAccel = std::max(0.0, nucleus.params.maxAccel);
    decision::Action safe = action;
    if (!std::isfinite(safe.dirX) || !std::isfinite(safe.dirY)) {
        safe.dirX = 0.0f;
        safe.dirY = 0.0f;
    }
    if (!std::isfinite(safe.magnitude)) safe.magnitude = 0.0f;
    safe.magnitude = std::clamp(safe.magnitude, 0.0f, 1.0f);

    core::Vec2 direction{};
    switch (safe.kind) {
        case decision::Action::Kind::Move:
            direction = core::Math::unit({safe.dirX, safe.dirY});
            break;
        case decision::Action::Kind::Harvest: {
            // Predation. The decision layer reports normalized distance (distance / spotRadius),
            // so reach in world units is the same normalized threshold scaled back up. Querying a
            // fixed radius here instead would make the two layers disagree about "close enough".
            const double reachUnits = std::max(1.0, config.colony.preyRadius);
            std::vector<environment::CyanoSample> nearby;
            cyano.querySamples(nucleus.pos, reachUnits, nearby);
            const environment::CyanoSample* best = nullptr;
            double bestDistance = 0.0;
            for (const auto& sample : nearby) {
                const double distance = (sample.position - nucleus.pos).lengthSq();
                if (!best || distance < bestDistance) {
                    best = &sample;
                    bestDistance = distance;
                }
            }
            if (best) {
                const double distance = (best->position - nucleus.pos).length();
                if (distance <= reachUnits) {
                    const double wanted =
                        nucleus.params.harvestRate * std::max(0.0, best->energy);
                    const double moved = cyano.harvest(best->id, wanted, nucleus.energy, ledger);
                    if (moved > 0.0) {
                        result.consumedPrey = true;
                        result.energyGained = moved;
                        clipToCapacity(colonies, nucleus, ledger);
                    }
                } else {
                    // Not close enough yet: steer toward the prey instead of wasting the turn.
                    direction = core::Math::unit(best->position - nucleus.pos);
                }
            }
            direction = core::Math::unit({safe.dirX, safe.dirY});
            break;
        }
        case decision::Action::Kind::DeployBall:
            colonies.deployBall(colonyIndex, colony::BallType::Resource, random, ledger);
            break;
        case decision::Action::Kind::Rest:
        default:
            break;
    }

    // Locomotion: steering toward the desired direction, bounded by maxAccel and maxSpeed.
    if (direction.lengthSq() > 0.0) {
        const core::Vec2 desired = direction * (maxSpeed * safe.magnitude);
        core::Vec2 accel = desired - nucleus.vel;
        accel = core::Math::limit(accel, maxAccel);
        nucleus.vel += accel;
        nucleus.vel = core::Math::limit(nucleus.vel, maxSpeed);
    } else {
        nucleus.vel = nucleus.vel * 0.5;
    }
    nucleus.pos += nucleus.vel;
    core::Math::bounce(nucleus.pos, nucleus.vel, bounds, 0.8, 0.0);

    const double speed = nucleus.vel.length();
    const double moveCost = std::max(0.0, config.colony.moveCostK) * speed * speed;
    if (moveCost > 0.0) {
        ledger.dissipate(nucleus.energy, nucleus.id, moveCost, world::EnergyReason::NucleusMove);
    }
    if (nucleus.energy.energy <= 1e-12) {
        ledger.kill(nucleus.energy, nucleus.id, world::EnergyReason::NucleusDeath);
    }
    clipToCapacity(colonies, nucleus, ledger);
    ++nucleus.age;
    return result;
}

void applyColonyMetabolism(colony::ColonySystem& colonies, world::EnergyLedger& ledger,
                           const core::SimConfig& config) {
    const double metabolism = std::max(0.0, config.colony.metabolism);
    if (metabolism <= 0.0) return;
    for (auto& colony : colonies.colonies()) {
        if (!colony.nucleus.alive()) continue;
        ledger.dissipate(colony.nucleus.energy, colony.nucleus.id, metabolism,
                         world::EnergyReason::NucleusMetabolism);
        if (colony.nucleus.energy.energy <= 1e-12) {
            ledger.kill(colony.nucleus.energy, colony.nucleus.id,
                        world::EnergyReason::NucleusDeath);
        }
    }
}

std::size_t reproduceColonies(colony::ColonySystem& colonies, world::EnergyLedger& ledger,
                              core::Random& random, const core::SimConfig& config) {
    std::size_t born = 0;
    const std::size_t limit = config.colony.maxNuclei;
    const std::size_t initial = colonies.colonies().size();
    colony::ParamRanges ranges;
    for (std::size_t i = 0; i < initial; ++i) {
        if (colonies.colonies().size() >= limit) break;
        colony::Nucleus& parent = colonies.colonies()[i].nucleus;
        if (!parent.alive()) continue;
        if (parent.energy.energy < parent.params.energyThreshold) continue;

        colony::Colony child;
        child.nucleus.id = colonies.nextNucleusId() + born;
        child.nucleus.params = parent.params;
        colony::mutateParams(child.nucleus.params, random, parent.params.mutationRate, ranges);
        child.nucleus.wanderAngle = random.uniformReal(0.0, kTwoPi);
        child.nucleus.pos = parent.pos + core::Vec2(random.uniformReal(-30.0, 30.0),
                                                    random.uniformReal(-30.0, 30.0));
        const core::Bounds bounds = config.world.bounds();
        child.nucleus.pos.x = std::clamp(child.nucleus.pos.x, bounds.minX, bounds.maxX);
        child.nucleus.pos.y = std::clamp(child.nucleus.pos.y, bounds.minY, bounds.maxY);
        child.nucleus.energy = core::EnergyState{0.0, colony::kNucleusNativeCap, true};

        // Zero-sum: the child is funded entirely by the parent.
        const double share = std::min(parent.energy.energy * 0.5,
                                      std::max(0.0, parent.params.energyThreshold * 0.5));
        const double moved = ledger.transfer(parent.energy, child.nucleus.energy, share,
                                             world::EnergyReason::NucleusReproduction);
        if (moved <= 0.0) continue;
        if (parent.energy.energy <= 1e-12) {
            ledger.kill(parent.energy, parent.id, world::EnergyReason::NucleusDeath);
        }
        colonies.colonies().push_back(std::move(child));
        ++born;
    }
    return born;
}

void adoptFreeBalls(colony::ColonySystem& colonies) {
    const double adoptRadius = std::max(0.0, colonies.config().adoptRadius);
    if (adoptRadius <= 0.0) return;
    for (std::size_t c = 0; c < colonies.colonies().size(); ++c) {
        const colony::Nucleus& nucleus = colonies.colonies()[c].nucleus;
        if (!nucleus.alive()) continue;
        for (std::size_t b = 0; b < colonies.balls().size(); ++b) {
            const colony::Ball& ball = colonies.balls()[b];
            if (!ball.alive() || ball.ownerId != core::kInvalidEntityId) continue;
            if ((ball.pos - nucleus.pos).lengthSq() > adoptRadius * adoptRadius) continue;
            colonies.adopt(c, b);
        }
    }
}

} // namespace agentsim

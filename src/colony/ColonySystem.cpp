#include "colony/ColonySystem.h"

#include "core/Math.h"

#include <algorithm>
#include <cmath>

namespace colony {
namespace {
constexpr double kTwoPi = 6.28318530717958647692;
}

ColonySystem::ColonySystem(core::Bounds bounds, core::ColonyConfig config)
    : bounds_(bounds.valid() ? bounds : core::Bounds{}), config_(config), ballIndex_(bounds_, 200.0) {}

void ColonySystem::initialize(core::Random& random, world::EnergyLedger& ledger) {
    balls_.clear();
    colonies_.clear();
    nextNucleusId_ = 1;
    nextBallId_ = 1;
    births_ = 0;
    deaths_ = 0;

    ParamRanges ranges;
    const std::size_t count = config_.initialNuclei;
    for (std::size_t i = 0; i < count; ++i) {
        Colony colony;
        colony.nucleus.id = nextNucleusId_++;
        colony.nucleus.pos = {random.uniformReal(bounds_.minX, bounds_.maxX),
                              random.uniformReal(bounds_.minY, bounds_.maxY)};
        randomizeParams(colony.nucleus.params, random, ranges);
        colony.nucleus.wanderAngle = random.uniformReal(0.0, kTwoPi);
        colony.nucleus.energy = core::EnergyState{0.0, kNucleusNativeCap, true};
        ledger.injectInitial(colony.nucleus.energy, colony.nucleus.id,
                             std::max(0.0, config_.initialEnergy));
        colony.nucleus.energy.cap = kNucleusNativeCap;
        colonies_.push_back(std::move(colony));
    }

    // Give every nucleus a few starting balls. These are part of the initial endowment rather
    // than a purchase, so a nucleus does not begin the run already starving.
    for (std::size_t c = 0; c < colonies_.size(); ++c) {
        const int wanted = static_cast<int>(std::max(0.0, config_.initialBallsPerNucleus));
        for (int b = 0; b < wanted; ++b) {
            const BallType type = b == 0 ? BallType::Resource
                                         : (b == 1 ? BallType::Scout : BallType::Resource);
            const double angle = colonies_[c].nucleus.wanderAngle +
                                 random.uniformReal(-0.5, 0.5);
            Ball ball;
            ball.id = nextBallId_++;
            ball.type = type;
            ball.ownerId = colonies_[c].nucleus.id;
            ball.energy = core::EnergyState{0.0,
                                            type == BallType::Resource ? kResourceBallCap : 1.0,
                                            true};
            const double distance = std::max(1.0, colonies_[c].nucleus.params.orbitRadius) * 0.6;
            ball.pos = colonies_[c].nucleus.pos +
                       core::Vec2(std::cos(angle), std::sin(angle)) * distance;
            ball.pos.x = std::clamp(ball.pos.x, bounds_.minX, bounds_.maxX);
            ball.pos.y = std::clamp(ball.pos.y, bounds_.minY, bounds_.maxY);
            balls_.push_back(ball);
            colonies_[c].balls.push_back(balls_.size() - 1);
        }
    }
    rebuildIndex();
}

void ColonySystem::integrateBalls(world::EnergyLedger& ledger) {
    (void)ledger;
    for (Colony& colony : colonies_) {
        Nucleus& nucleus = colony.nucleus;
        if (!nucleus.alive()) continue;
        for (std::size_t index : colony.balls) {
            if (index >= balls_.size()) continue;
            Ball& ball = balls_[index];
            if (!ball.alive()) continue;
            if (isDegenerate(ball, nucleus)) {
                // Place a degenerate ball just outside the nucleus so it has a direction.
                const double angle = nucleus.wanderAngle;
                ball.pos = nucleus.pos + core::Vec2(std::cos(angle), std::sin(angle)) *
                                             std::max(1.0, orbit_.nucleusRepelRadius);
                ball.vel = {};
            }
            core::Vec2 acceleration = orbitAcceleration(ball, nucleus, orbit_);
            // Balls of one colony spread along the shared orbit instead of stacking.
            for (std::size_t other : colony.balls) {
                if (other == index || other >= balls_.size()) continue;
                if (!balls_[other].alive()) continue;
                acceleration += ballRepulsion(ball, balls_[other], orbit_);
            }
            integrateBall(ball, acceleration, orbit_);
            // Keep balls inside the world.
            core::Math::bounce(ball.pos, ball.vel, bounds_, 0.6, 0.0);
        }
    }
}

void ColonySystem::updateOwnership() {
    const double detachSq = config_.detachRadius * config_.detachRadius;
    for (Colony& colony : colonies_) {
        if (!colony.nucleus.alive()) continue;
        for (auto it = colony.balls.begin(); it != colony.balls.end();) {
            const std::size_t index = *it;
            if (index >= balls_.size() || !balls_[index].alive()) {
                it = colony.balls.erase(it);
                continue;
            }
            Ball& ball = balls_[index];
            const double distanceSq = (ball.pos - colony.nucleus.pos).lengthSq();
            if (distanceSq > detachSq) {
                // Detaching only clears ownership: the ball keeps every joule it held.
                ball.ownerId = core::kInvalidEntityId;
                it = colony.balls.erase(it);
                continue;
            }
            ++it;
        }
    }
}

void ColonySystem::removeDead(world::EnergyLedger& ledger) {
    for (Colony& colony : colonies_) {
        if (!colony.nucleus.alive()) {
            // A dead nucleus releases its balls rather than destroying their energy.
            for (std::size_t index : colony.balls) {
                if (index < balls_.size() && balls_[index].alive()) {
                    balls_[index].ownerId = core::kInvalidEntityId;
                }
            }
            colony.balls.clear();
        }
    }
    const std::size_t before = colonies_.size();
    colonies_.erase(std::remove_if(colonies_.begin(), colonies_.end(),
                                   [](const Colony& c) { return !c.nucleus.alive(); }),
                    colonies_.end());
    deaths_ += before - colonies_.size();
    (void)ledger;
}

std::vector<const Nucleus*> ColonySystem::nuclei() const {
    std::vector<const Nucleus*> out;
    out.reserve(colonies_.size());
    for (const Colony& colony : colonies_) out.push_back(&colony.nucleus);
    return out;
}

std::size_t ColonySystem::aliveNuclei() const {
    return static_cast<std::size_t>(std::count_if(
        colonies_.begin(), colonies_.end(),
        [](const Colony& colony) { return colony.nucleus.alive(); }));
}

std::size_t ColonySystem::aliveBalls() const {
    return static_cast<std::size_t>(std::count_if(
        balls_.begin(), balls_.end(), [](const Ball& b) { return b.alive(); }));
}

void ColonySystem::rebuildIndex() {
    ballIndex_.clear();
    for (std::size_t i = 0; i < balls_.size(); ++i) {
        if (!balls_[i].alive()) continue;
        ballIndex_.insert(balls_[i].id, balls_[i].pos);
    }
}

double ColonySystem::colonyCap(const Nucleus& nucleus) const {
    double cap = kNucleusNativeCap;
    for (const Colony& colony : colonies_) {
        if (colony.nucleus.id != nucleus.id) continue;
        for (std::size_t index : colony.balls) {
            if (index >= balls_.size() || !balls_[index].alive()) continue;
            if (balls_[index].type == BallType::Resource) cap += balls_[index].energy.cap;
        }
        break;
    }
    return cap;
}

double ColonySystem::storedEnergyFor(const Nucleus& nucleus) const {
    double stored = 0.0;
    for (const Colony& colony : colonies_) {
        if (colony.nucleus.id != nucleus.id) continue;
        for (std::size_t index : colony.balls) {
            if (index >= balls_.size() || !balls_[index].alive()) continue;
            if (balls_[index].type == BallType::Resource) stored += balls_[index].energy.energy;
        }
        break;
    }
    return stored;
}

double colonyCapFor(const ColonySystem& system, const Nucleus& nucleus) {
    return system.colonyCap(nucleus);
}

double storedEnergyFor(const ColonySystem& system, const Nucleus& nucleus) {
    return system.storedEnergyFor(nucleus);
}

double ColonySystem::ballEnergy() const {
    double total = 0.0;
    for (const Ball& ball : balls_) {
        if (ball.alive()) total += ball.energy.energy;
    }
    return total;
}

double ColonySystem::nucleusEnergy() const {
    double total = 0.0;
    for (const Colony& colony : colonies_) {
        if (colony.nucleus.alive()) total += colony.nucleus.energy.energy;
    }
    return total;
}

void ColonySystem::adopt(std::size_t colonyIndex, std::size_t ballIndex) {
    if (colonyIndex >= colonies_.size() || ballIndex >= balls_.size()) return;
    if (!colonies_[colonyIndex].nucleus.alive() || !balls_[ballIndex].alive()) return;
    for (const Colony& colony : colonies_) {
        if (std::find(colony.balls.begin(), colony.balls.end(), ballIndex) != colony.balls.end()) {
            return;  // already owned
        }
    }
    balls_[ballIndex].ownerId = colonies_[colonyIndex].nucleus.id;
    colonies_[colonyIndex].balls.push_back(ballIndex);
}

void ColonySystem::release(std::size_t colonyIndex, std::size_t ballIndex) {
    if (colonyIndex >= colonies_.size()) return;
    auto& owned = colonies_[colonyIndex].balls;
    const auto it = std::find(owned.begin(), owned.end(), ballIndex);
    if (it == owned.end()) return;
    owned.erase(it);
    if (ballIndex < balls_.size()) balls_[ballIndex].ownerId = core::kInvalidEntityId;
}

std::size_t ColonySystem::deployBall(std::size_t colonyIndex, BallType type,
                                     core::Random& random, world::EnergyLedger& ledger) {
    if (colonyIndex >= colonies_.size()) return npos;
    Nucleus& nucleus = colonies_[colonyIndex].nucleus;
    if (!nucleus.alive()) return npos;
    if (balls_.size() >= config_.maxNuclei * 32) return npos;  // safety valve
    const double cost = std::max(0.0, config_.deployCost);
    if (nucleus.energy.energy < cost) return npos;

    Ball ball;
    ball.id = nextBallId_++;
    ball.type = type;
    ball.ownerId = nucleus.id;
    // The ball starts empty: it is storage, not a free gift of energy.
    ball.energy = core::EnergyState{0.0,
                                    type == BallType::Resource ? kResourceBallCap : 1.0, true};
    const double angle = nucleus.wanderAngle + random.uniformReal(-0.5, 0.5);
    const double distance = std::max(1.0, nucleus.params.orbitRadius) * 0.6;
    ball.pos = nucleus.pos + core::Vec2(std::cos(angle), std::sin(angle)) * distance;
    ball.pos.x = std::clamp(ball.pos.x, bounds_.minX, bounds_.maxX);
    ball.pos.y = std::clamp(ball.pos.y, bounds_.minY, bounds_.maxY);

    // Pay for the ball: dissipation, because the energy is spent, not transferred.
    ledger.dissipate(nucleus.energy, nucleus.id, cost, world::EnergyReason::BallCharge);
    if (!nucleus.energy.alive) return npos;

    const std::size_t index = balls_.size();
    balls_.push_back(ball);
    colonies_[colonyIndex].balls.push_back(index);
    return index;
}

double ColonySystem::recycleBall(std::size_t colonyIndex, std::size_t ballIndex,
                                 world::EnergyLedger& ledger) {
    if (colonyIndex >= colonies_.size() || ballIndex >= balls_.size()) return 0.0;
    Ball& ball = balls_[ballIndex];
    if (!ball.alive()) return 0.0;
    auto& owned = colonies_[colonyIndex].balls;
    if (std::find(owned.begin(), owned.end(), ballIndex) == owned.end()) return 0.0;

    Nucleus& nucleus = colonies_[colonyIndex].nucleus;
    const double stored = std::max(0.0, ball.energy.energy);
    const double refund = stored * std::clamp(config_.recycleReturn, 0.0, 1.0);
    // Recycled storage is worth less than it held: the remainder dissipates.
    const double moved = refund > 0.0
                             ? ledger.transfer(ball.energy, nucleus.energy, refund,
                                               world::EnergyReason::BallDischarge)
                             : 0.0;
    ledger.kill(ball.energy, ball.id, world::EnergyReason::BallDeath);
    owned.erase(std::find(owned.begin(), owned.end(), ballIndex));
    return moved;
}

void ColonySystem::queryBalls(core::Vec2 center, double radius,
                              std::vector<std::size_t>& out) const {
    out.clear();
    if (!core::Math::isFinite(center) || !std::isfinite(radius) || radius <= 0.0) return;
    static thread_local std::vector<core::EntityId> ids;
    ballIndex_.queryUnsorted(center, radius, ids);
    const double radiusSq = radius * radius;
    for (core::EntityId id : ids) {
        for (std::size_t i = 0; i < balls_.size(); ++i) {
            if (balls_[i].id != id || !balls_[i].alive()) continue;
            if ((balls_[i].pos - center).lengthSq() > radiusSq) break;
            out.push_back(i);
            break;
        }
    }
}

} // namespace colony

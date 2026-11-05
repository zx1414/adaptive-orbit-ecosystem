#pragma once
#include "colony/Colony.h"
#include "colony/Orbit.h"
#include "core/Config.h"
#include "core/Random.h"
#include "core/Types.h"
#include "world/Energy.h"
#include "world/SpatialIndex.h"

#include <vector>

namespace sim { class SnapshotStore; }

namespace colony {

// One nucleus together with the balls it owns. balls holds indices into the system's flat ball
// array, so ownership changes never copy energy.
struct Colony {
    Nucleus nucleus;
    std::vector<std::size_t> balls;
};

// Owns every nucleus and every ball. Balls live in one flat array regardless of owner, so a
// detached ball keeps its energy and can be adopted by a different nucleus.
class ColonySystem {
public:
    ColonySystem(core::Bounds bounds, core::ColonyConfig config);

    void initialize(core::Random& random, world::EnergyLedger& ledger);

    // Orbital integration for every owned ball, plus nucleus attraction.
    void integrateBalls(world::EnergyLedger& ledger);
    // Detach balls that drifted too far from their owner. Energy stays with the ball.
    void updateOwnership();
    void removeDead(world::EnergyLedger& ledger);
    void rebuildIndex();

    std::size_t aliveNuclei() const;
    std::size_t aliveBalls() const;
    std::size_t nucleusBirths() const { return births_; }
    std::size_t nucleusDeaths() const { return deaths_; }
    core::EntityId nextNucleusId() const { return nextNucleusId_; }
    core::EntityId nextBallId() const { return nextBallId_; }

    // Nuclei live inside colonies_ so there is exactly one copy of each nucleus; this rebuilds
    // a flat view for callers that only iterate nuclei. Never cache the reference across a
    // mutation of colonies_.
    std::vector<const Nucleus*> nuclei() const;
    const std::vector<Ball>& balls() const { return balls_; }
    std::vector<Ball>& balls() { return balls_; }
    const std::vector<Colony>& colonies() const { return colonies_; }
    std::vector<Colony>& colonies() { return colonies_; }
    const core::ColonyConfig& config() const { return config_; }
    const OrbitParams& orbit() const { return orbit_; }
    OrbitParams& orbit() { return orbit_; }

    // Capacity of a colony: native plus one contribution per owned Resource ball.
    double colonyCap(const Nucleus& nucleus) const;
    // Energy currently held inside this nucleus's resource balls.
    double storedEnergyFor(const Nucleus& nucleus) const;
    double ballEnergy() const;
    double nucleusEnergy() const;

    // Adopt a free ball into a colony. Does not move energy.
    void adopt(std::size_t colonyIndex, std::size_t ballIndex);
    // Release a ball; it keeps its energy and becomes free floating.
    void release(std::size_t colonyIndex, std::size_t ballIndex);

    // Spend nucleus energy to create a ball of type, owned by that nucleus.
    std::size_t deployBall(std::size_t colonyIndex, BallType type, core::Random& random,
                           world::EnergyLedger& ledger);
    // Recycle a ball: part of its energy returns to the nucleus, the rest dissipates.
    double recycleBall(std::size_t colonyIndex, std::size_t ballIndex, world::EnergyLedger& ledger);

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    void queryBalls(core::Vec2 center, double radius, std::vector<std::size_t>& out) const;
    const world::SpatialIndex& ballIndex() const { return ballIndex_; }

private:
    friend class sim::SnapshotStore;
    core::Bounds bounds_;
    core::ColonyConfig config_;
    OrbitParams orbit_;
    std::vector<Ball> balls_;
    std::vector<Colony> colonies_;
    world::SpatialIndex ballIndex_;
    core::EntityId nextNucleusId_ = 1;
    core::EntityId nextBallId_ = 1;
    std::size_t births_ = 0;
    std::size_t deaths_ = 0;
};

// Free-function forms so the perception layer can ask without owning a system reference.
double colonyCapFor(const ColonySystem& system, const Nucleus& nucleus);
double storedEnergyFor(const ColonySystem& system, const Nucleus& nucleus);

} // namespace colony

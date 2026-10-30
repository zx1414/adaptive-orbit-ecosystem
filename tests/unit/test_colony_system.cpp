#include "TestHarness.h"

#include "colony/Colony.h"
#include "colony/ColonySystem.h"
#include "colony/Orbit.h"
#include "core/Config.h"
#include "core/Random.h"
#include "world/Energy.h"

#include <cmath>
#include <vector>

namespace {
core::ColonyConfig colonyConfig() {
    core::ColonyConfig config;
    config.initialNuclei = 2;
    config.maxNuclei = 20;
    config.initialEnergy = 200.0;
    config.initialBallsPerNucleus = 0.0;
    config.deployCost = 10.0;
    config.recycleReturn = 0.5;
    config.detachRadius = 400.0;
    return config;
}
} // namespace

TEST("deploying a ball spends nucleus energy and starts the ball empty") {
    core::Bounds bounds = core::Bounds::fromSize(1000.0, 1000.0);
    world::EnergyLedger ledger;
    core::Random random(4);
    colony::ColonySystem system(bounds, colonyConfig());
    system.initialize(random, ledger);
    ledger.sealInitial();

    const double before = system.colonies()[0].nucleus.energy.energy;
    const std::size_t index = system.deployBall(0, colony::BallType::Resource, random, ledger);
    CHECK(index != colony::ColonySystem::npos);
    CHECK_NEAR(system.balls()[index].energy.energy, 0.0, 0.0);
    CHECK(system.colonies()[0].nucleus.energy.energy < before);
    CHECK(ledger.audit(system.nucleusEnergy() + system.ballEnergy(),
                       1e-9 * std::max(1.0, ledger.stats().injected)));
}

TEST("resource balls raise the colony capacity and recycle returns only a fraction") {
    core::Bounds bounds = core::Bounds::fromSize(1000.0, 1000.0);
    world::EnergyLedger ledger;
    core::Random random(6);
    colony::ColonySystem system(bounds, colonyConfig());
    system.initialize(random, ledger);
    ledger.sealInitial();

    const double nativeCap = system.colonyCap(system.colonies()[0].nucleus);
    CHECK_NEAR(nativeCap, colony::kNucleusNativeCap, 0.0);

    const std::size_t index = system.deployBall(0, colony::BallType::Resource, random, ledger);
    CHECK(index != colony::ColonySystem::npos);
    CHECK(system.colonyCap(system.colonies()[0].nucleus) > nativeCap);

    // Move energy into the ball through the ledger, as the simulation does; writing to the
    // field directly would create joules from nothing and the audit would rightly fail.
    const double stored = 20.0;
    ledger.transfer(system.colonies()[0].nucleus.energy, system.balls()[index].energy, stored,
                    world::EnergyReason::BallCharge);
    CHECK_NEAR(system.balls()[index].energy.energy, stored, 1e-12);
    const double nucleusBefore = system.colonies()[0].nucleus.energy.energy;
    const double refund = system.recycleBall(0, index, ledger);
    CHECK(refund > 0.0);
    CHECK(refund < stored);
    CHECK(system.colonies()[0].nucleus.energy.energy > nucleusBefore);
    CHECK(!system.balls()[index].alive());
    CHECK(ledger.audit(system.nucleusEnergy() + system.ballEnergy(),
                       1e-9 * std::max(1.0, ledger.stats().injected)));
}

TEST("a detached ball keeps its energy and can be adopted by another nucleus") {
    core::Bounds bounds = core::Bounds::fromSize(2000.0, 2000.0);
    world::EnergyLedger ledger;
    core::Random random(8);
    colony::ColonySystem system(bounds, colonyConfig());
    system.initialize(random, ledger);
    ledger.sealInitial();
    CHECK(system.colonies().size() == 2);

    const std::size_t index = system.deployBall(0, colony::BallType::Resource, random, ledger);
    CHECK(index != colony::ColonySystem::npos);
    ledger.transfer(system.colonies()[0].nucleus.energy, system.balls()[index].energy, 15.0,
                    world::EnergyReason::BallCharge);
    const double ballEnergyBefore = system.balls()[index].energy.energy;
    const double totalBefore = system.nucleusEnergy() + system.ballEnergy();

    system.balls()[index].pos = system.colonies()[0].nucleus.pos + core::Vec2(900.0, 0.0);
    system.updateOwnership();
    CHECK(system.balls()[index].ownerId == core::kInvalidEntityId);
    CHECK_NEAR(system.balls()[index].energy.energy, ballEnergyBefore, 1e-12);
    CHECK_NEAR(system.nucleusEnergy() + system.ballEnergy(), totalBefore, 1e-12);

    system.balls()[index].pos = system.colonies()[1].nucleus.pos + core::Vec2(5.0, 0.0);
    system.adopt(1, index);
    CHECK(system.balls()[index].ownerId == system.colonies()[1].nucleus.id);
    CHECK_NEAR(system.balls()[index].energy.energy, ballEnergyBefore, 1e-12);
}

TEST("a dead nucleus releases its balls instead of destroying them") {
    core::Bounds bounds = core::Bounds::fromSize(1000.0, 1000.0);
    world::EnergyLedger ledger;
    core::Random random(12);
    colony::ColonySystem system(bounds, colonyConfig());
    system.initialize(random, ledger);
    ledger.sealInitial();

    const std::size_t index = system.deployBall(0, colony::BallType::Resource, random, ledger);
    ledger.transfer(system.colonies()[0].nucleus.energy, system.balls()[index].energy, 25.0,
                    world::EnergyReason::BallCharge);
    const double ballEnergy = system.balls()[index].energy.energy;

    ledger.kill(system.colonies()[0].nucleus.energy, system.colonies()[0].nucleus.id,
                world::EnergyReason::NucleusDeath);
    system.removeDead(ledger);

    CHECK(system.balls()[index].alive());
    CHECK_NEAR(system.balls()[index].energy.energy, ballEnergy, 1e-12);
    CHECK(system.balls()[index].ownerId == core::kInvalidEntityId);
}

TEST("balls orbit their nucleus and stay near the target radius") {
    core::Bounds bounds = core::Bounds::fromSize(4000.0, 4000.0);
    world::EnergyLedger ledger;
    core::Random random(15);
    colony::ColonySystem system(bounds, colonyConfig());
    system.initialize(random, ledger);
    ledger.sealInitial();

    for (auto& colony : system.colonies()) {
        colony.nucleus.params.orbitRadius = 120.0;
        colony.nucleus.params.affinity = 0.6;
        colony.nucleus.pos = {2000.0, 2000.0};
    }
    std::vector<std::size_t> indices;
    for (int i = 0; i < 3; ++i) {
        const std::size_t index = system.deployBall(0, colony::BallType::Resource, random, ledger);
        CHECK(index != colony::ColonySystem::npos);
        indices.push_back(index);
    }

    for (int tick = 0; tick < 3000; ++tick) system.integrateBalls(ledger);

    const auto& nucleus = system.colonies()[0].nucleus;
    double sum = 0.0;
    std::size_t counted = 0;
    for (std::size_t index : indices) {
        const double distance = colony::orbitDistance(system.balls()[index], nucleus);
        CHECK(std::isfinite(distance));
        sum += distance;
        ++counted;
    }
    CHECK(counted == indices.size());
    const double mean = sum / static_cast<double>(counted);
    CHECK(mean > 120.0 * 0.5);
    CHECK(mean < 120.0 * 2.0);
}

TEST("the ledger audits nucleus and ball energy together") {
    core::Bounds bounds = core::Bounds::fromSize(800.0, 800.0);
    world::EnergyLedger ledger;
    core::Random random(17);
    colony::ColonySystem system(bounds, colonyConfig());
    system.initialize(random, ledger);
    ledger.sealInitial();

    for (int i = 0; i < 5; ++i) {
        system.deployBall(0, colony::BallType::Resource, random, ledger);
    }
    for (int tick = 0; tick < 50; ++tick) {
        system.integrateBalls(ledger);
        system.updateOwnership();
        const double stored = system.nucleusEnergy() + system.ballEnergy();
        CHECK(ledger.audit(stored, 1e-9 * std::max(1.0, ledger.stats().injected)));
    }
}

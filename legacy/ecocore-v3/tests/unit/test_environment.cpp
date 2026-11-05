#include "TestHarness.h"

#include "core/Config.h"
#include "core/Random.h"
#include "environment/Cyanobacteria.h"
#include "sim/Simulation.h"
#include "world/Energy.h"
#include "sim/SnapshotStore.h"
#include "world/Environment.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

namespace {
core::CyanoConfig smallConfig() {
    core::CyanoConfig config;
    config.initial = 20;
    config.maxNodes = 200;
    config.initialEnergy = 10.0;
    config.cap = 40.0;
    config.photoRate = 2.0;
    config.lightHalfSaturation = 0.5;
    config.metabolism = 0.001;
    config.reproThreshold = 30.0;
    config.reproProbability = 1.0;   // deterministic reproduction in tests
    config.reproOffset = 40.0;
    return config;
}
core::SimConfig cyanoSimConfig() {
    core::SimConfig config;
    config.world.maxTicks = 200;
    config.cyano = smallConfig();
    // Two nuclei so the world stays alive and the snapshot carries a full state.
    config.colony.initialNuclei = 2;
    config.colony.maxNuclei = 8;
    config.colony.initialEnergy = 120.0;
    config.colony.initialBallsPerNucleus = 2.0;
    return config;
}
} // namespace

TEST("cyanobacteria photosynthesis is the only primary injector") {
    core::Bounds bounds = core::Bounds::fromSize(500.0, 500.0);
    core::LightConfig light;
    world::Environment environment(bounds, light);
    world::EnergyLedger ledger;
    core::Random random(7);
    environment::Cyanobacteria cyano(bounds, smallConfig());
    cyano.initialize(random, ledger);
    ledger.sealInitial();
    CHECK(cyano.aliveCount() == 20);
    const double before = ledger.stats().injectedPrimary();
    cyano.tick(random, ledger, environment, 0);
    const double after = ledger.stats().injectedPrimary();
    // Light is non-zero, so a tick must add primary energy and stay audited.
    CHECK(after > before);
    CHECK(ledger.audit(cyano.totalEnergy(), 1e-9 * std::max(1.0, ledger.stats().injected)));
}

TEST("cyanobacteria nodes conserve energy through metabolism and reproduction") {
    core::Bounds bounds = core::Bounds::fromSize(500.0, 500.0);
    core::LightConfig light;
    world::Environment environment(bounds, light);
    world::EnergyLedger ledger;
    core::Random random(11);
    environment::Cyanobacteria cyano(bounds, smallConfig());
    cyano.initialize(random, ledger);
    ledger.sealInitial();
    for (core::Tick tick = 0; tick < 50; ++tick) {
        cyano.tick(random, ledger, environment, tick);
        const double stored = cyano.totalEnergy();
        CHECK(ledger.audit(stored, 1e-9 * std::max(1.0, ledger.stats().injected)));
    }
    CHECK(cyano.births() > 0);
    CHECK(ledger.stats().injectedPrimary() > 0.0);
    CHECK_NEAR(ledger.stats().reasons[static_cast<std::size_t>(world::EnergyReason::CyanoReproduction)].in,
               ledger.stats().reasons[static_cast<std::size_t>(world::EnergyReason::CyanoReproduction)].out,
               1e-9);
}

TEST("consuming a node returns its energy and keeps the ledger balanced") {
    core::Bounds bounds = core::Bounds::fromSize(300.0, 300.0);
    core::LightConfig light;
    world::Environment environment(bounds, light);
    world::EnergyLedger ledger;
    core::Random random(3);
    environment::Cyanobacteria cyano(bounds, smallConfig());
    cyano.initialize(random, ledger);
    ledger.sealInitial();

    const core::EntityId id = cyano.all().front().id;
    const double before = cyano.totalEnergy();
    const double gained = cyano.consume(id, ledger);
    CHECK(gained > 0.0);
    CHECK_NEAR(cyano.consume(id, ledger), 0.0, 0.0);
    CHECK_NEAR(cyano.totalEnergy(), before - gained, 1e-12);
    CHECK(ledger.audit(cyano.totalEnergy(), 1e-9 * std::max(1.0, ledger.stats().injected)));
}

TEST("cyanobacteria queries respect the radius and skip dead nodes") {
    core::Bounds bounds = core::Bounds::fromSize(400.0, 400.0);
    core::LightConfig light;
    world::Environment environment(bounds, light);
    world::EnergyLedger ledger;
    core::Random random(5);
    environment::Cyanobacteria cyano(bounds, smallConfig());
    cyano.initialize(random, ledger);
    ledger.sealInitial();

    const core::Vec2 center = cyano.all().front().pos;
    std::vector<environment::CyanoSample> samples;
    cyano.querySamples(center, 1.0, samples);
    for (const auto& sample : samples) {
        CHECK((sample.position - center).lengthSq() <= 1.0 + 1e-9);
    }
    const core::EntityId id = cyano.all().front().id;
    cyano.consume(id, ledger);
    cyano.querySamples(center, 500.0, samples);
    for (const auto& sample : samples) CHECK(sample.id != id);
}

TEST("simulation audits the whole ecosystem, not just one layer") {
    sim::Simulation simulation(cyanoSimConfig());
    CHECK(simulation.cyanobacteria().aliveCount() == 20);
    CHECK(simulation.colonies().colonies().size() == 2);
    for (int i = 0; i < 100; ++i) {
        simulation.step();
        CHECK(simulation.auditOk());
    }
    CHECK(simulation.cyanobacteria().aliveCount() > 0);
    // The audited total is environment + nuclei + balls, not any single layer.
    const double expected = simulation.cyanobacteria().totalEnergy() +
                            simulation.colonies().nucleusEnergy() +
                            simulation.colonies().ballEnergy();
    CHECK_NEAR(simulation.totalEnergy(), expected, 1e-9);
    CHECK(simulation.ledger().stats().injectedPrimary() > 0.0);
}

TEST("cyanobacteria layer is deterministic for a fixed seed") {
    sim::Simulation a(cyanoSimConfig());
    sim::Simulation b(cyanoSimConfig());
    for (int i = 0; i < 150; ++i) {
        a.step();
        b.step();
    }
    CHECK(a.stateHash() == b.stateHash());
    CHECK(a.cyanobacteria().aliveCount() == b.cyanobacteria().aliveCount());
    CHECK_NEAR(a.cyanobacteria().totalEnergy(), b.cyanobacteria().totalEnergy(), 0.0);
}

TEST("snapshot round-trip preserves cyanobacteria nodes") {
    sim::Simulation original(cyanoSimConfig());
    for (int i = 0; i < 120; ++i) original.step();
    const std::size_t alive = original.cyanobacteria().aliveCount();
    const double energy = original.cyanobacteria().totalEnergy();
    CHECK(alive > 0);

    std::error_code ignored;
    std::filesystem::create_directories("build", ignored);
    const std::string path = (std::filesystem::path("build") / "m5_cyano.snap").string();
    std::string error;
    CHECK(sim::SnapshotStore::save(original, path, error));

    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::FormatError;
    std::unique_ptr<sim::Simulation> loaded =
        sim::SnapshotStore::load(path, false, 0, error, status);
    CHECK(loaded != nullptr);
    CHECK(status == sim::snapshot::LoadStatus::Ok);
    if (!loaded) return;
    CHECK(loaded->cyanobacteria().aliveCount() == alive);
    CHECK_NEAR(loaded->cyanobacteria().totalEnergy(), energy, 1e-9);
    CHECK(loaded->stateHash() == original.stateHash());
    CHECK(loaded->auditOk());

    for (int i = 0; i < 60; ++i) {
        original.step();
        loaded->step();
    }
    CHECK(loaded->stateHash() == original.stateHash());
    CHECK(loaded->auditOk());
    std::filesystem::remove(path);
}

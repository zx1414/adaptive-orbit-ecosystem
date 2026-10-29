#include "TestHarness.h"
#include "world/Energy.h"
#include "world/Environment.h"
#include "producers/ProducerSystem.h"
#include "core/Random.h"

using namespace world;

TEST("energy ledger conserves transfers and accounts injections") {
    core::EnergyState a{0.0, 20.0, true};
    core::EnergyState b{0.0, 20.0, true};
    EnergyLedger ledger;
    CHECK_NEAR(ledger.injectInitial(a, 1, 5.0), 5.0, 1e-12);
    CHECK_NEAR(ledger.injectInitial(b, 2, 8.0), 8.0, 1e-12);
    CHECK_NEAR(ledger.transfer(a, b, 10.0, EnergyReason::AgentHarvest), 5.0, 1e-12);
    CHECK_NEAR(a.energy, 0.0, 1e-12);
    CHECK_NEAR(b.energy, 13.0, 1e-12);
    CHECK_NEAR(ledger.totalIn(EnergyReason::InitialEndowment), 13.0, 1e-12);
    CHECK(ledger.audit(a.energy + b.energy));
}

TEST("energy ledger kill dissipates residual and is idempotent") {
    core::EnergyState state{0.0, 10.0, true};
    EnergyLedger ledger;
    ledger.injectInitial(state, 1, 4.0);
    ledger.kill(state, 7, EnergyReason::AgentDeath);
    CHECK(!state.alive);
    CHECK_NEAR(state.energy, 0.0, 1e-12);
    CHECK_NEAR(ledger.totalOut(EnergyReason::AgentDeath), 4.0, 1e-12);
    ledger.kill(state, 7, EnergyReason::AgentDeath);
    CHECK_NEAR(ledger.totalOut(EnergyReason::AgentDeath), 4.0, 1e-12);
    CHECK(ledger.audit(0.0));
}

TEST("initial endowment is sealed after world setup") {
    core::EnergyState state{0.0, 10.0, true};
    EnergyLedger ledger;
    CHECK_NEAR(ledger.injectInitial(state, 1, 3.0), 3.0, 1e-12);
    ledger.sealInitial();
    CHECK_NEAR(ledger.injectInitial(state, 2, 3.0), 0.0, 0.0);
    CHECK_NEAR(state.energy, 3.0, 1e-12);
    CHECK(ledger.initialSealed());
}

TEST("sub-epsilon dissipation is accounted, not silently dropped") {
    core::EnergyState state{0.0, 1.0, true};
    EnergyLedger ledger;
    CHECK_NEAR(ledger.injectInitial(state, 1, 1.5e-12), 1.5e-12, 1e-24);
    CHECK_NEAR(ledger.dissipate(state, 1, 0.6e-12, EnergyReason::AgentMetabolism), 0.6e-12, 1e-24);
    CHECK_NEAR(state.energy, 0.0, 0.0);
    CHECK_NEAR(ledger.totalOut(EnergyReason::AgentMetabolism), 1.5e-12, 1e-24);
    CHECK(ledger.audit(0.0));
}

TEST("environment is a passive finite light field") {
    core::LightConfig light;
    Environment environment(core::Bounds::fromSize(100.0, 100.0), light);
    const double a = environment.lightAt({50.0, 50.0}, 0);
    const double b = environment.lightAt({50.0, 50.0}, 0);
    CHECK(a >= 0.0);
    CHECK_NEAR(a, b, 0.0);
    CHECK(std::isfinite(environment.lightAt({-10.0, 1000.0}, 42)));
}

TEST("photo producer is the only primary injector") {
    core::Bounds bounds = core::Bounds::fromSize(100.0, 100.0);
    core::ProducerConfig config;
    config.initial = 1;
    config.initialEnergy = 20.0;
    config.cap = 100;
    config.photo = 2.0;
    config.maint = 0.0;
    config.repro = 1000.0;
    core::LightConfig light;
    Environment environment(bounds, light);
    EnergyLedger ledger;
    core::Random random(7);
    producers::ProducerSystem system(bounds, config);
    system.initialize(random, ledger, environment);
    CHECK(system.aliveCount() == 1);
    const double before = ledger.totalIn(EnergyReason::Photosynthesis);
    system.tick(random, ledger, environment, 0);
    CHECK(ledger.totalIn(EnergyReason::Photosynthesis) > before + 1e-9);
    CHECK(ledger.audit(system.all()[0]->energy.energy));
}

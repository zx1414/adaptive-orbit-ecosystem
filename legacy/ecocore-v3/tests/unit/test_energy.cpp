#include "TestHarness.h"
#include "world/Energy.h"
#include "world/Environment.h"
#include "environment/Cyanobacteria.h"
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

TEST("consumePrey is zero-sum and kills the prey") {
    core::EnergyState prey{0.0, 20.0, true};
    core::EnergyState predator{0.0, 30.0, true};
    EnergyLedger ledger;
    ledger.injectInitial(prey, 1, 10.0);
    const double moved = ledger.consumePrey(prey, 1, predator, 4.0,
                                            EnergyReason::NucleusPrey,
                                            EnergyReason::CyanoDeath);
    CHECK_NEAR(moved, 4.0, 1e-12);
    CHECK_NEAR(predator.energy, 4.0, 1e-12);
    CHECK(!prey.alive);
    CHECK_NEAR(prey.energy, 0.0, 1e-12);
    CHECK(ledger.audit(predator.energy));
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

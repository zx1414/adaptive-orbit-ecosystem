#include "TestHarness.h"

#include "agents/Behavior.h"
#include "core/Config.h"
#include "core/Random.h"
#include "decision/PolicyRegistry.h"
#include "producers/Producer.h"
#include "sim/Simulation.h"
#include "world/Environment.h"
#include "world/Forage.h"
#include "world/SpatialIndex.h"
#include "world/WorldQuery.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {
class FakeQuery final : public world::WorldQuery {
public:
    core::SimConfig cfg{};
    core::Bounds boundsValue = core::Bounds::fromSize(100.0, 100.0);

    const core::SimConfig& config() const override { return cfg; }
    core::Bounds bounds() const override { return boundsValue; }
    core::Tick tick() const override { return 0; }
    double lightAt(core::Vec2) const override { return 1.0; }
    void queryProducers(core::Vec2, double, std::vector<world::ProducerSample>& out) const override {
        out.clear();
        out.push_back({1, {10.0, 10.0}, 100.0, 100.0});
    }
    void queryAgents(core::Vec2, double, std::vector<world::AgentSample>& out) const override {
        out.clear();
    }
};

class FakeForage final : public world::Forage {
public:
    double lastAmount = 0.0;
    double harvest(core::EntityId, double amount, double, core::EnergyState&) override {
        lastAmount = amount;
        return amount;
    }
};
} // namespace

TEST("spatial index sorted and unsorted queries agree") {
    world::SpatialIndex index(core::Bounds::fromSize(1000.0, 1000.0), 100.0);
    index.insert(1, {500.0, 500.0});
    index.insert(2, {600.0, 500.0});
    index.insert(3, {500.0, 600.0});
    index.insert(4, {900.0, 900.0});
    std::vector<core::EntityId> sorted;
    std::vector<core::EntityId> unsorted;
    index.query({500.0, 500.0}, 200.0, sorted);
    index.queryUnsorted({500.0, 500.0}, 200.0, unsorted);
    CHECK(sorted.size() == 3);
    CHECK(unsorted.size() == 3);
    std::sort(unsorted.begin(), unsorted.end());
    CHECK(sorted == unsorted);
    CHECK(sorted.front() == 1);
}

TEST("producer local competition reduces photosynthesis") {
    core::ProducerConfig config;
    config.photo = 10.0;
    config.maint = 0.0;
    config.repro = 1000.0;
    config.competition = 1.0;
    core::LightConfig light;
    world::Environment environment(core::Bounds::fromSize(100.0, 100.0), light);
    core::Random randomA(7);
    core::Random randomB(7);
    world::EnergyLedger ledgerA;
    world::EnergyLedger ledgerB;
    producers::PhotoProducer a(config, {0.0, 100.0, true});
    producers::PhotoProducer b(config, {0.0, 100.0, true});
    a.id = 1;
    b.id = 2;
    a.pos = {50.0, 50.0};
    b.pos = {50.0, 50.0};
    producers::ProducerContext contextA;
    contextA.environment = &environment;
    contextA.ledger = &ledgerA;
    contextA.random = &randomA;
    contextA.tick = 0;
    contextA.population = 1;
    contextA.maxPopulation = 100;
    producers::ProducerContext contextB = contextA;
    contextB.ledger = &ledgerB;
    contextB.random = &randomB;
    contextB.localBiomass = 50.0;
    a.tick(contextA);
    b.tick(contextB);
    CHECK(a.energy.energy > 0.0);
    CHECK(b.energy.energy > 0.0);
    CHECK(b.energy.energy < a.energy.energy);
}

TEST("producer reproduction is suppressed by local density") {
    core::ProducerConfig config;
    config.photo = 0.0;
    config.maint = 0.0;
    config.repro = 10.0;
    config.share = 5.0;
    config.densityRepro = 1.0;
    core::LightConfig light;
    world::Environment environment(core::Bounds::fromSize(100.0, 100.0), light);
    core::Random randomA(11);
    core::Random randomB(11);
    world::EnergyLedger ledgerA;
    world::EnergyLedger ledgerB;
    producers::PhotoProducer sparse(config, {50.0, 100.0, true});
    producers::PhotoProducer dense(config, {50.0, 100.0, true});
    sparse.id = 1;
    dense.id = 2;
    sparse.pos = {10.0, 10.0};
    dense.pos = {10.0, 10.0};
    std::vector<std::unique_ptr<producers::Producer>> sparseNewborns;
    std::vector<std::unique_ptr<producers::Producer>> denseNewborns;
    producers::ProducerContext sparseContext;
    sparseContext.environment = &environment;
    sparseContext.ledger = &ledgerA;
    sparseContext.random = &randomA;
    sparseContext.population = 1;
    sparseContext.maxPopulation = 100;
    sparseContext.localDensity = 0;
    sparseContext.newborns = &sparseNewborns;
    producers::ProducerContext denseContext = sparseContext;
    denseContext.ledger = &ledgerB;
    denseContext.random = &randomB;
    denseContext.localDensity = 100;
    denseContext.newborns = &denseNewborns;
    sparse.tick(sparseContext);
    dense.tick(denseContext);
    CHECK(sparseNewborns.size() == 1);
    CHECK(denseNewborns.empty());
}

TEST("saturating harvest honours half saturation and reserve") {
    core::SimConfig config;
    config.agent.harvest = 0.5;
    config.agent.harvestReserve = 0.0;
    config.agent.harvestHalfSaturation = 20.0;
    FakeQuery query;
    query.cfg = config;
    FakeForage forage;
    world::EnergyLedger ledger;
    auto policy = decision::PolicyRegistry::create("rule");
    CHECK(policy != nullptr);
    agentsim::Agent agent(1, {10.0, 10.0}, {0.0, 0.0}, {50.0, 100.0, true}, std::move(policy));
    decision::Action action;
    action.kind = decision::Action::Kind::Harvest;
    action.magnitude = 1.0f;
    agentsim::Behavior::execute(agent, action, query, forage, ledger);
    CHECK_NEAR(forage.lastAmount, 0.5 * 100.0 / (100.0 + 20.0), 1e-12);

    FakeQuery linearQuery;
    linearQuery.cfg = config;
    linearQuery.cfg.agent.harvestHalfSaturation = 0.0;
    FakeForage linearForage;
    auto linearPolicy = decision::PolicyRegistry::create("rule");
    agentsim::Agent linearAgent(2, {10.0, 10.0}, {0.0, 0.0}, {50.0, 100.0, true},
                                std::move(linearPolicy));
    agentsim::Behavior::execute(linearAgent, action, linearQuery, linearForage, ledger);
    CHECK_NEAR(linearForage.lastAmount, 0.5, 1e-12);
}

TEST("M2.5 configuration is deterministic and audited") {
    core::SimConfig config;
    config.producer.max = 1000.0;
    config.agent.max = 1000.0;
    config.producer.interactionRadius = 400.0;
    config.producer.competition = 0.2;
    config.producer.densityRepro = 0.02;
    config.agent.harvest = 0.5;
    config.agent.harvestReserve = 10.0;
    config.agent.harvestHalfSaturation = 20.0;
    config.agent.densityRepro = 0.01;
    config.agent.densityRadius = 200.0;
    config.agent.moveK = 0.00002;
    config.world.maxTicks = 300;
    sim::Simulation a(config);
    sim::Simulation b(config);
    for (int i = 0; i < 300; ++i) {
        a.step();
        b.step();
    }
    CHECK(a.stateHash() == b.stateHash());
    CHECK(a.auditOk());
    CHECK(a.view().stats.producers > 0);
    CHECK(a.view().stats.agents > 0);
}

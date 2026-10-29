#include "TestHarness.h"
#include "agents/Sensors.h"
#include "world/World.h"
#include <cmath>

using namespace world;

TEST("world starts with producers as the only initial biomass") {
    core::SimConfig config;
    config.producer.initial = 4;
    config.agent.initial = 2;
    config.producer.photo = 0.0;
    config.producer.maint = 0.0;
    config.agent.metab = 0.0;
    World world(config);
    CHECK(world.view().stats.producers == 4);
    CHECK(world.view().stats.agents == 2);
    CHECK_NEAR(world.energyStats().injectedPrimary(), 0.0, 0.0);
    CHECK(world.auditOk());
    world.step();
    CHECK_NEAR(world.energyStats().injectedPrimary(), 0.0, 0.0);
    CHECK(world.auditOk());
}

TEST("zero producers means no photosynthesis and eventual starvation") {
    core::SimConfig config;
    config.producer.initial = 0;
    config.agent.initial = 4;
    config.agent.initialEnergy = 5.0;
    config.agent.metab = 0.05;
    config.agent.repro = 1000.0;
    config.light.base = 2.0;
    World world(config);
    for (int i = 0; i < 2000 && !world.finished(); ++i) {
        world.step();
        CHECK(world.auditOk());
    }
    CHECK(world.view().stats.producers == 0);
    CHECK_NEAR(world.energyStats().injectedPrimary(), 0.0, 0.0);
    CHECK(world.view().stats.agents == 0);
    CHECK_NEAR(world.totalEnergy(), 0.0, 1e-9);
}

TEST("world exposes genome and population statistics") {
    core::SimConfig config;
    config.producer.initial = 2;
    config.agent.initial = 2;
    config.producer.photo = 0.0;
    config.agent.metab = 0.0;
    World world(config);
    CHECK(world.meanGenome().size() == 6);
    CHECK(world.meanProducerEnergy() > 0.0);
    CHECK(world.meanAgentEnergy() > 0.0);
    CHECK(world.agentSystem().births() == 0);
    CHECK(world.agentSystem().deaths() == 0);

    config.agent.lifespan = 1;
    World dying(config);
    dying.step();
    CHECK(dying.agentSystem().deaths() == 2);
    CHECK(dying.view().stats.agents == 0);
    CHECK(dying.auditOk());
}

TEST("world is deterministic and observations are finite") {
    core::SimConfig config;
    config.producer.initial = 6;
    config.agent.initial = 3;
    config.world.maxTicks = 300;
    World a(config);
    World b(config);
    for (int i = 0; i < 300; ++i) { a.step(); b.step(); }
    CHECK(a.stateHash() == b.stateHash());
    CHECK(a.auditOk());
    const auto& agents = a.agentSystem().agents();
    CHECK(!agents.empty());
    const decision::Observation observation = agentsim::buildObservation(agents[0], a);
    for (float value : observation.values) CHECK(std::isfinite(value));
    CHECK(observation.version == 1);
}

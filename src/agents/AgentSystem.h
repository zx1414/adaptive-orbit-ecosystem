#pragma once
#include "agents/Agent.h"
#include "core/Config.h"
#include "world/Energy.h"
#include "world/Forage.h"
#include "world/SpatialIndex.h"
#include "world/WorldQuery.h"
#include <memory>
#include <vector>

namespace agentsim {
class AgentSystem {
public:
    AgentSystem(core::Bounds bounds, core::AgentConfig config, core::PolicyConfig policyConfig);
    void initialize(core::Random& random, world::EnergyLedger& ledger);
    void tick(world::WorldQuery& query, world::Forage& forage, core::Random& random,
              world::EnergyLedger& ledger);
    void removeDead();
    std::size_t aliveCount() const;
    std::size_t births() const { return births_; }
    std::size_t deaths() const { return deaths_; }
    std::size_t size() const { return agents_.size(); }
    const std::vector<Agent>& agents() const { return agents_; }
    std::vector<Agent>& agents() { return agents_; }
    void rebuildIndex();
    const world::SpatialIndex& index() const { return index_; }
    void querySamples(core::Vec2 center, double radius, std::vector<world::AgentSample>& out) const;
    Agent* find(core::EntityId id);
    const Agent* find(core::EntityId id) const;
    std::size_t localDensity(core::Vec2 center, double radius, core::EntityId exclude) const;
private:
    core::Bounds bounds_;
    core::AgentConfig config_;
    core::PolicyConfig policyConfig_;
    std::vector<Agent> agents_;
    world::SpatialIndex index_;
    std::vector<Agent*> byId_;
    core::EntityId nextId_ = 1;
    std::size_t births_ = 0;
    std::size_t deaths_ = 0;
};
} // namespace agentsim

#pragma once
#include "agents/Agent.h"
#include "core/Config.h"
#include "world/Energy.h"
#include "world/SpatialIndex.h"
#include "world/WorldQuery.h"
#include <memory>
#include <vector>

namespace world { class World; }
namespace agentsim {
class AgentSystem {
public:
    AgentSystem(core::Bounds bounds, core::AgentConfig config, core::PolicyConfig policyConfig);
    void initialize(core::Random& random, world::EnergyLedger& ledger);
    void tick(world::World& world, core::Random& random, world::EnergyLedger& ledger);
    void removeDead();
    std::size_t aliveCount() const;
    std::size_t size() const { return agents_.size(); }
    const std::vector<Agent>& agents() const { return agents_; }
    std::vector<Agent>& agents() { return agents_; }
    void rebuildIndex();
    const world::SpatialIndex& index() const { return index_; }
    void querySamples(core::Vec2 center, double radius, std::vector<world::AgentSample>& out) const;
private:
    core::Bounds bounds_;
    core::AgentConfig config_;
    core::PolicyConfig policyConfig_;
    std::vector<Agent> agents_;
    world::SpatialIndex index_;
    core::EntityId nextId_ = 1;
};
} // namespace agentsim

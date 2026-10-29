#pragma once
#include "agents/AgentSystem.h"
#include "core/Config.h"
#include "core/Random.h"
#include "producers/ProducerSystem.h"
#include "world/Energy.h"
#include "world/Environment.h"
#include "world/Forage.h"
#include "world/WorldQuery.h"
#include "world/WorldView.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sim {
class SnapshotStore;

// Composition root. Owns every mutable subsystem and exposes only the
// read-only WorldQuery contract plus the Forage mutation used by Behavior.
class Simulation final : public world::WorldQuery, public world::Forage {
public:
    explicit Simulation(const core::SimConfig& config);
    void step();
    bool finished() const { return finished_; }
    const char* finishReason() const { return finishReason_.c_str(); }

    core::Tick tick() const override { return tick_; }
    const world::WorldView& view() const { return view_; }
    const core::SimConfig& config() const override { return config_; }
    core::Bounds bounds() const override { return environment_.bounds(); }
    double lightAt(core::Vec2 position) const override;
    void queryProducers(core::Vec2 center, double radius,
                        std::vector<world::ProducerSample>& out) const override;
    void queryAgents(core::Vec2 center, double radius,
                     std::vector<world::AgentSample>& out) const override;
    double harvest(core::EntityId producerId, double amount, double reserve,
                   core::EnergyState& to) override;

    const world::EnergyStats& energyStats() const { return ledger_.stats(); }
    double totalEnergy() const;
    double meanProducerEnergy() const;
    double meanAgentEnergy() const;
    std::vector<double> meanGenome() const;
    bool auditOk() const { return auditOk_; }
    std::uint64_t stateHash() const;
    const producers::ProducerSystem& producerSystem() const { return producers_; }
    const agentsim::AgentSystem& agentSystem() const { return agents_; }
    const world::EnergyLedger& ledger() const { return ledger_; }

private:
    friend class SnapshotStore;
    struct LoadTag {};
    Simulation(LoadTag, const core::SimConfig& config);
    void initialize();
    void updateView();

    core::SimConfig config_;
    world::Environment environment_;
    world::EnergyLedger ledger_;
    core::Random random_;
    producers::ProducerSystem producers_;
    agentsim::AgentSystem agents_;
    core::Tick tick_ = 0;
    bool finished_ = false;
    bool auditOk_ = true;
    std::string finishReason_;
    world::WorldView view_;
};
} // namespace sim
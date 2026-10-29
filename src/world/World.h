#pragma once
#include "agents/AgentSystem.h"
#include "core/Config.h"
#include "core/Random.h"
#include "producers/ProducerSystem.h"
#include "world/Energy.h"
#include "world/Environment.h"
#include "world/WorldQuery.h"
#include "world/WorldView.h"
#include <cstdint>
#include <string>

namespace world {
class World final : public WorldQuery {
public:
    explicit World(const core::SimConfig& config);
    void step();
    bool finished() const { return finished_; }
    const char* finishReason() const { return finishReason_.c_str(); }
    core::Tick tick() const override { return tick_; }
    const WorldView& view() const { return view_; }
    const core::SimConfig& config() const override { return config_; }
    core::Bounds bounds() const override { return environment_.bounds(); }
    double lightAt(core::Vec2 position) const override;
    void queryProducers(core::Vec2 center, double radius,
                        std::vector<ProducerSample>& out) const override;
    void queryAgents(core::Vec2 center, double radius,
                     std::vector<AgentSample>& out) const override;
    producers::Producer* nearestProducer(core::Vec2 position, double radius);
    const EnergyStats& energyStats() const { return ledger_.stats(); }
    double totalEnergy() const;
    bool auditOk() const { return auditOk_; }
    std::uint64_t stateHash() const;
    const producers::ProducerSystem& producerSystem() const { return producers_; }
    const agentsim::AgentSystem& agentSystem() const { return agents_; }
    const EnergyLedger& ledger() const { return ledger_; }
private:
    void initialize();
    void updateView();
    core::SimConfig config_;
    Environment environment_;
    EnergyLedger ledger_;
    core::Random random_;
    producers::ProducerSystem producers_;
    agentsim::AgentSystem agents_;
    core::Tick tick_ = 0;
    bool finished_ = false;
    bool auditOk_ = true;
    std::string finishReason_;
    WorldView view_;
};
} // namespace world

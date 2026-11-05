#pragma once
#include "colony/ColonySystem.h"
#include "core/Config.h"
#include "core/Random.h"
#include "environment/Cyanobacteria.h"
#include "world/Energy.h"
#include "world/Environment.h"
#include "world/WorldView.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sim {
class SnapshotStore;

// Composition root for the colony ecosystem:
// sunlight -> cyanobacteria (producers) -> nuclei (consumers, decision makers) -> organ balls.
class Simulation final {
public:
    explicit Simulation(const core::SimConfig& config);

    void step();
    bool finished() const { return finished_; }
    const char* finishReason() const { return finishReason_.c_str(); }

    core::Tick tick() const { return tick_; }
    const world::WorldView& view() const { return view_; }
    const core::SimConfig& config() const { return config_; }
    core::Bounds bounds() const { return environment_.bounds(); }
    double lightAt(core::Vec2 position) const { return environment_.lightAt(position, tick_); }

    const world::EnergyStats& energyStats() const { return ledger_.stats(); }
    double totalEnergy() const;
    bool auditOk() const { return auditOk_; }
    std::uint64_t stateHash() const;

    environment::Cyanobacteria& cyanobacteria() { return cyanobacteria_; }
    const environment::Cyanobacteria& cyanobacteria() const { return cyanobacteria_; }
    colony::ColonySystem& colonies() { return colonies_; }
    const colony::ColonySystem& colonies() const { return colonies_; }
    const world::EnergyLedger& ledger() const { return ledger_; }
    core::Random& random() { return random_; }

private:
    friend class SnapshotStore;
    struct LoadTag {};
    Simulation(LoadTag, const core::SimConfig& config);
    void initialize();
    void updateView();

    core::SimConfig config_;
    world::Environment environment_;
    environment::Cyanobacteria cyanobacteria_;
    colony::ColonySystem colonies_;
    world::EnergyLedger ledger_;
    core::Random random_;
    core::Tick tick_ = 0;
    bool finished_ = false;
    bool auditOk_ = true;
    std::string finishReason_;
    world::WorldView view_;
};
} // namespace sim

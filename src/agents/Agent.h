#pragma once

#include "core/Types.h"
#include "decision/IPolicy.h"

#include <cstdint>
#include <memory>

namespace agentsim {

class Behavior;
class AgentSystem;

// A simulation entity. The numeric id is never reused, including after death.
// Kinematics and the cached energy value are deliberately not publicly mutable:
// Behavior applies actions and synchronizes energy after ledger operations.
class Agent {
public:
    Agent(core::EntityId id, core::Vec2 position, core::Vec2 velocity,
          core::EnergyState energy, std::unique_ptr<decision::IPolicy> policy);
    ~Agent();

    Agent(Agent&&) noexcept;
    Agent& operator=(Agent&&) noexcept;
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    core::EntityId id() const noexcept { return id_; }
    const core::Vec2& position() const noexcept { return pos_; }
    const core::Vec2& pos() const noexcept { return pos_; }
    const core::Vec2& velocity() const noexcept { return vel_; }
    const core::Vec2& vel() const noexcept { return vel_; }
    const core::EnergyState& energyState() const noexcept { return energy_; }
    const core::EnergyState& energy() const noexcept { return energy_; }
    core::Tick age() const noexcept { return age_; }
    bool alive() const noexcept { return energy_.alive; }

    decision::IPolicy& policy() noexcept { return *policy_; }
    const decision::IPolicy& policy() const noexcept { return *policy_; }

private:
    friend class Behavior;
    friend class AgentSystem;

    std::unique_ptr<Agent> cloneForReproduction(
        core::EntityId childId, core::Vec2 childPosition,
        core::EnergyState childEnergy, double mutationRate,
        core::Random& random) const;

    core::EntityId id_;
    core::Vec2 pos_{};
    core::Vec2 vel_{};
    core::EnergyState energy_{};
    core::Tick age_ = 0;
    std::unique_ptr<decision::IPolicy> policy_;
};

} // namespace agentsim

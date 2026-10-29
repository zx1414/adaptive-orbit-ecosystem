#include "agents/Agent.h"
#include "decision/IPolicy.h"
#include <utility>

namespace agentsim {
Agent::Agent(core::EntityId id, core::Vec2 position, core::Vec2 velocity,
             core::EnergyState energy, std::unique_ptr<decision::IPolicy> policy)
    : id_(id), pos_(position), vel_(velocity), energy_(energy), policy_(std::move(policy)) {}
Agent::~Agent() = default;
Agent::Agent(Agent&&) noexcept = default;
Agent& Agent::operator=(Agent&&) noexcept = default;

std::unique_ptr<Agent> Agent::cloneForReproduction(
        core::EntityId childId, core::Vec2 childPosition,
        core::EnergyState childEnergy, double mutationRate,
        core::Random& random) const {
    if (!policy_) return nullptr;
    auto childPolicy = policy_->clone();
    if (!childPolicy) return nullptr;
    auto child = std::make_unique<Agent>(childId, childPosition, core::Vec2{}, childEnergy, std::move(childPolicy));
    child->policy_->mutate(random, static_cast<float>(mutationRate));
    return child;
}
} // namespace agentsim

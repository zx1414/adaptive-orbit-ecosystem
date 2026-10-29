#include "agents/AgentSystem.h"
#include "agents/Behavior.h"
#include "agents/Sensors.h"
#include "core/Math.h"
#include "decision/PolicyRegistry.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace agentsim {
AgentSystem::AgentSystem(core::Bounds bounds, core::AgentConfig config, core::PolicyConfig policyConfig)
    : bounds_(bounds), config_(config), policyConfig_(std::move(policyConfig)), index_(bounds) {}
void AgentSystem::initialize(core::Random& random, world::EnergyLedger& ledger) {
    agents_.clear();
    nextId_ = 1;
    const std::size_t count = config_.initial;
    for (std::size_t i = 0; i < count; ++i) {
        auto policy = decision::PolicyRegistry::create(policyConfig_.rule);
        if (!policy) throw std::runtime_error("unknown policy: " + policyConfig_.rule);
        core::EnergyState state{0.0, std::max(1e-9, static_cast<double>(config_.cap)), true};
        core::Vec2 position{random.uniformReal(bounds_.minX, bounds_.maxX),
                            random.uniformReal(bounds_.minY, bounds_.maxY)};
        agents_.emplace_back(nextId_++, position, core::Vec2{}, state, std::move(policy));
        ledger.injectInitial(agents_.back().energy_, agents_.back().id_, std::max(0.0, config_.initialEnergy));
    }
    rebuildIndex();
}
void AgentSystem::tick(world::World& world, core::Random& random, world::EnergyLedger& ledger) {
    const std::size_t initial = agents_.size();
    for (std::size_t i = 0; i < initial; ++i) {
        Agent& agent = agents_[i];
        if (!agent.alive()) continue;
        const decision::Observation observation = buildObservation(agent, world);
        const decision::Action action = agent.policy_->decide(observation);
        Behavior::execute(agent, action, world, ledger);
        ++agent.age_;
    }
    const std::size_t populationLimit = static_cast<std::size_t>(std::max(0.0, config_.max));
    for (std::size_t i = 0; i < initial; ++i) {
        Agent& parent = agents_[i];
        if (!parent.alive() || parent.energy_.energy < config_.repro) continue;
        if (agents_.size() >= populationLimit) break;
        core::EnergyState childEnergy{0.0, std::max(1e-9, static_cast<double>(config_.cap)), true};
        core::Vec2 childPosition = parent.pos_ + core::Vec2(
            random.uniformReal(-20.0, 20.0), random.uniformReal(-20.0, 20.0));
        childPosition.x = std::clamp(childPosition.x, bounds_.minX, bounds_.maxX);
        childPosition.y = std::clamp(childPosition.y, bounds_.minY, bounds_.maxY);
        auto child = parent.cloneForReproduction(nextId_++, childPosition, childEnergy,
                                                 policyConfig_.mutation, random);
        if (!child) continue;
        const double share = std::min(std::max(0.0, config_.child), parent.energy_.energy);
        const double moved = ledger.transfer(parent.energy_, child->energy_, share,
                                             world::EnergyReason::AgentReproduction);
        if (moved <= 0.0) continue;
        ledger.dissipate(parent.energy_, parent.id_, std::max(0.0, config_.cost),
                         world::EnergyReason::AgentReproduction);
        if (parent.energy_.energy <= 1e-12) ledger.kill(parent.energy_, parent.id_, world::EnergyReason::AgentDeath);
        agents_.push_back(std::move(*child));
    }
}
void AgentSystem::removeDead() {
    agents_.erase(std::remove_if(agents_.begin(), agents_.end(),
        [](const Agent& agent) { return !agent.alive(); }), agents_.end());
}
std::size_t AgentSystem::aliveCount() const {
    return static_cast<std::size_t>(std::count_if(agents_.begin(), agents_.end(),
        [](const Agent& agent) { return agent.alive(); }));
}
void AgentSystem::rebuildIndex() {
    index_.clear();
    for (const Agent& agent : agents_) if (agent.alive()) index_.insert(agent.id(), agent.pos());
}
void AgentSystem::querySamples(core::Vec2 center, double radius, std::vector<world::AgentSample>& out) const {
    out.clear();
    for (const Agent& agent : agents_) {
        if (!agent.alive()) continue;
        if ((agent.pos() - center).lengthSq() <= radius * radius) {
            out.push_back({agent.id(), agent.pos(), agent.energy().energy, agent.energy().cap});
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
}
} // namespace agentsim

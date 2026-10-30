#include "agents/AgentSystem.h"
#include "behavior/Behavior.h"
#include "perception/Sensors.h"
#include "core/Math.h"
#include "decision/PolicyRegistry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace agentsim {
AgentSystem::AgentSystem(core::Bounds bounds, core::AgentConfig config, core::PolicyConfig policyConfig)
    : bounds_(bounds), config_(config), policyConfig_(std::move(policyConfig)), index_(bounds, 200.0) {}
void AgentSystem::initialize(core::Random& random, world::EnergyLedger& ledger) {
    agents_.clear();
    const std::size_t capacity = core::Math::toSizeClamped(config_.max);
    agents_.reserve(std::min<std::size_t>(capacity, 100000));
    nextId_ = 1;
    births_ = 0;
    deaths_ = 0;
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
void AgentSystem::tick(world::WorldQuery& query, world::Forage& forage, core::Random& random,
                       world::EnergyLedger& ledger) {
    const std::size_t initial = agents_.size();
    for (std::size_t i = 0; i < initial; ++i) {
        Agent& agent = agents_[i];
        if (!agent.alive()) continue;
        const decision::Observation observation = buildObservation(agent, query);
        const decision::Action action = agent.policy_->decide(observation);
        Behavior::execute(agent, action, query, forage, ledger);
        ++agent.age_;
        if (config_.lifespan > 0 && agent.alive() && agent.age_ >= config_.lifespan) {
            ledger.kill(agent.energy_, agent.id_, world::EnergyReason::AgentDeath);
        }
    }
    const std::size_t populationLimit = core::Math::toSizeClamped(config_.max);
    for (std::size_t i = 0; i < initial; ++i) {
        Agent& parent = agents_[i];
        if (!parent.alive()) continue;
        const double localAgents = config_.densityRepro > 0.0 && config_.densityRadius > 0.0
            ? static_cast<double>(localDensity(parent.pos_, config_.densityRadius, parent.id()))
            : 0.0;
        const double reproThreshold = config_.repro *
            (1.0 + std::max(0.0, config_.densityRepro) * localAgents);
        if (!std::isfinite(reproThreshold) || parent.energy_.energy < reproThreshold) continue;
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
        ++births_;
    }
}
void AgentSystem::removeDead() {
    const std::size_t before = agents_.size();
    agents_.erase(std::remove_if(agents_.begin(), agents_.end(),
        [](const Agent& agent) { return !agent.alive(); }), agents_.end());
    deaths_ += before - agents_.size();
}
std::size_t AgentSystem::aliveCount() const {
    return static_cast<std::size_t>(std::count_if(agents_.begin(), agents_.end(),
        [](const Agent& agent) { return agent.alive(); }));
}
void AgentSystem::rebuildIndex() {
    index_.clear();
    byId_.assign(static_cast<std::size_t>(nextId_), nullptr);
    for (Agent& agent : agents_) {
        if (!agent.alive()) continue;
        index_.insert(agent.id(), agent.pos());
        if (agent.id() < byId_.size()) byId_[static_cast<std::size_t>(agent.id())] = &agent;
    }
}
Agent* AgentSystem::find(core::EntityId id) {
    return id < byId_.size() ? byId_[static_cast<std::size_t>(id)] : nullptr;
}
const Agent* AgentSystem::find(core::EntityId id) const {
    return id < byId_.size() ? byId_[static_cast<std::size_t>(id)] : nullptr;
}
std::size_t AgentSystem::localDensity(core::Vec2 center, double radius, core::EntityId exclude) const {
    static thread_local std::vector<core::EntityId> ids;
    index_.queryUnsorted(center, radius, ids);
    // Count living agents whose *current* position is inside the radius. The index cell
    // membership is only a candidate filter, and dead agents must not raise the threshold.
    std::size_t count = 0;
    for (const core::EntityId id : ids) {
        if (id == exclude) continue;
        const Agent* agent = find(id);
        if (!agent || !agent->alive()) continue;
        if ((agent->pos() - center).lengthSq() > radius * radius) continue;
        ++count;
    }
    return count;
}

void AgentSystem::querySamples(core::Vec2 center, double radius, std::vector<world::AgentSample>& out) const {
    out.clear();
    if (!core::Math::isFinite(center) || !std::isfinite(radius) || radius <= 0.0) return;
    static thread_local std::vector<core::EntityId> ids;
    index_.queryUnsorted(center, radius, ids);
    out.reserve(ids.size());
    for (const core::EntityId id : ids) {
        const Agent* agent = find(id);
        if (!agent || !agent->alive()) continue;
        // The index holds positions captured at the start of the tick, but agents move during
        // the tick. Re-test the current position so perception never reports a body that has
        // already left the radius and never drops one that has entered it.
        if ((agent->pos() - center).lengthSq() > radius * radius) continue;
        out.push_back({agent->id(), agent->pos(), agent->energy().energy, agent->energy().cap});
    }
}
} // namespace agentsim

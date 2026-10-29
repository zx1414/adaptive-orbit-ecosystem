#include "agents/Behavior.h"
#include "core/Math.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>

namespace agentsim {
void Behavior::execute(Agent& agent, const decision::Action& intent,
                       world::World& world, world::EnergyLedger& ledger) {
    if (!agent.alive()) return;
    const core::SimConfig& config = world.config();
    const core::Bounds bounds = world.bounds();
    const double maxSpeed = std::max(0.0, config.agent.maxSpeed);
    decision::Action action = intent;
    if (!std::isfinite(action.dirX) || !std::isfinite(action.dirY)) {
        action.dirX = 0.0f;
        action.dirY = 0.0f;
    }
    if (!std::isfinite(action.magnitude)) action.magnitude = 0.0f;
    action.magnitude = std::clamp(action.magnitude, 0.0f, 1.0f);
    if (action.kind == decision::Action::Kind::Harvest) {
        producers::Producer* target = world.nearestProducer(agent.pos_, config.agent.radius);
        if (target && target->alive()) {
            const core::Vec2 delta = target->pos - agent.pos_;
            const core::Vec2 direction = core::Math::unit(delta);
            agent.vel_ = core::Math::limit(agent.vel_ * 0.5 + direction * (maxSpeed * 0.15), maxSpeed);
            const double requested = std::max(0.0, config.agent.harvest);
            ledger.transfer(target->energy, agent.energy_, requested, world::EnergyReason::AgentHarvest);
            if (target->energy.energy <= 1e-12) ledger.kill(target->energy, target->id, world::EnergyReason::ProducerDeath);
        }
    } else if (action.kind == decision::Action::Kind::Move) {
        const core::Vec2 direction = core::Math::unit({action.dirX, action.dirY});
        const core::Vec2 desired = direction * (maxSpeed * action.magnitude);
        const double steering = std::clamp(1.0 - config.agent.damping, 0.02, 1.0);
        agent.vel_ += (desired - agent.vel_) * steering;
        agent.vel_ = core::Math::limit(agent.vel_, maxSpeed);
    } else {
        agent.vel_ *= 0.5;
    }
    agent.pos_ += agent.vel_;
    core::Math::bounce(agent.pos_, agent.vel_, bounds, 0.8, std::max(0.0, config.agent.radius * 0.1));
    const double speed = agent.vel_.length();
    const double moveCost = std::max(0.0, config.agent.moveK) * speed * speed;
    if (moveCost > 0.0) ledger.dissipate(agent.energy_, agent.id_, moveCost, world::EnergyReason::AgentMove);
    const double metabolism = std::max(0.0, config.agent.metab) *
        (action.kind == decision::Action::Kind::Rest ? 0.5 : 1.0);
    if (metabolism > 0.0) ledger.dissipate(agent.energy_, agent.id_, metabolism, world::EnergyReason::AgentMetabolism);
    if (agent.energy_.energy <= 1e-12) ledger.kill(agent.energy_, agent.id_, world::EnergyReason::AgentDeath);
}
} // namespace agentsim

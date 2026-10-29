#include "agents/Behavior.h"
#include "core/Math.h"
#include "world/Forage.h"
#include "world/WorldQuery.h"
#include <algorithm>
#include <cmath>

namespace agentsim {
void Behavior::execute(Agent& agent, const decision::Action& intent,
                       const world::WorldQuery& query, world::Forage& forage,
                       world::EnergyLedger& ledger) {
    if (!agent.alive()) return;
    const core::SimConfig& config = query.config();
    const core::Bounds bounds = query.bounds();
    const double maxSpeed = std::max(0.0, config.agent.maxSpeed);
    decision::Action action = intent;
    if (!std::isfinite(action.dirX) || !std::isfinite(action.dirY)) {
        action.dirX = 0.0f;
        action.dirY = 0.0f;
    }
    if (!std::isfinite(action.magnitude)) action.magnitude = 0.0f;
    action.magnitude = std::clamp(action.magnitude, 0.0f, 1.0f);
    if (action.kind == decision::Action::Kind::Harvest) {
        std::vector<world::ProducerSample> producers;
        query.queryProducers(agent.pos_, config.agent.radius, producers);
        const world::ProducerSample* target = nullptr;
        double bestDistanceSq = 0.0;
        for (const auto& sample : producers) {
            if (sample.id == core::kInvalidEntityId || sample.energy <= 0.0) continue;
            const double distanceSq = (sample.position - agent.pos_).lengthSq();
            if (target == nullptr || distanceSq < bestDistanceSq ||
                (distanceSq == bestDistanceSq && sample.id < target->id)) {
                target = &sample;
                bestDistanceSq = distanceSq;
            }
        }
        if (target != nullptr) {
            const core::Vec2 delta = target->position - agent.pos_;
            const core::Vec2 direction = core::Math::unit(delta);
            agent.vel_ = core::Math::limit(agent.vel_ * 0.5 + direction * (maxSpeed * 0.15), maxSpeed);
            const double reserve = std::max(0.0, config.agent.harvestReserve);
            const double food = std::max(0.0, target->energy - reserve);
            double requested = std::min(std::max(0.0, config.agent.harvest), food);
            const double halfSaturation = std::max(0.0, config.agent.harvestHalfSaturation);
            if (halfSaturation > 0.0) {
                requested = std::min(requested, std::max(0.0, config.agent.harvest) *
                                                    food / (food + halfSaturation));
            }
            if (requested > 0.0) forage.harvest(target->id, requested, reserve, agent.energy_);
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

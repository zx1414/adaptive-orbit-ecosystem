#include "sim/Simulation.h"
#include "core/Math.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace sim {
namespace {
core::SimConfig normalizedConfig(core::SimConfig config) {
    if (!std::isfinite(config.world.width) || !(config.world.width > 0.0)) config.world.width = 2000.0;
    if (!std::isfinite(config.world.height) || !(config.world.height > 0.0)) config.world.height = 2000.0;
    return config;
}
std::uint64_t hashBytes(std::uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}
std::uint64_t hashDouble(std::uint64_t hash, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return hashBytes(hash, &bits, sizeof(bits));
}
} // namespace

Simulation::Simulation(const core::SimConfig& config)
    : config_(normalizedConfig(config)),
      environment_(config_.world.bounds(), config_.light),
      random_(config_.world.seed),
      producers_(config_.world.bounds(), config_.producer),
      agents_(config_.world.bounds(), config_.agent, config_.policy) {
    initialize();
}

void Simulation::initialize() {
    ledger_.reset();
    producers_.initialize(random_, ledger_, environment_);
    agents_.initialize(random_, ledger_);
    ledger_.sealInitial();
    tick_ = 0;
    finished_ = false;
    finishReason_.clear();
    auditOk_ = true;
    updateView();
}

void Simulation::step() {
    if (finished_) return;
    producers_.tick(random_, ledger_, environment_, tick_);
    agents_.tick(*this, *this, random_, ledger_);
    producers_.removeDead();
    agents_.removeDead();
    producers_.rebuildIndex();
    agents_.rebuildIndex();
    ++tick_;
    const double stored = totalEnergy();
    auditOk_ = ledger_.audit(stored, 1e-9 * std::max(1.0, ledger_.stats().injected));
    if (!auditOk_) {
        finished_ = true;
        finishReason_ = "能量审计失败";
    } else if (agents_.aliveCount() == 0) {
        finished_ = true;
        finishReason_ = "所有 Agent 已死亡";
    } else if (tick_ >= config_.world.maxTicks) {
        finished_ = true;
        finishReason_ = "达到最大 tick";
    }
    updateView();
}

double Simulation::lightAt(core::Vec2 position) const { return environment_.lightAt(position, tick_); }

void Simulation::queryProducers(core::Vec2 center, double radius,
                                std::vector<world::ProducerSample>& out) const {
    producers_.querySamples(center, radius, out);
}

void Simulation::queryAgents(core::Vec2 center, double radius,
                             std::vector<world::AgentSample>& out) const {
    agents_.querySamples(center, radius, out);
}

double Simulation::harvest(core::EntityId producerId, double amount, double reserve,
                           core::EnergyState& to) {
    producers::Producer* producer = producers_.find(producerId);
    if (!producer || !producer->alive()) return 0.0;
    const double available = std::max(0.0, producer->energy.energy - std::max(0.0, reserve));
    const double requested = std::min(std::max(0.0, amount), available);
    if (requested <= 0.0) return 0.0;
    const double moved = ledger_.transfer(producer->energy, to, requested,
                                          world::EnergyReason::AgentHarvest);
    if (producer->energy.energy <= 1e-12) {
        ledger_.kill(producer->energy, producer->id, world::EnergyReason::ProducerDeath);
    }
    return moved;
}

double Simulation::totalEnergy() const {
    double total = 0.0;
    for (const auto& producer : producers_.all()) if (producer && producer->alive()) total += producer->energy.energy;
    for (const auto& agent : agents_.agents()) if (agent.alive()) total += agent.energy().energy;
    return total;
}

double Simulation::meanProducerEnergy() const {
    double total = 0.0;
    std::size_t count = 0;
    for (const auto& producer : producers_.all()) {
        if (!producer || !producer->alive()) continue;
        total += producer->energy.energy;
        ++count;
    }
    return count > 0 ? total / static_cast<double>(count) : 0.0;
}

double Simulation::meanAgentEnergy() const {
    double total = 0.0;
    std::size_t count = 0;
    for (const auto& agent : agents_.agents()) {
        if (!agent.alive()) continue;
        total += agent.energy().energy;
        ++count;
    }
    return count > 0 ? total / static_cast<double>(count) : 0.0;
}

std::vector<double> Simulation::meanGenome() const {
    std::vector<double> sums;
    std::vector<std::size_t> counts;
    for (const auto& agent : agents_.agents()) {
        if (!agent.alive()) continue;
        const std::vector<float> genome = agent.policy().genome();
        if (sums.size() < genome.size()) {
            sums.resize(genome.size(), 0.0);
            counts.resize(genome.size(), 0);
        }
        for (std::size_t i = 0; i < genome.size(); ++i) {
            if (!std::isfinite(genome[i])) continue;
            sums[i] += genome[i];
            ++counts[i];
        }
    }
    for (std::size_t i = 0; i < sums.size(); ++i) {
        if (counts[i] > 0) sums[i] /= static_cast<double>(counts[i]);
    }
    return sums;
}

std::uint64_t Simulation::stateHash() const {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = hashBytes(hash, &tick_, sizeof(tick_));
    const core::RandomState rng = random_.state();
    hash = hashBytes(hash, &rng.state, sizeof(rng.state));
    hash = hashBytes(hash, &rng.increment, sizeof(rng.increment));
    hash = hashDouble(hash, rng.cachedNormal);
    hash = hashBytes(hash, &rng.hasCachedNormal, sizeof(rng.hasCachedNormal));
    for (const auto& producer : producers_.all()) {
        if (!producer || !producer->alive()) continue;
        hash = hashBytes(hash, &producer->id, sizeof(producer->id));
        hash = hashDouble(hash, producer->pos.x);
        hash = hashDouble(hash, producer->pos.y);
        hash = hashDouble(hash, producer->energy.energy);
    }
    for (const auto& agent : agents_.agents()) {
        if (!agent.alive()) continue;
        const core::EntityId id = agent.id();
        hash = hashBytes(hash, &id, sizeof(id));
        hash = hashDouble(hash, agent.position().x);
        hash = hashDouble(hash, agent.position().y);
        hash = hashDouble(hash, agent.velocity().x);
        hash = hashDouble(hash, agent.velocity().y);
        hash = hashDouble(hash, agent.energy().energy);
        const auto genome = agent.policy().genome();
        for (float value : genome) { const double d = value; hash = hashDouble(hash, d); }
    }
    hash = hashDouble(hash, ledger_.stats().injected);
    hash = hashDouble(hash, ledger_.stats().dissipated);
    hash = hashDouble(hash, totalEnergy());
    return hash;
}

void Simulation::updateView() {
    view_.bounds = environment_.bounds();
    view_.producers.clear();
    view_.agents.clear();
    view_.producers.reserve(producers_.size());
    view_.agents.reserve(agents_.size());
    for (const auto& producer : producers_.all()) {
        if (!producer || !producer->alive()) continue;
        view_.producers.push_back({producer->id, producer->pos, producer->energy.energy, producer->energy.cap});
    }
    for (const auto& agent : agents_.agents()) {
        if (!agent.alive()) continue;
        view_.agents.push_back({agent.id(), agent.position(), agent.energy().energy,
                                agent.energy().cap, agent.policy().spec().id});
    }
    view_.stats.tick = tick_;
    view_.stats.producers = producers_.aliveCount();
    view_.stats.agents = agents_.aliveCount();
    view_.stats.totalEnergy = totalEnergy();
    view_.stats.injected = ledger_.stats().injected;
    view_.stats.dissipated = ledger_.stats().dissipated;
    view_.stats.transferred = ledger_.stats().transferred;
    view_.stats.auditOk = auditOk_;
    view_.stats.finished = finished_;
    view_.stats.finishReason = finishReason_;
}
} // namespace sim

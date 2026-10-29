#include "world/World.h"
#include "core/Math.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace world {
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
}
World::World(const core::SimConfig& config)
    : config_(normalizedConfig(config)),
      environment_(config_.world.bounds(), config_.light),
      random_(config_.world.seed),
      producers_(config_.world.bounds(), config_.producer),
      agents_(config_.world.bounds(), config_.agent, config_.policy) {
    initialize();
}
void World::initialize() {
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
void World::step() {
    if (finished_) return;
    producers_.tick(random_, ledger_, environment_, tick_);
    agents_.tick(*this, random_, ledger_);
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
double World::lightAt(core::Vec2 position) const { return environment_.lightAt(position, tick_); }
void World::queryProducers(core::Vec2 center, double radius, std::vector<ProducerSample>& out) const {
    out.clear();
    if (!std::isfinite(radius) || radius <= 0.0) return;
    const double radiusSq = radius * radius;
    for (const auto& producer : producers_.all()) {
        if (!producer || !producer->alive()) continue;
        if ((producer->pos - center).lengthSq() <= radiusSq) {
            out.push_back({producer->id, producer->pos, producer->energy.energy, producer->energy.cap});
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
}
void World::queryAgents(core::Vec2 center, double radius, std::vector<AgentSample>& out) const {
    agents_.querySamples(center, radius, out);
}
producers::Producer* World::nearestProducer(core::Vec2 position, double radius) {
    producers::Producer* nearest = nullptr;
    double best = radius * radius;
    for (auto& producer : producers_.all()) {
        if (!producer || !producer->alive()) continue;
        const double distanceSq = (producer->pos - position).lengthSq();
        if (distanceSq <= best) {
            best = distanceSq;
            nearest = producer.get();
        }
    }
    return nearest;
}
double World::totalEnergy() const {
    double total = 0.0;
    for (const auto& producer : producers_.all()) if (producer && producer->alive()) total += producer->energy.energy;
    for (const auto& agent : agents_.agents()) if (agent.alive()) total += agent.energy().energy;
    return total;
}
std::uint64_t World::stateHash() const {
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
void World::updateView() {
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
} // namespace world

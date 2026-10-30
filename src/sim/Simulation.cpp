#include "sim/Simulation.h"

#include "behavior/ColonyBehavior.h"
#include "colony/Orbit.h"
#include "core/Math.h"
#include "decision/PolicyRegistry.h"
#include "perception/ColonySensors.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
std::uint64_t hashString(std::uint64_t hash, const std::string& value) {
    const std::uint64_t size = static_cast<std::uint64_t>(value.size());
    hash = hashBytes(hash, &size, sizeof(size));
    return hashBytes(hash, value.data(), value.size());
}
void hashConfig(std::uint64_t& hash, const core::SimConfig& config) {
    hash = hashDouble(hash, config.world.width);
    hash = hashDouble(hash, config.world.height);
    const std::uint64_t seed = config.world.seed;
    hash = hashBytes(hash, &seed, sizeof(seed));
    const std::uint64_t maxTicks = config.world.maxTicks;
    hash = hashBytes(hash, &maxTicks, sizeof(maxTicks));
    hash = hashDouble(hash, config.light.base);
    hash = hashDouble(hash, config.light.amplitude);
    const std::uint64_t period = config.light.period;
    hash = hashBytes(hash, &period, sizeof(period));
    hash = hashDouble(hash, config.light.phase);
    hash = hashDouble(hash, config.light.halfSaturation);
    hash = hashDouble(hash, config.light.verticalGradient);
    const std::uint64_t cyanoInitial = config.cyano.initial;
    hash = hashBytes(hash, &cyanoInitial, sizeof(cyanoInitial));
    hash = hashDouble(hash, config.cyano.cap);
    hash = hashDouble(hash, config.cyano.photoRate);
    hash = hashDouble(hash, config.cyano.lightHalfSaturation);
    hash = hashDouble(hash, config.cyano.metabolism);
    hash = hashDouble(hash, config.cyano.reproThreshold);
    hash = hashDouble(hash, config.cyano.reproProbability);
    const std::uint64_t initialNuclei = config.colony.initialNuclei;
    hash = hashBytes(hash, &initialNuclei, sizeof(initialNuclei));
    hash = hashDouble(hash, config.colony.initialEnergy);
    hash = hashDouble(hash, config.colony.deployCost);
    hash = hashDouble(hash, config.colony.recycleReturn);
    hash = hashDouble(hash, config.colony.metabolism);
    hash = hashDouble(hash, config.colony.preyRadius);
    hash = hashString(hash, config.policy.rule);
}
} // namespace

Simulation::Simulation(const core::SimConfig& config) : Simulation(LoadTag{}, config) { initialize(); }

Simulation::Simulation(LoadTag, const core::SimConfig& config)
    : config_(normalizedConfig(config)),
      environment_(config_.world.bounds(), config_.light),
      cyanobacteria_(config_.world.bounds(), config_.cyano),
      colonies_(config_.world.bounds(), config_.colony),
      random_(config_.world.seed) {}

void Simulation::initialize() {
    ledger_.reset();
    cyanobacteria_.initialize(random_, ledger_);
    colonies_.initialize(random_, ledger_);
    ledger_.sealInitial();
    tick_ = 0;
    finished_ = false;
    finishReason_.clear();
    auditOk_ = true;
    updateView();
    if (config_.world.maxTicks == 0) {
        finished_ = true;
        finishReason_ = "达到最大 tick";
        updateView();
    }
}

void Simulation::step() {
    if (finished_) return;

    // 1. Environment layer: the only primary energy source.
    cyanobacteria_.tick(random_, ledger_, environment_, tick_);

    // 2. Perception and decision per nucleus, then behaviour. Fixed iteration order.
    auto policy = decision::PolicyRegistry::create(config_.policy.rule);
    for (std::size_t i = 0; i < colonies_.colonies().size(); ++i) {
        colony::Nucleus& nucleus = colonies_.colonies()[i].nucleus;
        if (!nucleus.alive()) continue;
        if (!policy) break;
        const decision::Observation observation = agentsim::buildColonyObservation(
            colonies_.colonies()[i], colonies_, cyanobacteria_, environment_.bounds(), tick_,
            config_.light, random_);
        const decision::Action action = policy->decide(observation);
        agentsim::executeColonyAction(colonies_, i, action, cyanobacteria_, ledger_, random_,
                                      config_);
    }

    // 3. Orbital integration for every owned ball.
    colonies_.integrateBalls(ledger_);

    // 4. Ownership, adoption, upkeep, reproduction, cleanup.
    colonies_.updateOwnership();
    agentsim::adoptFreeBalls(colonies_);
    agentsim::applyColonyMetabolism(colonies_, ledger_, config_);
    agentsim::reproduceColonies(colonies_, ledger_, random_, config_);
    colonies_.removeDead(ledger_);
    colonies_.rebuildIndex();

    ++tick_;
    const double stored = totalEnergy();
    auditOk_ = ledger_.audit(stored, 1e-9 * std::max(1.0, ledger_.stats().injected));
    if (!auditOk_) {
        finished_ = true;
        finishReason_ = "能量审计失败";
    } else if (colonies_.aliveNuclei() == 0) {
        finished_ = true;
        finishReason_ = "所有核已死亡";
    } else if (tick_ >= config_.world.maxTicks) {
        finished_ = true;
        finishReason_ = "达到最大 tick";
    }
    updateView();
}

double Simulation::totalEnergy() const {
    return cyanobacteria_.totalEnergy() + colonies_.nucleusEnergy() + colonies_.ballEnergy();
}

std::uint64_t Simulation::stateHash() const {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = hashBytes(hash, &tick_, sizeof(tick_));
    const core::RandomState rng = random_.state();
    hash = hashBytes(hash, &rng.state, sizeof(rng.state));
    hash = hashBytes(hash, &rng.increment, sizeof(rng.increment));
    hash = hashDouble(hash, rng.cachedNormal);
    hash = hashBytes(hash, &rng.hasCachedNormal, sizeof(rng.hasCachedNormal));
    hashConfig(hash, config_);
    for (const auto& node : cyanobacteria_.all()) {
        if (!node.alive()) continue;
        hash = hashBytes(hash, &node.id, sizeof(node.id));
        hash = hashDouble(hash, node.pos.x);
        hash = hashDouble(hash, node.pos.y);
        hash = hashDouble(hash, node.energy.energy);
    }
    for (const auto& colony : colonies_.colonies()) {
        if (!colony.nucleus.alive()) continue;
        const auto& n = colony.nucleus;
        hash = hashBytes(hash, &n.id, sizeof(n.id));
        hash = hashDouble(hash, n.pos.x);
        hash = hashDouble(hash, n.pos.y);
        hash = hashDouble(hash, n.vel.x);
        hash = hashDouble(hash, n.vel.y);
        hash = hashDouble(hash, n.energy.energy);
        hash = hashDouble(hash, n.params.orbitRadius);
        hash = hashDouble(hash, n.params.affinity);
        hash = hashDouble(hash, n.params.maxSpeed);
        hash = hashDouble(hash, n.params.energyThreshold);
        hash = hashDouble(hash, n.params.mutationRate);
        hash = hashDouble(hash, n.params.spotRadius);
    }
    for (const auto& ball : colonies_.balls()) {
        if (!ball.alive()) continue;
        hash = hashBytes(hash, &ball.id, sizeof(ball.id));
        hash = hashBytes(hash, &ball.ownerId, sizeof(ball.ownerId));
        const std::uint8_t type = static_cast<std::uint8_t>(ball.type);
        hash = hashBytes(hash, &type, sizeof(type));
        hash = hashDouble(hash, ball.pos.x);
        hash = hashDouble(hash, ball.pos.y);
        hash = hashDouble(hash, ball.energy.energy);
    }
    hash = hashDouble(hash, ledger_.stats().injected);
    hash = hashDouble(hash, ledger_.stats().dissipated);
    hash = hashDouble(hash, ledger_.stats().transferred);
    for (std::size_t i = 0; i < world::kEnergyReasonCount; ++i) {
        hash = hashDouble(hash, ledger_.stats().reasons[i].in);
        hash = hashDouble(hash, ledger_.stats().reasons[i].out);
    }
    hash = hashDouble(hash, totalEnergy());
    return hash;
}

void Simulation::updateView() {
    view_.bounds = environment_.bounds();
    view_.cyanobacteria.clear();
    view_.nuclei.clear();
    view_.balls.clear();
    for (const auto& node : cyanobacteria_.all()) {
        if (!node.alive()) continue;
        view_.cyanobacteria.push_back({node.id, node.pos, node.energy.energy, node.energy.cap});
    }
    for (const auto& colony : colonies_.colonies()) {
        if (!colony.nucleus.alive()) continue;
        world::NucleusView nucleus;
        nucleus.id = colony.nucleus.id;
        nucleus.position = colony.nucleus.pos;
        nucleus.velocity = colony.nucleus.vel;
        nucleus.energy = colony.nucleus.energy.energy;
        nucleus.cap = colony.nucleus.energy.cap;
        nucleus.orbitRadius = colony.nucleus.params.orbitRadius;
        view_.nuclei.push_back(nucleus);
    }
    for (const auto& ball : colonies_.balls()) {
        if (!ball.alive()) continue;
        world::BallView view;
        view.id = ball.id;
        view.ownerId = ball.ownerId;
        view.type = static_cast<std::uint8_t>(ball.type);
        view.position = ball.pos;
        view.energy = ball.energy.energy;
        view.cap = ball.energy.cap;
        view_.balls.push_back(view);
    }
    view_.stats.tick = tick_;
    view_.stats.cyanobacteria = cyanobacteria_.aliveCount();
    view_.stats.nuclei = colonies_.aliveNuclei();
    view_.stats.balls = colonies_.aliveBalls();
    view_.stats.totalEnergy = totalEnergy();
    view_.stats.injected = ledger_.stats().injected;
    view_.stats.dissipated = ledger_.stats().dissipated;
    view_.stats.transferred = ledger_.stats().transferred;
    view_.stats.auditOk = auditOk_;
    view_.stats.finished = finished_;
    view_.stats.finishReason = finishReason_;
}

} // namespace sim

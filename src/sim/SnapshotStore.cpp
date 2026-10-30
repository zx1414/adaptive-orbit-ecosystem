#include "sim/SnapshotStore.h"

#include "sim/Snapshot.h"
#include "sim/Simulation.h"

#include "agents/Agent.h"
#include "decision/PolicyRegistry.h"
#include "producers/Producer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sim {
namespace {
using snapshot::Reader;
using snapshot::Writer;

constexpr std::uint64_t kMaxEntities = 1000000ULL;
constexpr std::uint64_t kMaxNextId = 10000000ULL;
constexpr std::uint32_t kMaxGenome = 4096U;
constexpr std::uint32_t kMaxPolicyState = 1024U * 1024U;

bool fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}
bool finiteNonNegative(double value) { return std::isfinite(value) && value >= 0.0; }
bool finitePositive(double value) { return std::isfinite(value) && value > 0.0; }

void writeConfig(Writer& writer, const core::SimConfig& config) {
    writer.f64(config.world.width);
    writer.f64(config.world.height);
    writer.u64(config.world.seed);
    writer.u64(config.world.maxTicks);

    writer.f64(config.light.base);
    writer.f64(config.light.amplitude);
    writer.u64(config.light.period);
    writer.f64(config.light.phase);
    writer.f64(config.light.halfSaturation);
    writer.f64(config.light.verticalGradient);

    writer.u64(static_cast<std::uint64_t>(config.producer.initial));
    writer.u64(static_cast<std::uint64_t>(config.producer.cap));
    writer.f64(config.producer.initialEnergy);
    writer.f64(config.producer.photo);
    writer.f64(config.producer.maint);
    writer.f64(config.producer.repro);
    writer.f64(config.producer.share);
    writer.f64(config.producer.max);
    writer.f64(config.producer.radius);
    writer.f64(config.producer.crowding);
    writer.f64(config.producer.interactionRadius);
    writer.f64(config.producer.competition);
    writer.f64(config.producer.densityRepro);

    writer.u64(static_cast<std::uint64_t>(config.agent.initial));
    writer.u64(static_cast<std::uint64_t>(config.agent.cap));
    writer.f64(config.agent.initialEnergy);
    writer.f64(config.agent.metab);
    writer.f64(config.agent.maxSpeed);
    writer.f64(config.agent.moveK);
    writer.f64(config.agent.harvest);
    writer.f64(config.agent.harvestReserve);
    writer.f64(config.agent.harvestHalfSaturation);
    writer.f64(config.agent.densityRepro);
    writer.f64(config.agent.densityRadius);
    writer.f64(config.agent.radius);
    writer.f64(config.agent.repro);
    writer.f64(config.agent.cost);
    writer.f64(config.agent.child);
    writer.f64(config.agent.max);
    writer.u64(config.agent.lifespan);
    writer.f64(config.agent.damping);
    writer.f64(config.agent.avoidanceRadius);

    writer.u64(static_cast<std::uint64_t>(config.cyano.initial));
    writer.u64(static_cast<std::uint64_t>(config.cyano.maxNodes));
    writer.f64(config.cyano.initialEnergy);
    writer.f64(config.cyano.cap);
    writer.f64(config.cyano.photoRate);
    writer.f64(config.cyano.lightHalfSaturation);
    writer.f64(config.cyano.metabolism);
    writer.f64(config.cyano.reproThreshold);
    writer.f64(config.cyano.reproProbability);
    writer.f64(config.cyano.reproOffset);
    writer.f64(config.cyano.minSeparation);

    writer.str(config.policy.rule);
    writer.f64(config.policy.mutation);
    writer.f64(config.policy.senseRadius);
    writer.u64(static_cast<std::uint64_t>(config.policy.slots));
    writer.f64(config.policy.damping);
}

bool readConfig(Reader& reader, core::SimConfig& config, std::string& error) {
    std::uint64_t initial = 0;
    std::uint64_t cap = 0;
    std::uint64_t period = 0;
    std::uint64_t sizeValue = 0;
    std::uint64_t lifetime = 0;

    reader.f64(config.world.width);
    reader.f64(config.world.height);
    reader.u64(config.world.seed);
    reader.u64(config.world.maxTicks);
    reader.f64(config.light.base);
    reader.f64(config.light.amplitude);
    reader.u64(period);
    reader.f64(config.light.phase);
    reader.f64(config.light.halfSaturation);
    reader.f64(config.light.verticalGradient);

    reader.u64(initial);
    if (initial > kMaxEntities) return fail(error, "producer.initial exceeds limit");
    config.producer.initial = static_cast<std::size_t>(initial);
    reader.u64(cap);
    if (cap > kMaxEntities || cap == 0) return fail(error, "producer.cap is invalid");
    config.producer.cap = static_cast<std::size_t>(cap);
    reader.f64(config.producer.initialEnergy);
    reader.f64(config.producer.photo);
    reader.f64(config.producer.maint);
    reader.f64(config.producer.repro);
    reader.f64(config.producer.share);
    reader.f64(config.producer.max);
    reader.f64(config.producer.radius);
    reader.f64(config.producer.crowding);
    reader.f64(config.producer.interactionRadius);
    reader.f64(config.producer.competition);
    reader.f64(config.producer.densityRepro);

    reader.u64(initial);
    if (initial > kMaxEntities) return fail(error, "agent.initial exceeds limit");
    config.agent.initial = static_cast<std::size_t>(initial);
    reader.u64(cap);
    if (cap > kMaxEntities || cap == 0) return fail(error, "agent.cap is invalid");
    config.agent.cap = static_cast<std::size_t>(cap);
    reader.f64(config.agent.initialEnergy);
    reader.f64(config.agent.metab);
    reader.f64(config.agent.maxSpeed);
    reader.f64(config.agent.moveK);
    reader.f64(config.agent.harvest);
    reader.f64(config.agent.harvestReserve);
    reader.f64(config.agent.harvestHalfSaturation);
    reader.f64(config.agent.densityRepro);
    reader.f64(config.agent.densityRadius);
    reader.f64(config.agent.radius);
    reader.f64(config.agent.repro);
    reader.f64(config.agent.cost);
    reader.f64(config.agent.child);
    reader.f64(config.agent.max);
    reader.u64(lifetime);
    config.agent.lifespan = static_cast<core::Tick>(lifetime);
    reader.f64(config.agent.damping);
    reader.f64(config.agent.avoidanceRadius);

    reader.u64(initial);
    if (initial > kMaxEntities) return fail(error, "cyano.initial exceeds limit");
    config.cyano.initial = static_cast<std::size_t>(initial);
    reader.u64(cap);
    if (cap > kMaxEntities || cap == 0) return fail(error, "cyano.maxNodes is invalid");
    config.cyano.maxNodes = static_cast<std::size_t>(cap);
    reader.f64(config.cyano.initialEnergy);
    reader.f64(config.cyano.cap);
    reader.f64(config.cyano.photoRate);
    reader.f64(config.cyano.lightHalfSaturation);
    reader.f64(config.cyano.metabolism);
    reader.f64(config.cyano.reproThreshold);
    reader.f64(config.cyano.reproProbability);
    reader.f64(config.cyano.reproOffset);
    reader.f64(config.cyano.minSeparation);
    if (!reader.ok()) return fail(error, reader.error());
    if (!finiteNonNegative(config.cyano.initialEnergy) || !finitePositive(config.cyano.cap) ||
        !finiteNonNegative(config.cyano.photoRate) ||
        !finiteNonNegative(config.cyano.lightHalfSaturation) ||
        !finiteNonNegative(config.cyano.metabolism) ||
        !finiteNonNegative(config.cyano.reproThreshold) ||
        !finiteNonNegative(config.cyano.reproProbability) ||
        !finiteNonNegative(config.cyano.reproOffset) ||
        !finiteNonNegative(config.cyano.minSeparation)) {
        return fail(error, "cyanobacteria configuration is invalid");
    }

    reader.str(config.policy.rule);
    reader.f64(config.policy.mutation);
    reader.f64(config.policy.senseRadius);
    reader.u64(sizeValue);
    if (sizeValue > kMaxEntities) return fail(error, "policy.slots exceeds limit");
    config.policy.slots = static_cast<std::size_t>(sizeValue);
    reader.f64(config.policy.damping);
    if (!reader.ok()) return fail(error, reader.error());

    if (!finitePositive(config.world.width) || !finitePositive(config.world.height)) {
        return fail(error, "world dimensions must be positive and finite");
    }
    if (period < 1) return fail(error, "light period must be positive");
    config.light.period = static_cast<core::Tick>(period);
    if (!finiteNonNegative(config.light.base) || !finiteNonNegative(config.light.amplitude) ||
        !std::isfinite(config.light.phase) || !finiteNonNegative(config.light.halfSaturation) ||
        !std::isfinite(config.light.verticalGradient)) {
        return fail(error, "light configuration is invalid");
    }
    if (!finiteNonNegative(config.producer.initialEnergy) ||
        !finiteNonNegative(config.producer.photo) ||
        !finiteNonNegative(config.producer.maint) ||
        !finiteNonNegative(config.producer.repro) ||
        !finiteNonNegative(config.producer.share) ||
        !finiteNonNegative(config.producer.max) ||
        !finiteNonNegative(config.producer.radius) ||
        !finiteNonNegative(config.producer.crowding) ||
        !finiteNonNegative(config.producer.interactionRadius) ||
        !finiteNonNegative(config.producer.competition) ||
        !finiteNonNegative(config.producer.densityRepro)) {
        return fail(error, "producer configuration is invalid");
    }
    if (!finiteNonNegative(config.agent.initialEnergy) || !finiteNonNegative(config.agent.metab) ||
        !finiteNonNegative(config.agent.maxSpeed) || !finiteNonNegative(config.agent.moveK) ||
        !finiteNonNegative(config.agent.harvest) || !finiteNonNegative(config.agent.harvestReserve) ||
        !finiteNonNegative(config.agent.harvestHalfSaturation) ||
        !finiteNonNegative(config.agent.densityRepro) ||
        !finiteNonNegative(config.agent.densityRadius) || !finiteNonNegative(config.agent.radius) ||
        !finiteNonNegative(config.agent.repro) || !finiteNonNegative(config.agent.cost) ||
        !finiteNonNegative(config.agent.child) || !finiteNonNegative(config.agent.max) ||
        !std::isfinite(config.agent.damping) || !finiteNonNegative(config.agent.avoidanceRadius)) {
        return fail(error, "agent configuration is invalid");
    }
    if (config.policy.rule.empty() || config.policy.rule.size() > snapshot::kMaxString) {
        return fail(error, "policy.rule is empty or too large");
    }
    if (!finiteNonNegative(config.policy.mutation) || !finitePositive(config.policy.senseRadius) ||
        !std::isfinite(config.policy.damping)) {
        return fail(error, "policy configuration is invalid");
    }
    return true;
}

void writeStats(Writer& writer, const world::EnergyStats& stats) {
    for (std::size_t i = 0; i < world::kEnergyReasonCount; ++i) {
        writer.f64(stats.reasons[i].in);
        writer.f64(stats.reasons[i].out);
    }
    writer.f64(stats.injected);
    writer.f64(stats.dissipated);
    writer.f64(stats.transferred);
}

bool readStats(Reader& reader, world::EnergyStats& stats, bool& sealed, std::string& error) {
    for (std::size_t i = 0; i < world::kEnergyReasonCount; ++i) {
        reader.f64(stats.reasons[i].in);
        reader.f64(stats.reasons[i].out);
    }
    reader.f64(stats.injected);
    reader.f64(stats.dissipated);
    reader.f64(stats.transferred);
    std::uint8_t sealedValue = 0;
    reader.u8(sealedValue);
    if (!reader.ok()) return fail(error, reader.error());
    if (sealedValue > 1) return fail(error, "ledger sealed flag is invalid");
    sealed = sealedValue != 0;
    if (!sealed) return fail(error, "snapshot ledger is not sealed");
    if (!finiteNonNegative(stats.injected) || !finiteNonNegative(stats.dissipated) ||
        !finiteNonNegative(stats.transferred)) {
        return fail(error, "ledger totals are invalid");
    }
    for (std::size_t i = 0; i < world::kEnergyReasonCount; ++i) {
        if (!finiteNonNegative(stats.reasons[i].in) || !finiteNonNegative(stats.reasons[i].out)) {
            return fail(error, "ledger reason totals are invalid");
        }
    }
    return true;
}

void writeRandom(Writer& writer, const core::RandomState& state) {
    writer.u64(state.state);
    writer.u64(state.increment);
    writer.f64(state.cachedNormal);
    writer.u8(state.hasCachedNormal ? 1U : 0U);
}

bool readRandom(Reader& reader, core::RandomState& state, std::string& error) {
    std::uint8_t cached = 0;
    reader.u64(state.state);
    reader.u64(state.increment);
    reader.f64(state.cachedNormal);
    reader.u8(cached);
    if (!reader.ok()) return fail(error, reader.error());
    if (cached > 1) return fail(error, "random cached-normal flag is invalid");
    if (!std::isfinite(state.cachedNormal)) return fail(error, "random cached normal is not finite");
    // PCG32 requires an odd increment. Reject it here so a checksum-valid but semantically
    // corrupt snapshot becomes a snapshot error (exit 5) instead of an uncaught throw.
    if (state.increment == 0 || (state.increment & 1ULL) == 0ULL) {
        return fail(error, "random increment must be odd");
    }
    state.hasCachedNormal = cached != 0;
    return true;
}

bool readSize(Reader& reader, std::uint64_t& value, const char* name, std::string& error) {
    if (!reader.u64(value)) return fail(error, reader.error());
    if (value > kMaxEntities) return fail(error, std::string(name) + " exceeds limit");
    return true;
}
} // namespace

bool SnapshotStore::save(const Simulation& simulation, const std::string& path, std::string& error) {
    // load() rejects a snapshot whose saved state fails the energy audit, so writing one would
    // only produce a file that can never be resumed. Fail here instead.
    if (!simulation.auditOk_) {
        return fail(error, "refusing to save a simulation that failed the energy audit");
    }
    Writer writer;
    writeConfig(writer, simulation.config_);
    writer.u64(simulation.tick_);

    writer.u64(simulation.producers_.nextId_);
    writer.u64(static_cast<std::uint64_t>(simulation.producers_.births_));
    writer.u64(static_cast<std::uint64_t>(simulation.producers_.deaths_));
    writer.u64(simulation.agents_.nextId_);
    writer.u64(static_cast<std::uint64_t>(simulation.agents_.births_));
    writer.u64(static_cast<std::uint64_t>(simulation.agents_.deaths_));

    writeRandom(writer, simulation.random_.state());
    writeStats(writer, simulation.ledger_.stats_);
    writer.u8(simulation.ledger_.initialSealed_ ? 1U : 0U);

    std::size_t producerCount = 0;
    for (const auto& producer : simulation.producers_.producers_) {
        if (producer && producer->alive()) ++producerCount;
    }
    writer.u64(static_cast<std::uint64_t>(producerCount));
    for (const auto& producer : simulation.producers_.producers_) {
        if (!producer || !producer->alive()) continue;
        writer.u64(producer->id);
        writer.u8(static_cast<std::uint8_t>(producer->kind()));
        writer.f64(producer->pos.x);
        writer.f64(producer->pos.y);
        writer.f64(producer->energy.energy);
        writer.f64(producer->energy.cap);
    }

    std::size_t agentCount = 0;
    for (const auto& agent : simulation.agents_.agents_) if (agent.alive()) ++agentCount;
    writer.u64(static_cast<std::uint64_t>(agentCount));
    for (const auto& agent : simulation.agents_.agents_) {
        if (!agent.alive()) continue;
        writer.u64(agent.id_);
        writer.f64(agent.pos_.x);
        writer.f64(agent.pos_.y);
        writer.f64(agent.vel_.x);
        writer.f64(agent.vel_.y);
        writer.f64(agent.energy_.energy);
        writer.f64(agent.energy_.cap);
        writer.u64(agent.age_);
        const decision::PolicySpec spec = agent.policy_->spec();
        writer.str(spec.id);
        writer.u32(spec.observationVersion);
        writer.u32(spec.actionVersion);
        writer.u32(spec.observationDim);
        writer.u32(spec.genomeDim);
        const std::vector<float> genome = agent.policy_->genome();
        writer.u32(static_cast<std::uint32_t>(genome.size()));
        for (float value : genome) writer.f32(value);
        std::vector<unsigned char> state;
        agent.policy_->serializeState(state);
        writer.u32(static_cast<std::uint32_t>(state.size()));
        writer.bytes(state.data(), state.size());
    }
    if (!writer.ok()) return fail(error, writer.error());
    // Environment layer: cyanobacteria nodes hold the only primary energy, so they must be
    // part of the snapshot or a reload would silently drop most of the world's energy.
    std::size_t cyanoCount = 0;
    for (const auto& node : simulation.cyanobacteria_.all()) if (node.alive()) ++cyanoCount;
    writer.u64(static_cast<std::uint64_t>(cyanoCount));
    for (const auto& node : simulation.cyanobacteria_.all()) {
        if (!node.alive()) continue;
        writer.u64(node.id);
        writer.f64(node.pos.x);
        writer.f64(node.pos.y);
        writer.f64(node.energy.energy);
        writer.f64(node.energy.cap);
        writer.f64(node.age);
    }

    if (!writer.ok()) return fail(error, writer.error());
    return snapshot::writeFile(path, writer.data(), error);
}

std::unique_ptr<Simulation> SnapshotStore::load(const std::string& path, bool overrideTicks,
                                                core::Tick maxTicks, std::string& error,
                                                snapshot::LoadStatus& status) {
    status = snapshot::LoadStatus::FormatError;
    std::vector<std::uint8_t> payload;
    const snapshot::LoadStatus read = snapshot::readFile(path, payload, error);
    if (read != snapshot::LoadStatus::Ok) {
        status = read;
        return nullptr;
    }
    Reader reader(payload);

    core::SimConfig config;
    if (!readConfig(reader, config, error)) return nullptr;

    std::uint64_t tick = 0;
    std::uint64_t producerNextId = 0;
    std::uint64_t producerBirths = 0;
    std::uint64_t producerDeaths = 0;
    std::uint64_t agentNextId = 0;
    std::uint64_t agentBirths = 0;
    std::uint64_t agentDeaths = 0;
    reader.u64(tick);
    reader.u64(producerNextId);
    reader.u64(producerBirths);
    reader.u64(producerDeaths);
    reader.u64(agentNextId);
    reader.u64(agentBirths);
    reader.u64(agentDeaths);
    if (!reader.ok()) { fail(error, reader.error()); return nullptr; }
    if (producerNextId == 0 || producerNextId > kMaxNextId ||
        agentNextId == 0 || agentNextId > kMaxNextId) {
        error = "snapshot next-id is invalid";
        return nullptr;
    }

    core::RandomState randomState;
    if (!readRandom(reader, randomState, error)) return nullptr;
    world::EnergyStats stats;
    bool sealed = false;
    if (!readStats(reader, stats, sealed, error)) return nullptr;

    std::uint64_t producerCount = 0;
    if (!readSize(reader, producerCount, "producer count", error)) return nullptr;
    if (producerCount > producerNextId) { error = "producer count exceeds next-id"; return nullptr; }
    // Reject impossible counts before allocating: each record needs at least 41 payload bytes.
    if (producerCount > reader.remaining() / 41ULL) {
        error = "producer count exceeds remaining payload";
        return nullptr;
    }
    std::vector<std::unique_ptr<producers::Producer>> producers;
    producers.reserve(static_cast<std::size_t>(producerCount));
    std::unordered_set<core::EntityId> producerIds;
    for (std::uint64_t i = 0; i < producerCount; ++i) {
        std::uint64_t id = 0;
        std::uint8_t kind = 0;
        double px = 0.0, py = 0.0, energy = 0.0, cap = 0.0;
        reader.u64(id);
        reader.u8(kind);
        reader.f64(px);
        reader.f64(py);
        reader.f64(energy);
        reader.f64(cap);
        if (!reader.ok()) { fail(error, reader.error()); return nullptr; }
        if (id == 0 || id >= producerNextId) { error = "producer id is invalid"; return nullptr; }
        if (!producerIds.insert(id).second) { error = "duplicate producer id"; return nullptr; }
        if (kind != static_cast<std::uint8_t>(producers::ProducerKind::Photo)) {
            error = "unsupported producer kind";
            return nullptr;
        }
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(energy) ||
            !std::isfinite(cap) || cap <= 0.0 || energy < 0.0 || energy > cap + 1e-9) {
            error = "producer state is invalid";
            return nullptr;
        }
        auto producer = std::make_unique<producers::PhotoProducer>(
            config.producer, core::EnergyState{energy, cap, true});
        producer->id = id;
        producer->pos = {px, py};
        producers.push_back(std::move(producer));
    }

    std::uint64_t agentCount = 0;
    if (!readSize(reader, agentCount, "agent count", error)) return nullptr;
    if (agentCount > agentNextId) { error = "agent count exceeds next-id"; return nullptr; }
    // Each agent record needs at least 92 payload bytes: refuse impossible counts before allocating.
    if (agentCount > reader.remaining() / 92ULL) {
        error = "agent count exceeds remaining payload";
        return nullptr;
    }
    std::vector<agentsim::Agent> agents;
    agents.reserve(static_cast<std::size_t>(agentCount));
    std::unordered_set<core::EntityId> agentIds;
    for (std::uint64_t i = 0; i < agentCount; ++i) {
        std::uint64_t id = 0;
        double px = 0.0, py = 0.0, vx = 0.0, vy = 0.0, energy = 0.0, cap = 0.0;
        std::uint64_t age = 0;
        reader.u64(id);
        reader.f64(px);
        reader.f64(py);
        reader.f64(vx);
        reader.f64(vy);
        reader.f64(energy);
        reader.f64(cap);
        reader.u64(age);
        std::string policyId;
        std::uint32_t observationVersion = 0, actionVersion = 0, observationDim = 0, genomeDim = 0;
        std::uint32_t genomeSize = 0, stateSize = 0;
        reader.str(policyId);
        reader.u32(observationVersion);
        reader.u32(actionVersion);
        reader.u32(observationDim);
        reader.u32(genomeDim);
        reader.u32(genomeSize);
        if (!reader.ok()) { fail(error, reader.error()); return nullptr; }
        if (id == 0 || id >= agentNextId) { error = "agent id is invalid"; return nullptr; }
        if (!agentIds.insert(id).second) { error = "duplicate agent id"; return nullptr; }
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(vx) ||
            !std::isfinite(vy) || !std::isfinite(energy) || !std::isfinite(cap) || cap <= 0.0 ||
            energy < 0.0 || energy > cap + 1e-9) {
            error = "agent state is invalid";
            return nullptr;
        }
        if (genomeSize > kMaxGenome || genomeSize != genomeDim) {
            error = "agent genome size is invalid";
            return nullptr;
        }
        std::vector<float> genome(genomeSize);
        for (std::uint32_t g = 0; g < genomeSize; ++g) {
            reader.f32(genome[g]);
            if (!reader.ok()) { fail(error, reader.error()); return nullptr; }
            if (!std::isfinite(genome[g])) { error = "agent genome value is not finite"; return nullptr; }
        }
        reader.u32(stateSize);
        if (!reader.ok()) { fail(error, reader.error()); return nullptr; }
        if (stateSize > kMaxPolicyState) { error = "agent policy state is too large"; return nullptr; }
        std::vector<unsigned char> state;
        if (!reader.bytes(state, stateSize)) { fail(error, reader.error()); return nullptr; }
        auto policy = decision::PolicyRegistry::create(policyId);
        if (!policy) { error = "unknown agent policy: " + policyId; return nullptr; }
        const decision::PolicySpec spec = policy->spec();
        if (spec.observationVersion != observationVersion || spec.actionVersion != actionVersion ||
            spec.observationDim != observationDim || spec.genomeDim != genomeDim) {
            error = "agent policy spec does not match snapshot";
            return nullptr;
        }
        policy->setGenome(genome.data(), genome.size());
        if (!policy->deserializeState(state.data(), state.size())) {
            error = "agent policy state rejected";
            return nullptr;
        }
        agents.emplace_back(id, core::Vec2{px, py}, core::Vec2{vx, vy},
                            core::EnergyState{energy, cap, true}, std::move(policy));
        agents.back().age_ = static_cast<core::Tick>(age);
    }
    // Environment layer nodes.
    std::uint64_t cyanoCount = 0;
    if (!readSize(reader, cyanoCount, "cyano count", error)) return nullptr;
    if (cyanoCount > reader.remaining() / 40ULL) {
        error = "cyano count exceeds remaining payload";
        return nullptr;
    }
    std::vector<environment::CyanoNode> cyanoNodes;
    cyanoNodes.reserve(static_cast<std::size_t>(cyanoCount));
    std::unordered_set<core::EntityId> cyanoIds;
    for (std::uint64_t i = 0; i < cyanoCount; ++i) {
        std::uint64_t id = 0;
        double px = 0.0, py = 0.0, energy = 0.0, cap = 0.0, age = 0.0;
        reader.u64(id);
        reader.f64(px);
        reader.f64(py);
        reader.f64(energy);
        reader.f64(cap);
        reader.f64(age);
        if (!reader.ok()) { fail(error, reader.error()); return nullptr; }
        if (id == 0) { error = "cyano node id is invalid"; return nullptr; }
        if (!cyanoIds.insert(id).second) { error = "duplicate cyano node id"; return nullptr; }
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(energy) ||
            !std::isfinite(cap) || cap <= 0.0 || energy < 0.0 || energy > cap + 1e-9 ||
            !std::isfinite(age) || age < 0.0) {
            error = "cyano node state is invalid";
            return nullptr;
        }
        environment::CyanoNode node;
        node.id = id;
        node.pos = {px, py};
        node.energy = core::EnergyState{energy, cap, true};
        node.age = age;
        cyanoNodes.push_back(node);
    }

    if (reader.remaining() != 0) { error = "snapshot has trailing payload bytes"; return nullptr; }
    if (producerNextId <= producerIds.size() || agentNextId <= agentIds.size()) {
        error = "snapshot next-id does not exceed entity ids";
        return nullptr;
    }

    std::unique_ptr<Simulation> simulation(new Simulation(Simulation::LoadTag{}, config));
    simulation->producers_.producers_ = std::move(producers);
    simulation->producers_.nextId_ = producerNextId;
    simulation->producers_.births_ = static_cast<std::size_t>(producerBirths);
    simulation->producers_.deaths_ = static_cast<std::size_t>(producerDeaths);
    simulation->agents_.agents_ = std::move(agents);
    simulation->agents_.nextId_ = agentNextId;
    simulation->agents_.births_ = static_cast<std::size_t>(agentBirths);
    simulation->agents_.deaths_ = static_cast<std::size_t>(agentDeaths);
    simulation->producers_.rebuildIndex();
    simulation->agents_.rebuildIndex();
    simulation->cyanobacteria_.nodes_ = std::move(cyanoNodes);
    {
        core::EntityId maxId = 0;
        for (const auto& node : simulation->cyanobacteria_.nodes_) {
            maxId = std::max(maxId, node.id);
        }
        simulation->cyanobacteria_.nextId_ = maxId + 1;
    }
    simulation->cyanobacteria_.rebuildIndex();
    simulation->ledger_.stats_ = stats;
    simulation->ledger_.initialSealed_ = true;
    simulation->random_.setState(randomState);
    simulation->tick_ = static_cast<core::Tick>(tick);
    simulation->auditOk_ = true;
    simulation->finishReason_.clear();
    if (overrideTicks) simulation->config_.world.maxTicks = maxTicks;
    simulation->finished_ = false;
    if (simulation->tick_ >= simulation->config_.world.maxTicks) {
        simulation->finished_ = true;
        simulation->finishReason_ = "达到最大 tick";
    } else if (simulation->agents_.aliveCount() == 0) {
        simulation->finished_ = true;
        simulation->finishReason_ = "所有 Agent 已死亡";
    }
    simulation->updateView();
    simulation->auditOk_ = simulation->ledger_.audit(
        simulation->totalEnergy(),
        1e-9 * std::max(1.0, simulation->ledger_.stats().injected));
    simulation->view_.stats.auditOk = simulation->auditOk_;
    if (!simulation->auditOk_) { error = "snapshot audit failed"; return nullptr; }
    status = snapshot::LoadStatus::Ok;
    return simulation;
}

} // namespace sim
#include "sim/SnapshotStore.h"

#include "sim/Snapshot.h"
#include "sim/Simulation.h"

#include "colony/Colony.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace sim {
namespace {
using snapshot::Reader;
using snapshot::Writer;

constexpr std::uint64_t kMaxEntities = 1000000ULL;
constexpr std::uint64_t kMaxNextId = 10000000ULL;

bool fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}
bool finiteNonNegative(double value) { return std::isfinite(value) && value >= 0.0; }
bool finitePositive(double value) { return std::isfinite(value) && value > 0.0; }

void writeConfig(Writer& writer, const core::SimConfig& c) {
    writer.f64(c.world.width);
    writer.f64(c.world.height);
    writer.u64(c.world.seed);
    writer.u64(c.world.maxTicks);
    writer.f64(c.light.base);
    writer.f64(c.light.amplitude);
    writer.u64(c.light.period);
    writer.f64(c.light.phase);
    writer.f64(c.light.halfSaturation);
    writer.f64(c.light.verticalGradient);
    writer.u64(static_cast<std::uint64_t>(c.cyano.initial));
    writer.u64(static_cast<std::uint64_t>(c.cyano.maxNodes));
    writer.f64(c.cyano.initialEnergy);
    writer.f64(c.cyano.cap);
    writer.f64(c.cyano.photoRate);
    writer.f64(c.cyano.lightHalfSaturation);
    writer.f64(c.cyano.metabolism);
    writer.f64(c.cyano.reproThreshold);
    writer.f64(c.cyano.reproProbability);
    writer.f64(c.cyano.reproOffset);
    writer.f64(c.cyano.minSeparation);
    writer.u64(static_cast<std::uint64_t>(c.colony.initialNuclei));
    writer.u64(static_cast<std::uint64_t>(c.colony.maxNuclei));
    writer.f64(c.colony.initialEnergy);
    writer.f64(c.colony.initialBallsPerNucleus);
    writer.f64(c.colony.deployCost);
    writer.f64(c.colony.recycleReturn);
    writer.f64(c.colony.detachRadius);
    writer.f64(c.colony.adoptRadius);
    writer.f64(c.colony.metabolism);
    writer.f64(c.colony.moveCostK);
    writer.f64(c.colony.preyRadius);
    writer.u64(static_cast<std::uint64_t>(c.colony.maxBalls));
    writer.str(c.policy.rule);
}

bool readConfig(Reader& reader, core::SimConfig& c, std::string& error) {
    std::uint64_t value = 0;
    reader.f64(c.world.width);
    reader.f64(c.world.height);
    reader.u64(c.world.seed);
    reader.u64(c.world.maxTicks);
    reader.f64(c.light.base);
    reader.f64(c.light.amplitude);
    reader.u64(value);
    if (value < 1) return fail(error, "light period must be positive");
    c.light.period = static_cast<core::Tick>(value);
    reader.f64(c.light.phase);
    reader.f64(c.light.halfSaturation);
    reader.f64(c.light.verticalGradient);
    reader.u64(value);
    if (value > kMaxEntities) return fail(error, "cyano.initial exceeds limit");
    c.cyano.initial = static_cast<std::size_t>(value);
    reader.u64(value);
    if (value == 0 || value > kMaxEntities) return fail(error, "cyano.maxNodes is invalid");
    c.cyano.maxNodes = static_cast<std::size_t>(value);
    reader.f64(c.cyano.initialEnergy);
    reader.f64(c.cyano.cap);
    reader.f64(c.cyano.photoRate);
    reader.f64(c.cyano.lightHalfSaturation);
    reader.f64(c.cyano.metabolism);
    reader.f64(c.cyano.reproThreshold);
    reader.f64(c.cyano.reproProbability);
    reader.f64(c.cyano.reproOffset);
    reader.f64(c.cyano.minSeparation);
    reader.u64(value);
    if (value > kMaxEntities) return fail(error, "colony.initialNuclei exceeds limit");
    c.colony.initialNuclei = static_cast<std::size_t>(value);
    reader.u64(value);
    if (value == 0 || value > kMaxEntities) return fail(error, "colony.maxNuclei is invalid");
    c.colony.maxNuclei = static_cast<std::size_t>(value);
    reader.f64(c.colony.initialEnergy);
    reader.f64(c.colony.initialBallsPerNucleus);
    reader.f64(c.colony.deployCost);
    reader.f64(c.colony.recycleReturn);
    reader.f64(c.colony.detachRadius);
    reader.f64(c.colony.adoptRadius);
    reader.f64(c.colony.metabolism);
    reader.f64(c.colony.moveCostK);
    reader.f64(c.colony.preyRadius);
    reader.u64(value);
    c.colony.maxBalls = static_cast<std::size_t>(std::min<std::uint64_t>(value, kMaxEntities));
    reader.str(c.policy.rule);
    if (!reader.ok()) return fail(error, reader.error());
    if (!finitePositive(c.world.width) || !finitePositive(c.world.height)) {
        return fail(error, "world dimensions must be positive");
    }
    if (!finitePositive(c.cyano.cap)) return fail(error, "cyano.cap must be positive");
    if (c.policy.rule.empty()) return fail(error, "policy.rule is empty");
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
    std::uint8_t flag = 0;
    reader.u8(flag);
    if (!reader.ok()) return fail(error, reader.error());
    if (flag > 1) return fail(error, "ledger sealed flag is invalid");
    sealed = flag != 0;
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

void writeParams(Writer& writer, const colony::NucleusParams& p) {
    writer.f64(p.orbitRadius);
    writer.f64(p.affinity);
    writer.f64(p.maxSpeed);
    writer.f64(p.maxAccel);
    writer.f64(p.energyThreshold);
    writer.f64(p.mutationRate);
    writer.f64(p.harvestRate);
    writer.f64(p.spotRadius);
}

bool readParams(Reader& reader, colony::NucleusParams& p, std::string& error) {
    reader.f64(p.orbitRadius);
    reader.f64(p.affinity);
    reader.f64(p.maxSpeed);
    reader.f64(p.maxAccel);
    reader.f64(p.energyThreshold);
    reader.f64(p.mutationRate);
    reader.f64(p.harvestRate);
    reader.f64(p.spotRadius);
    if (!reader.ok()) return fail(error, reader.error());
    if (!finitePositive(p.orbitRadius) || !finiteNonNegative(p.affinity) ||
        !finitePositive(p.maxSpeed) || !finiteNonNegative(p.maxAccel) ||
        !finitePositive(p.energyThreshold) || !finiteNonNegative(p.mutationRate) ||
        !finiteNonNegative(p.harvestRate) || !finitePositive(p.spotRadius)) {
        return fail(error, "nucleus parameters are invalid");
    }
    return true;
}
} // namespace

bool SnapshotStore::save(const Simulation& sim, const std::string& path, std::string& error) {
    if (!sim.auditOk_) {
        return fail(error, "refusing to save a simulation that failed the energy audit");
    }
    Writer writer;
    writeConfig(writer, sim.config_);
    writer.u64(sim.tick_);
    writer.u64(sim.colonies_.nextNucleusId_);
    writer.u64(sim.colonies_.nextBallId_);
    writer.u64(static_cast<std::uint64_t>(sim.cyanobacteria_.nextId()));
    writeRandom(writer, sim.random_.state());
    writeStats(writer, sim.ledger_.stats_);
    writer.u8(sim.ledger_.initialSealed_ ? 1U : 0U);

    std::size_t cyanoCount = 0;
    for (const auto& node : sim.cyanobacteria_.all()) if (node.alive()) ++cyanoCount;
    writer.u64(static_cast<std::uint64_t>(cyanoCount));
    for (const auto& node : sim.cyanobacteria_.all()) {
        if (!node.alive()) continue;
        writer.u64(node.id);
        writer.f64(node.pos.x);
        writer.f64(node.pos.y);
        writer.f64(node.energy.energy);
        writer.f64(node.energy.cap);
        writer.f64(node.age);
    }

    writer.u64(static_cast<std::uint64_t>(sim.colonies_.colonies().size()));
    for (const auto& colony : sim.colonies_.colonies()) {
        const auto& n = colony.nucleus;
        writer.u64(n.id);
        writer.f64(n.pos.x);
        writer.f64(n.pos.y);
        writer.f64(n.vel.x);
        writer.f64(n.vel.y);
        writer.f64(n.energy.energy);
        writer.f64(n.energy.cap);
        writer.u64(n.age);
        writer.f64(n.wanderAngle);
        writeParams(writer, n.params);
        writer.u64(static_cast<std::uint64_t>(colony.balls.size()));
        for (std::size_t index : colony.balls) writer.u64(static_cast<std::uint64_t>(index));
    }

    std::size_t ballCount = 0;
    for (const auto& ball : sim.colonies_.balls()) if (ball.alive()) ++ballCount;
    writer.u64(static_cast<std::uint64_t>(ballCount));
    for (const auto& ball : sim.colonies_.balls()) {
        if (!ball.alive()) continue;
        writer.u64(ball.id);
        writer.u64(ball.ownerId);
        writer.u8(static_cast<std::uint8_t>(ball.type));
        writer.f64(ball.pos.x);
        writer.f64(ball.pos.y);
        writer.f64(ball.vel.x);
        writer.f64(ball.vel.y);
        writer.f64(ball.energy.energy);
        writer.f64(ball.energy.cap);
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
    std::uint64_t nextNucleusId = 0;
    std::uint64_t nextBallId = 0;
    std::uint64_t nextCyanoId = 0;
    reader.u64(tick);
    reader.u64(nextNucleusId);
    reader.u64(nextBallId);
    reader.u64(nextCyanoId);
    if (!reader.ok()) return fail(error, reader.error()), nullptr;
    if (nextNucleusId == 0 || nextNucleusId > kMaxNextId || nextBallId == 0 ||
        nextBallId > kMaxNextId || nextCyanoId == 0 || nextCyanoId > kMaxNextId) {
        fail(error, "snapshot next-id is invalid");
        return nullptr;
    }

    core::RandomState randomState;
    if (!readRandom(reader, randomState, error)) return nullptr;
    world::EnergyStats stats;
    bool sealed = false;
    if (!readStats(reader, stats, sealed, error)) return nullptr;

    std::uint64_t cyanoCount = 0;
    if (!readSize(reader, cyanoCount, "cyano count", error)) return nullptr;
    if (cyanoCount > reader.remaining() / 40ULL) {
        fail(error, "cyano count exceeds remaining payload");
        return nullptr;
    }
    std::vector<environment::CyanoNode> nodes;
    nodes.reserve(static_cast<std::size_t>(cyanoCount));
    for (std::uint64_t i = 0; i < cyanoCount; ++i) {
        environment::CyanoNode node;
        double px = 0.0, py = 0.0, energy = 0.0, cap = 0.0;
        reader.u64(node.id);
        reader.f64(px);
        reader.f64(py);
        reader.f64(energy);
        reader.f64(cap);
        reader.f64(node.age);
        if (!reader.ok()) return fail(error, reader.error()), nullptr;
        if (node.id == 0 || !std::isfinite(px) || !std::isfinite(py) ||
            !std::isfinite(energy) || !std::isfinite(cap) || cap <= 0.0 || energy < 0.0 ||
            energy > cap + 1e-9) {
            fail(error, "cyano node state is invalid");
            return nullptr;
        }
        node.pos = {px, py};
        node.energy = core::EnergyState{energy, cap, true};
        nodes.push_back(node);
    }

    std::uint64_t colonyCount = 0;
    if (!readSize(reader, colonyCount, "colony count", error)) return nullptr;
    std::vector<colony::Colony> colonies;
    colonies.reserve(static_cast<std::size_t>(colonyCount));
    for (std::uint64_t i = 0; i < colonyCount; ++i) {
        colony::Colony colony;
        double px = 0.0, py = 0.0, vx = 0.0, vy = 0.0, energy = 0.0, cap = 0.0;
        std::uint64_t age = 0;
        reader.u64(colony.nucleus.id);
        reader.f64(px);
        reader.f64(py);
        reader.f64(vx);
        reader.f64(vy);
        reader.f64(energy);
        reader.f64(cap);
        reader.u64(age);
        reader.f64(colony.nucleus.wanderAngle);
        if (!readParams(reader, colony.nucleus.params, error)) return nullptr;
        std::uint64_t owned = 0;
        if (!readSize(reader, owned, "owned ball count", error)) return nullptr;
        if (owned > reader.remaining() / 8ULL) {
            fail(error, "owned ball count exceeds remaining payload");
            return nullptr;
        }
        colony.balls.reserve(static_cast<std::size_t>(owned));
        for (std::uint64_t b = 0; b < owned; ++b) {
            std::uint64_t index = 0;
            reader.u64(index);
            if (!reader.ok()) return fail(error, reader.error()), nullptr;
            colony.balls.push_back(static_cast<std::size_t>(index));
        }
        if (!reader.ok()) return fail(error, reader.error()), nullptr;
        if (colony.nucleus.id == 0 || !std::isfinite(px) || !std::isfinite(py) ||
            !std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(energy) ||
            !std::isfinite(cap) || cap <= 0.0 || energy < 0.0 || energy > cap + 1e-9) {
            fail(error, "nucleus state is invalid");
            return nullptr;
        }
        colony.nucleus.pos = {px, py};
        colony.nucleus.vel = {vx, vy};
        colony.nucleus.energy = core::EnergyState{energy, cap, true};
        colony.nucleus.age = static_cast<core::Tick>(age);
        colonies.push_back(std::move(colony));
    }

    std::uint64_t ballCount = 0;
    if (!readSize(reader, ballCount, "ball count", error)) return nullptr;
    if (ballCount > reader.remaining() / 65ULL) {
        fail(error, "ball count exceeds remaining payload");
        return nullptr;
    }
    std::vector<colony::Ball> balls;
    balls.reserve(static_cast<std::size_t>(ballCount));
    for (std::uint64_t i = 0; i < ballCount; ++i) {
        colony::Ball ball;
        std::uint8_t type = 0;
        double px = 0.0, py = 0.0, vx = 0.0, vy = 0.0, energy = 0.0, cap = 0.0;
        reader.u64(ball.id);
        reader.u64(ball.ownerId);
        reader.u8(type);
        reader.f64(px);
        reader.f64(py);
        reader.f64(vx);
        reader.f64(vy);
        reader.f64(energy);
        reader.f64(cap);
        if (!reader.ok()) return fail(error, reader.error()), nullptr;
        if (type > static_cast<std::uint8_t>(colony::BallType::Shield)) {
            fail(error, "ball type is invalid");
            return nullptr;
        }
        if (ball.id == 0 || !std::isfinite(px) || !std::isfinite(py) || !std::isfinite(vx) ||
            !std::isfinite(vy) || !std::isfinite(energy) || !std::isfinite(cap) || cap <= 0.0 ||
            energy < 0.0 || energy > cap + 1e-9) {
            fail(error, "ball state is invalid");
            return nullptr;
        }
        ball.type = static_cast<colony::BallType>(type);
        ball.pos = {px, py};
        ball.vel = {vx, vy};
        ball.energy = core::EnergyState{energy, cap, true};
        balls.push_back(ball);
    }

    if (reader.remaining() != 0) {
        fail(error, "snapshot has trailing payload bytes");
        return nullptr;
    }

    std::unique_ptr<Simulation> sim(new Simulation(Simulation::LoadTag{}, config));
    sim->cyanobacteria_.nodes_ = std::move(nodes);
    sim->cyanobacteria_.nextId_ = nextCyanoId;
    sim->cyanobacteria_.rebuildIndex();
    sim->colonies_.colonies_ = std::move(colonies);
    sim->colonies_.balls_ = std::move(balls);
    sim->colonies_.nextNucleusId_ = nextNucleusId;
    sim->colonies_.nextBallId_ = nextBallId;
    sim->colonies_.rebuildIndex();
    sim->ledger_.stats_ = stats;
    sim->ledger_.initialSealed_ = true;
    sim->random_.setState(randomState);
    sim->tick_ = static_cast<core::Tick>(tick);
    if (overrideTicks) sim->config_.world.maxTicks = maxTicks;
    sim->finished_ = false;
    sim->finishReason_.clear();
    sim->auditOk_ = true;
    sim->updateView();
    sim->auditOk_ = sim->ledger_.audit(
        sim->totalEnergy(), 1e-9 * std::max(1.0, sim->ledger_.stats().injected));
    sim->view_.stats.auditOk = sim->auditOk_;
    if (!sim->auditOk_) {
        fail(error, "snapshot audit failed");
        return nullptr;
    }
    if (sim->tick_ >= sim->config_.world.maxTicks) {
        sim->finished_ = true;
        sim->finishReason_ = "达到最大 tick";
    } else if (sim->colonies_.aliveNuclei() == 0) {
        sim->finished_ = true;
        sim->finishReason_ = "所有核已死亡";
    }
    sim->updateView();
    status = snapshot::LoadStatus::Ok;
    return sim;
}

} // namespace sim

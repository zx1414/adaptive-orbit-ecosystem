#include "producers/ProducerSystem.h"
#include "core/Math.h"
#include <algorithm>
#include <cmath>

namespace producers {
ProducerSystem::ProducerSystem(core::Bounds bounds, core::ProducerConfig config)
    : bounds_(bounds), config_(config), index_(bounds, 200.0) {}

void ProducerSystem::initialize(core::Random& random, world::EnergyLedger& ledger, world::Environment& environment) {
    producers_.clear();
    nextId_ = 1;
    births_ = 0;
    deaths_ = 0;
    const std::size_t count = config_.initial;
    for (std::size_t i = 0; i < count; ++i) {
        core::EnergyState state{0.0, std::max(1e-9, static_cast<double>(config_.cap)), true};
        auto producer = std::make_unique<PhotoProducer>(config_, state);
        producer->id = allocateId();
        producer->pos = {random.uniformReal(bounds_.minX, bounds_.maxX),
                         random.uniformReal(bounds_.minY, bounds_.maxY)};
        ledger.injectInitial(producer->energy, producer->id, std::max(0.0, config_.initialEnergy));
        producers_.push_back(std::move(producer));
    }
    (void)environment;
    rebuildIndex();
}

void ProducerSystem::tick(core::Random& random, world::EnergyLedger& ledger, world::Environment& environment, core::Tick tick) {
    std::vector<std::unique_ptr<Producer>> newborns;
    const std::size_t initial = producers_.size();
    const bool useNeighborhood = config_.interactionRadius > 0.0 &&
        (config_.crowding > 0.0 || config_.competition > 0.0 || config_.densityRepro > 0.0);
    for (std::size_t i = 0; i < initial; ++i) {
        if (!producers_[i] || !producers_[i]->alive()) continue;
        ProducerContext context;
        context.environment = &environment;
        context.ledger = &ledger;
        context.random = &random;
        context.tick = tick;
        context.population = initial;
        context.maxPopulation = core::Math::toSizeClamped(config_.max);
        context.newborns = &newborns;
        if (useNeighborhood) {
            const Neighborhood local = neighborhood(producers_[i]->pos, config_.interactionRadius,
                                                    producers_[i]->id);
            context.localDensity = local.count;
            context.localBiomass = local.biomass;
        }
        producers_[i]->tick(context);
    }
    for (auto& newborn : newborns) {
        if (!newborn || !newborn->alive()) continue;
        newborn->id = allocateId();
        producers_.push_back(std::move(newborn));
        ++births_;
    }
    removeDead();
    rebuildIndex();
}

void ProducerSystem::removeDead() {
    const std::size_t before = producers_.size();
    producers_.erase(std::remove_if(producers_.begin(), producers_.end(),
        [](const std::unique_ptr<Producer>& producer) { return !producer || !producer->alive(); }),
        producers_.end());
    deaths_ += before - producers_.size();
}
std::size_t ProducerSystem::aliveCount() const {
    return static_cast<std::size_t>(std::count_if(producers_.begin(), producers_.end(),
        [](const std::unique_ptr<Producer>& producer) { return producer && producer->alive(); }));
}
void ProducerSystem::rebuildIndex() {
    index_.clear();
    byId_.assign(static_cast<std::size_t>(nextId_), nullptr);
    for (const auto& producer : producers_) {
        if (producer && producer->alive()) {
            index_.insert(producer->id, producer->pos);
            if (producer->id < byId_.size()) byId_[producer->id] = producer.get();
        }
    }
}
Producer* ProducerSystem::find(core::EntityId id) {
    return id < byId_.size() ? byId_[static_cast<std::size_t>(id)] : nullptr;
}
const Producer* ProducerSystem::find(core::EntityId id) const {
    return id < byId_.size() ? byId_[static_cast<std::size_t>(id)] : nullptr;
}
void ProducerSystem::querySamples(core::Vec2 center, double radius,
                                  std::vector<world::ProducerSample>& out) const {
    out.clear();
    if (!core::Math::isFinite(center) || !std::isfinite(radius) || radius <= 0.0) return;
    static thread_local std::vector<core::EntityId> ids;
    index_.queryUnsorted(center, radius, ids);
    out.reserve(ids.size());
    for (const core::EntityId id : ids) {
        const Producer* producer = find(id);
        if (!producer || !producer->alive()) continue;
        out.push_back({producer->id, producer->pos, producer->energy.energy, producer->energy.cap});
    }
}
ProducerSystem::Neighborhood ProducerSystem::neighborhood(core::Vec2 center, double radius,
                                                          core::EntityId exclude) const {
    Neighborhood result;
    static thread_local std::vector<core::EntityId> ids;
    index_.query(center, radius, ids);
    for (const core::EntityId id : ids) {
        if (id == exclude) continue;
        const Producer* producer = find(id);
        if (!producer || !producer->alive()) continue;
        ++result.count;
        result.biomass += producer->energy.energy;
    }
    return result;
}
} // namespace producers

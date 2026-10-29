#include "producers/ProducerSystem.h"
#include "core/Math.h"
#include <algorithm>
#include <cmath>

namespace producers {
ProducerSystem::ProducerSystem(core::Bounds bounds, core::ProducerConfig config)
    : bounds_(bounds), config_(config), index_(bounds) {}

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
    std::vector<core::EntityId> neighbors;
    const std::size_t initial = producers_.size();
    const bool useShading = config_.crowding > 0.0 && config_.interactionRadius > 0.0;
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
        if (useShading) {
            index_.query(producers_[i]->pos, config_.interactionRadius, neighbors);
            context.localDensity = neighbors.empty() ? 0 : neighbors.size() - 1;
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
    for (const auto& producer : producers_) {
        if (producer && producer->alive()) index_.insert(producer->id, producer->pos);
    }
}
} // namespace producers

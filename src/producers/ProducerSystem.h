#pragma once
#include "core/Config.h"
#include "core/Random.h"
#include "producers/Producer.h"
#include "world/SpatialIndex.h"
#include "world/WorldQuery.h"
#include <memory>
#include <vector>

namespace sim { class SnapshotStore; }

namespace producers {
class ProducerSystem {
public:
    ProducerSystem(core::Bounds bounds, core::ProducerConfig config);
    void initialize(core::Random& random, world::EnergyLedger& ledger, world::Environment& environment);
    void tick(core::Random& random, world::EnergyLedger& ledger, world::Environment& environment, core::Tick tick);
    void removeDead();
    std::size_t aliveCount() const;
    std::size_t births() const { return births_; }
    std::size_t deaths() const { return deaths_; }
    core::EntityId nextId() const { return nextId_; }
    std::size_t size() const { return producers_.size(); }
    const std::vector<std::unique_ptr<Producer>>& all() const { return producers_; }
    std::vector<std::unique_ptr<Producer>>& all() { return producers_; }
    void rebuildIndex();
    const world::SpatialIndex& index() const { return index_; }
    Producer* find(core::EntityId id);
    const Producer* find(core::EntityId id) const;
    void querySamples(core::Vec2 center, double radius,
                      std::vector<world::ProducerSample>& out) const;
    struct Neighborhood {
        std::size_t count = 0;
        double biomass = 0.0;
    };
    Neighborhood neighborhood(core::Vec2 center, double radius, core::EntityId exclude) const;
    core::EntityId allocateId() { return nextId_++; }
private:
    friend class sim::SnapshotStore;
    core::Bounds bounds_;
    core::ProducerConfig config_;
    std::vector<std::unique_ptr<Producer>> producers_;
    world::SpatialIndex index_;
    std::vector<Producer*> byId_;
    core::EntityId nextId_ = 1;
    std::size_t births_ = 0;
    std::size_t deaths_ = 0;
};
} // namespace producers
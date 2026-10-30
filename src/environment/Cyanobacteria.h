#pragma once
#include "core/Config.h"
#include "core/Random.h"
#include "core/Types.h"
#include "world/Energy.h"
#include "world/Environment.h"
#include "world/SpatialIndex.h"

#include <vector>

namespace sim { class SnapshotStore; }

namespace environment {

using core::CyanoConfig;

// One cyanobacteria node: the environment layer's producer. Nodes photosynthesise from
// sunlight, pay maintenance, and reproduce into nearby space. They are NOT decision makers
// and own no policy; nuclei prey on them.
struct CyanoNode {
    core::Vec2 pos{};
    core::EnergyState energy{0.0, 10.0, true};
    core::EntityId id = core::kInvalidEntityId;
    double age = 0.0;
    bool alive() const { return energy.alive; }
};

struct CyanoSample {
    core::EntityId id = core::kInvalidEntityId;
    core::Vec2 position{};
    double energy = 0.0;
    double cap = 1.0;
};

// The cyanobacteria population. This is the only primary energy injector in the target
// model: every joule that enters the system does so through CyanoPhotosynthesis.
class Cyanobacteria {
public:
    Cyanobacteria(core::Bounds bounds, CyanoConfig config);

    void initialize(core::Random& random, world::EnergyLedger& ledger);
    // Photosynthesis -> metabolism -> death -> reproduction. Fixed id order.
    void tick(core::Random& random, world::EnergyLedger& ledger,
              const world::Environment& environment, core::Tick tick);
    void removeDead();

    std::size_t aliveCount() const;
    std::size_t size() const { return nodes_.size(); }
    const std::vector<CyanoNode>& all() const { return nodes_; }
    const CyanoConfig& config() const { return config_; }
    core::EntityId nextId() const { return nextId_; }
    std::size_t births() const { return births_; }
    std::size_t deaths() const { return deaths_; }

    // Zero-allocation radius query; out is cleared and filled with living nodes.
    void querySamples(core::Vec2 center, double radius, std::vector<CyanoSample>& out) const;
    const CyanoNode* find(core::EntityId id) const;
    // Marks the node dead and returns the energy a predator should receive. The node stays in
    // the vector until removeDead(), so ids held by callers stay valid for the rest of the tick.
    double consume(core::EntityId id, world::EnergyLedger& ledger);
    double totalEnergy() const;
    void rebuildIndex();

private:
    friend class sim::SnapshotStore;
    CyanoConfig config_;
    core::Bounds bounds_;
    std::vector<CyanoNode> nodes_;
    world::SpatialIndex index_;
    std::vector<CyanoNode*> byId_;
    core::EntityId nextId_ = 1;
    std::size_t births_ = 0;
    std::size_t deaths_ = 0;
};

} // namespace environment

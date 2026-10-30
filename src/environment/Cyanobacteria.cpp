#include "environment/Cyanobacteria.h"

#include "core/Math.h"

#include <algorithm>
#include <cmath>

namespace environment {
Cyanobacteria::Cyanobacteria(core::Bounds bounds, CyanoConfig config)
    : config_(config), bounds_(bounds.valid() ? bounds : core::Bounds{}), index_(bounds_, 200.0) {}

void Cyanobacteria::initialize(core::Random& random, world::EnergyLedger& ledger) {
    nodes_.clear();
    nextId_ = 1;
    births_ = 0;
    deaths_ = 0;
    const std::size_t count = config_.initial;
    nodes_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        CyanoNode node;
        node.id = nextId_++;
        node.pos = {random.uniformReal(bounds_.minX, bounds_.maxX),
                    random.uniformReal(bounds_.minY, bounds_.maxY)};
        node.energy = core::EnergyState{0.0, std::max(1e-9, config_.cap), true};
        ledger.injectInitial(node.energy, node.id, std::max(0.0, config_.initialEnergy));
        nodes_.push_back(node);
    }
    rebuildIndex();
}

void Cyanobacteria::tick(core::Random& random, world::EnergyLedger& ledger,
                         const world::Environment& environment, core::Tick tick) {
    const double cap = std::max(1e-9, config_.cap);
    const double halfSaturation = std::max(1e-9, config_.lightHalfSaturation);

    // Stage 1+2: photosynthesis and metabolism, in stable id order.
    for (CyanoNode& node : nodes_) {
        if (!node.alive()) continue;
        ++node.age;
        const double light = std::max(0.0, environment.lightAt(node.pos, tick));
        // Saturating response: a dim node gains proportionally, a bright one is capped.
        const double lightFactor = light / (light + halfSaturation);
        const double headroom = std::max(0.0, node.energy.cap - node.energy.energy);
        const double gain = std::min(headroom,
                                     std::max(0.0, config_.photoRate * lightFactor));
        if (gain > 0.0) ledger.injectPrimary(node.energy, node.id, gain);

        if (config_.metabolism > 0.0) {
            ledger.dissipate(node.energy, node.id, config_.metabolism,
                             world::EnergyReason::CyanoMetabolism);
        }
        if (node.energy.energy <= 1e-12) {
            ledger.kill(node.energy, node.id, world::EnergyReason::CyanoDeath);
        }
    }

    // Stage 3: reproduction. Parents stay alive; the child receives a zero-sum transfer.
    std::vector<CyanoNode> newborns;
    const std::size_t limit = config_.maxNodes;
    const double reproProbability = std::clamp(config_.reproProbability, 0.0, 1.0);
    for (CyanoNode& node : nodes_) {
        if (!node.alive()) continue;
        if (nodes_.size() + newborns.size() >= limit) break;
        if (node.energy.energy < config_.reproThreshold) continue;
        if (random.uniformReal(0.0, 1.0) >= reproProbability) continue;

        CyanoNode child;
        child.id = core::kInvalidEntityId;  // assigned when it is admitted
        child.energy = core::EnergyState{0.0, cap, true};
        const double angle = random.uniformReal(0.0, 6.28318530717958647692);
        const double distance = std::max(0.0, config_.reproOffset) *
                                (0.5 + 0.5 * random.uniformReal(0.0, 1.0));
        child.pos = {node.pos.x + std::cos(angle) * distance,
                     node.pos.y + std::sin(angle) * distance};
        child.pos.x = std::clamp(child.pos.x, bounds_.minX, bounds_.maxX);
        child.pos.y = std::clamp(child.pos.y, bounds_.minY, bounds_.maxY);

        // Zero-sum: the child only receives what the parent actually gives up.
        const double share = std::min(node.energy.energy * 0.5,
                                      std::max(0.0, config_.reproThreshold * 0.5));
        if (share <= 0.0) continue;
        const double moved = ledger.transfer(node.energy, child.energy, share,
                                             world::EnergyReason::CyanoReproduction);
        if (moved <= 0.0) continue;
        newborns.push_back(child);
    }
    for (CyanoNode& child : newborns) {
        child.id = nextId_++;
        nodes_.push_back(child);
        ++births_;
    }

    removeDead();
    rebuildIndex();
}

void Cyanobacteria::removeDead() {
    const std::size_t before = nodes_.size();
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                [](const CyanoNode& node) { return !node.alive(); }),
                 nodes_.end());
    deaths_ += before - nodes_.size();
}

std::size_t Cyanobacteria::aliveCount() const {
    return static_cast<std::size_t>(std::count_if(
        nodes_.begin(), nodes_.end(), [](const CyanoNode& node) { return node.alive(); }));
}

void Cyanobacteria::rebuildIndex() {
    index_.clear();
    byId_.assign(static_cast<std::size_t>(nextId_), nullptr);
    for (CyanoNode& node : nodes_) {
        if (!node.alive()) continue;
        index_.insert(node.id, node.pos);
        if (node.id < byId_.size()) byId_[static_cast<std::size_t>(node.id)] = &node;
    }
}

void Cyanobacteria::querySamples(core::Vec2 center, double radius,
                                 std::vector<CyanoSample>& out) const {
    out.clear();
    if (!core::Math::isFinite(center) || !std::isfinite(radius) || radius <= 0.0) return;
    static thread_local std::vector<core::EntityId> ids;
    index_.queryUnsorted(center, radius, ids);
    out.reserve(ids.size());
    const double radiusSq = radius * radius;
    for (const core::EntityId id : ids) {
        const CyanoNode* node = find(id);
        if (!node || !node->alive()) continue;
        if ((node->pos - center).lengthSq() > radiusSq) continue;
        out.push_back({node->id, node->pos, node->energy.energy, node->energy.cap});
    }
}

const CyanoNode* Cyanobacteria::find(core::EntityId id) const {
    return id < byId_.size() ? byId_[static_cast<std::size_t>(id)] : nullptr;
}

double Cyanobacteria::consume(core::EntityId id, world::EnergyLedger& ledger) {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [id](const CyanoNode& node) { return node.id == id; });
    if (it == nodes_.end() || !it->alive()) return 0.0;
    const double available = std::max(0.0, it->energy.energy);
    // kill() books the residual as dissipation; the predator receives the same amount, so the
    // pair of operations stays zero-sum and the audit still balances.
    ledger.kill(it->energy, it->id, world::EnergyReason::CyanoDeath);
    return available;
}

double Cyanobacteria::totalEnergy() const {
    double total = 0.0;
    for (const CyanoNode& node : nodes_) {
        if (node.alive()) total += node.energy.energy;
    }
    return total;
}

} // namespace environment

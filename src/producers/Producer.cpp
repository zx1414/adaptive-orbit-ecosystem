#include "producers/Producer.h"
#include "core/Math.h"
#include <algorithm>
#include <cmath>

namespace producers {
namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}
PhotoProducer::PhotoProducer(core::ProducerConfig config, core::EnergyState state)
    : config_(config) { energy = state; }

std::unique_ptr<Producer> PhotoProducer::clone() const {
    auto copy = std::make_unique<PhotoProducer>(config_, energy);
    copy->id = id;
    copy->pos = pos;
    return copy;
}

void PhotoProducer::tick(ProducerContext& context) {
    if (!energy.alive || !context.environment || !context.ledger || !context.random) return;
    const double light = context.environment->lightAt(pos, context.tick);
    const double capacity = std::max(1e-9, energy.cap);
    const double fraction = std::clamp(energy.energy / capacity, 0.0, 1.0);
    const double density = static_cast<double>(context.localDensity);
    const double biomass = std::max(0.0, context.localBiomass) / capacity;
    const double pressure = 1.0 + std::max(0.0, config_.crowding) * density +
                            std::max(0.0, config_.competition) * biomass;
    const double shading = std::isfinite(pressure) && pressure > 0.0 ? 1.0 / pressure : 0.0;
    const double gain = std::max(0.0, config_.photo * light * (1.0 - fraction) * shading);
    if (gain > 0.0) context.ledger->injectPrimary(energy, id, gain);
    context.ledger->dissipate(energy, id, std::max(0.0, config_.maint), world::EnergyReason::ProducerMaintenance);
    if (energy.energy <= 1e-12) {
        context.ledger->kill(energy, id, world::EnergyReason::ProducerDeath);
        return;
    }
    const std::size_t total = context.population + (context.newborns ? context.newborns->size() : 0);
    const double reproThreshold = config_.repro * (1.0 + std::max(0.0, config_.densityRepro) * density);
    if (std::isfinite(reproThreshold) && energy.energy >= reproThreshold &&
        total < context.maxPopulation && context.newborns) {
        auto child = clone();
        child->id = core::kInvalidEntityId;
        child->energy = {0.0, energy.cap, true};
        const double angle = context.random->uniformReal(0.0, kTwoPi);
        const double distance = std::max(1.0, config_.radius) * (0.5 + context.random->uniformReal(0.0, 1.0));
        child->pos = pos + core::Vec2(std::cos(angle), std::sin(angle)) * distance;
        const core::Bounds bounds = context.environment->bounds();
        child->pos.x = std::clamp(child->pos.x, bounds.minX, bounds.maxX);
        child->pos.y = std::clamp(child->pos.y, bounds.minY, bounds.maxY);
        const double share = std::min(config_.share, energy.energy);
        const double moved = context.ledger->transfer(energy, child->energy, share, world::EnergyReason::ProducerReproduction);
        if (moved > 0.0) context.newborns->push_back(std::move(child));
    }
}
} // namespace producers

#include "decision/RulePolicy.h"
#include "core/Math.h"
#include <algorithm>
#include <cmath>

namespace decision {
namespace {
float finiteOr(float value, float fallback) { return std::isfinite(value) ? value : fallback; }
} // namespace

RulePolicy::RulePolicy() {
    genes_ = {{0.22f, 0.70f, 0.55f, 0.30f, 0.45f, 0.50f}};
}
PolicySpec RulePolicy::spec() const {
    return {"rule", kObservationVersion, Observation::kDim, 2,
            static_cast<std::uint32_t>(GeneCount)};
}
void RulePolicy::setGene(std::size_t index, float value) {
    if (index < genes_.size()) genes_[index] = std::clamp(finiteOr(value, 0.0f), 0.0f, 1.0f);
}
std::unique_ptr<IPolicy> RulePolicy::clone() const {
    auto copy = std::make_unique<RulePolicy>();
    copy->genes_ = genes_;
    return copy;
}
std::vector<float> RulePolicy::genome() const { return {genes_.begin(), genes_.end()}; }
void RulePolicy::setGenome(const float* data, std::size_t size) {
    if (!data) return;
    for (std::size_t i = 0; i < genes_.size() && i < size; ++i) setGene(i, data[i]);
}
void RulePolicy::mutate(core::Random& random, float rate) {
    const float safeRate = std::clamp(finiteOr(rate, 0.0f), 0.0f, 1.0f);
    for (float& value : genes_) {
        const float delta = static_cast<float>(random.normal(0.0, safeRate));
        value = std::clamp(value + delta, 0.0f, 1.0f);
    }
}

Action RulePolicy::decide(const Observation& observation) {
    Action action;
    if (observation.version != kObservationVersion) return action;

    const float energyRatio = std::clamp(finiteOr(observation[obs::EnergyRatio], 0.0f), 0.0f, 1.0f);
    const float preyX = observation[obs::Prey0];
    const float preyY = observation[obs::Prey0 + 1];
    const float preyDistance = observation[obs::Prey0 + 2];
    const bool hasPrey = std::isfinite(preyDistance) && preyDistance >= 0.0f;
    const float rivalX = observation[obs::Rival0];
    const float rivalY = observation[obs::Rival0 + 1];
    const float rivalDistance = observation[obs::Rival0 + 2];
    const bool hasRival = std::isfinite(rivalDistance) && rivalDistance >= 0.0f;
    const float resourceBalls =
        std::clamp(finiteOr(observation[obs::ResourceBalls], 0.0f), 0.0f, 1.0f);

    // Eating a reachable prey always beats resting: otherwise a starving nucleus with food in
    // reach would conserve itself to death. The threshold is normalized by spotRadius, matching
    // the unit the observation reports, and the behaviour layer converts it back with the same
    // factor, so the two layers cannot disagree about "close enough".
    const float harvestThreshold = std::clamp(0.02f + genes_[HarvestBias] * 0.20f, 0.02f, 0.35f);
    if (hasPrey && preyDistance <= harvestThreshold) {
        action.kind = Action::Kind::Harvest;
        action.dirX = preyX;
        action.dirY = preyY;
        action.magnitude = 1.0f;
        return action;
    }

    // Hungry with nothing in reach: hunt rather than idle, since resting cannot gain energy.
    if (energyRatio < genes_[RestBelow] && !hasPrey) {
        action.kind = Action::Kind::Rest;
        action.magnitude = 0.0f;
        return action;
    }

    // Invest in storage when carrying surplus with nowhere to put it.
    if (energyRatio > 0.6f && resourceBalls < genes_[StoreBias] * 4.0f && !hasPrey) {
        action.kind = Action::Kind::DeployBall;
        action.magnitude = 1.0f;
        return action;
    }

    core::Vec2 direction{};
    if (hasPrey) {
        direction = core::Vec2(preyX, preyY);
    } else {
        const double angle = static_cast<double>(genes_[WanderBias]) * 6.28318530717958647692;
        direction = {std::cos(angle), std::sin(angle)};
    }

    core::Vec2 normalized = core::Math::unit(direction);
    if (hasRival && rivalDistance < 0.12f) {
        const core::Vec2 away = core::Math::unit(core::Vec2(-rivalX, -rivalY));
        normalized += away * static_cast<double>(genes_[RivalAvoid] * (1.0f - rivalDistance / 0.12f));
    }
    normalized = core::Math::unit(normalized);
    action.kind = Action::Kind::Move;
    action.dirX = static_cast<float>(normalized.x);
    action.dirY = static_cast<float>(normalized.y);
    action.magnitude = std::clamp(genes_[SpeedPreference], 0.0f, 1.0f);
    return action;
}

} // namespace decision

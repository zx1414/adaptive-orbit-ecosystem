#include "decision/RulePolicy.h"
#include "core/Math.h"
#include <algorithm>
#include <cmath>

namespace decision {
namespace {
constexpr float kEpsilon = 1e-6f;
constexpr double kPi = 3.14159265358979323846;
float finiteOr(float value, float fallback) { return std::isfinite(value) ? value : fallback; }
}

RulePolicy::RulePolicy() {
    genes_ = {{0.22f, 1.0f, 0.75f, 0.7f, 0.55f, 0.25f}};
}
PolicySpec RulePolicy::spec() const {
    return {"rule", kObservationVersion, Observation::kDim, 1, static_cast<std::uint32_t>(GeneCount)};
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
    const float energyRatio = std::clamp(finiteOr(observation[0], 0.0f), 0.0f, 1.0f);
    const float producerX = observation[obs::Producer0];
    const float producerY = observation[obs::Producer0 + 1];
    const float producerDistance = observation[obs::Producer0 + 2];
    const bool hasProducer = std::isfinite(producerDistance) && producerDistance >= 0.0f;
    const float agentX = observation[obs::Agent0];
    const float agentY = observation[obs::Agent0 + 1];
    const float agentDistance = observation[obs::Agent0 + 2];
    const bool hasAgent = std::isfinite(agentDistance) && agentDistance >= 0.0f;

    if (energyRatio < genes_[RestBelow]) {
        action.kind = Action::Kind::Rest;
        action.magnitude = 0.0f;
        return action;
    }

    core::Vec2 direction{};
    if (hasProducer) {
        direction = core::Vec2(producerX, producerY);
        const float harvestThreshold = 0.01f + genes_[HarvestBias] * 0.04f;
        if (producerDistance <= harvestThreshold) {
            action.kind = Action::Kind::Harvest;
            action.dirX = producerX;
            action.dirY = producerY;
            action.magnitude = 1.0f;
            return action;
        }
    } else {
        const double angle = std::atan2(observation[obs::DaySinRepeat], observation[obs::DayCos]) +
                             static_cast<double>(genes_[WanderBias] - 0.5f) * kPi;
        direction = {std::cos(angle), std::sin(angle)};
    }

    core::Vec2 normalized = core::Math::unit(direction);
    if (hasAgent && agentDistance < 0.18f) {
        const core::Vec2 away = core::Math::unit(core::Vec2(-agentX, -agentY));
        normalized += away * static_cast<double>(genes_[CrowdAvoidance] * (1.0f - agentDistance / 0.18f));
    }
    normalized = core::Math::unit(normalized);
    action.kind = Action::Kind::Move;
    action.dirX = static_cast<float>(normalized.x);
    action.dirY = static_cast<float>(normalized.y);
    action.magnitude = std::clamp(genes_[SpeedPreference] * (0.65f + 0.35f * genes_[TurnGain]), 0.0f, 1.0f);
    return action;
}

} // namespace decision

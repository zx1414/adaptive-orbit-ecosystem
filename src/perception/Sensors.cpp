#include "perception/Sensors.h"
#include "core/Math.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace agentsim {
namespace {
struct ScoredProducer { world::ProducerSample sample; double distanceSq; };
struct ScoredAgent { world::AgentSample sample; double distanceSq; };
float finiteClamp(float value, float lo, float hi) {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : 0.0f;
}
}
decision::Observation buildObservation(const Agent& agent, const world::WorldQuery& query) {
    decision::Observation observation;
    const core::SimConfig& config = query.config();
    const double senseRadius = std::max(1e-9, config.policy.senseRadius);
    const core::Vec2 position = agent.position();
    const core::Bounds bounds = query.bounds();
    const double maxSpeed = std::max(1e-9, config.agent.maxSpeed);
    const double maxEnergy = std::max(1e-9, agent.energy().cap);

    using namespace decision::obs;
    observation[EnergyRatio] = static_cast<float>(std::clamp(agent.energy().energy / maxEnergy, 0.0, 1.0));
    observation[SpeedRatio] = static_cast<float>(std::clamp(agent.velocity().length() / maxSpeed, 0.0, 1.0));
    observation[EdgeLowX] = static_cast<float>(std::clamp((position.x - bounds.minX) / senseRadius, 0.0, 1.0));
    observation[EdgeHighX] = static_cast<float>(std::clamp((bounds.maxX - position.x) / senseRadius, 0.0, 1.0));
    observation[EdgeLowY] = static_cast<float>(std::clamp((position.y - bounds.minY) / senseRadius, 0.0, 1.0));
    observation[EdgeHighY] = static_cast<float>(std::clamp((bounds.maxY - position.y) / senseRadius, 0.0, 1.0));
    observation[Light] = finiteClamp(static_cast<float>(query.lightAt(position) * 0.5), 0.0f, 1.0f);
    const double period = std::max(1.0, static_cast<double>(config.light.period));
    const double phase = static_cast<double>(query.tick()) / period * 6.28318530717958647692;
    observation[DaySin] = finiteClamp(static_cast<float>(std::sin(phase)), -1.0f, 1.0f);

    static thread_local std::vector<world::ProducerSample> producers;
    query.queryProducers(position, senseRadius, producers);
    static thread_local std::vector<ScoredProducer> scoredProducers;
    scoredProducers.clear();
    scoredProducers.reserve(producers.size());
    for (const auto& sample : producers) {
        if (sample.id == core::kInvalidEntityId || sample.energy <= 0.0) continue;
        scoredProducers.push_back({sample, (sample.position - position).lengthSq()});
    }
    // Only the two nearest slots are part of Observation v1; avoid sorting the
    // whole neighbourhood every tick.
    const std::size_t producerSlots = std::min<std::size_t>(2, scoredProducers.size());
    std::partial_sort(scoredProducers.begin(), scoredProducers.begin() + static_cast<std::ptrdiff_t>(producerSlots),
                      scoredProducers.end(), [](const auto& a, const auto& b) {
        if (a.distanceSq != b.distanceSq) return a.distanceSq < b.distanceSq;
        return a.sample.id < b.sample.id;
    });
    for (std::size_t i = 0; i < 2; ++i) {
        const int base = Producer0 + static_cast<int>(i) * SlotStride;
        observation[base + 2] = -1.0f; // empty slot
        if (i >= producerSlots) continue;
        const auto& item = scoredProducers[i];
        const core::Vec2 delta = item.sample.position - position;
        const double distance = std::sqrt(std::max(0.0, item.distanceSq));
        const double cap = std::max(1e-9, item.sample.cap);
        observation[base + 0] = finiteClamp(static_cast<float>(delta.x / senseRadius), -1.0f, 1.0f);
        observation[base + 1] = finiteClamp(static_cast<float>(delta.y / senseRadius), -1.0f, 1.0f);
        observation[base + 2] = finiteClamp(static_cast<float>(distance / senseRadius), 0.0f, 1.0f);
        observation[base + 3] = finiteClamp(static_cast<float>(item.sample.energy / cap), 0.0f, 1.0f);
    }

    static thread_local std::vector<world::AgentSample> agents;
    query.queryAgents(position, senseRadius, agents);
    static thread_local std::vector<ScoredAgent> scoredAgents;
    scoredAgents.clear();
    scoredAgents.reserve(agents.size());
    for (const auto& sample : agents) {
        if (sample.id == agent.id() || sample.energy <= 0.0) continue;
        scoredAgents.push_back({sample, (sample.position - position).lengthSq()});
    }
    const std::size_t agentSlots = std::min<std::size_t>(2, scoredAgents.size());
    std::partial_sort(scoredAgents.begin(), scoredAgents.begin() + static_cast<std::ptrdiff_t>(agentSlots),
                      scoredAgents.end(), [](const auto& a, const auto& b) {
        if (a.distanceSq != b.distanceSq) return a.distanceSq < b.distanceSq;
        return a.sample.id < b.sample.id;
    });
    for (std::size_t i = 0; i < 2; ++i) {
        const int base = Agent0 + static_cast<int>(i) * SlotStride;
        observation[base + 2] = -1.0f; // empty slot
        if (i >= agentSlots) continue;
        const auto& item = scoredAgents[i];
        const core::Vec2 delta = item.sample.position - position;
        const double distance = std::sqrt(std::max(0.0, item.distanceSq));
        observation[base + 0] = finiteClamp(static_cast<float>(delta.x / senseRadius), -1.0f, 1.0f);
        observation[base + 1] = finiteClamp(static_cast<float>(delta.y / senseRadius), -1.0f, 1.0f);
        observation[base + 2] = finiteClamp(static_cast<float>(distance / senseRadius), 0.0f, 1.0f);
        const double relative = (item.sample.energy - agent.energy().energy) /
                                std::max({1e-9, item.sample.cap, agent.energy().cap});
        observation[base + 3] = finiteClamp(static_cast<float>(relative), -1.0f, 1.0f);
    }
    observation[Density] = finiteClamp(static_cast<float>(scoredAgents.size()) / 4.0f, 0.0f, 1.0f);
    observation[DaySinRepeat] = finiteClamp(static_cast<float>(std::sin(phase)), -1.0f, 1.0f);
    observation[DayCos] = finiteClamp(static_cast<float>(std::cos(phase)), -1.0f, 1.0f);
    observation[Reserved] = 0.0f;
    for (float& value : observation.values) value = std::isfinite(value) ? value : 0.0f;
    return observation;
}
} // namespace agentsim

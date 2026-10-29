#pragma once
#include "core/Config.h"
#include "core/Types.h"
#include <vector>

namespace world {
struct ProducerSample {
    core::EntityId id = core::kInvalidEntityId;
    core::Vec2 position{};
    double energy = 0.0;
    double cap = 0.0;
};
struct AgentSample {
    core::EntityId id = core::kInvalidEntityId;
    core::Vec2 position{};
    double energy = 0.0;
    double cap = 0.0;
};

class WorldQuery {
public:
    virtual ~WorldQuery() = default;
    virtual const core::SimConfig& config() const = 0;
    virtual core::Bounds bounds() const = 0;
    virtual core::Tick tick() const = 0;
    virtual double lightAt(core::Vec2 position) const = 0;
    virtual void queryProducers(core::Vec2 center, double radius,
                                std::vector<ProducerSample>& out) const = 0;
    virtual void queryAgents(core::Vec2 center, double radius,
                             std::vector<AgentSample>& out) const = 0;
};
} // namespace world

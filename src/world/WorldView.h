#pragma once
#include "core/Config.h"
#include "core/Types.h"
#include <cstddef>
#include <string>
#include <vector>

namespace world {
struct ProducerView {
    core::EntityId id = core::kInvalidEntityId;
    core::Vec2 position{};
    double energy = 0.0;
    double cap = 1.0;
};
struct AgentView {
    core::EntityId id = core::kInvalidEntityId;
    core::Vec2 position{};
    double energy = 0.0;
    double cap = 1.0;
    std::string policyId;
};
struct WorldStats {
    core::Tick tick = 0;
    std::size_t producers = 0;
    std::size_t agents = 0;
    double totalEnergy = 0.0;
    double injected = 0.0;
    double dissipated = 0.0;
    double transferred = 0.0;
    bool auditOk = true;
    bool finished = false;
    std::string finishReason;
};
struct WorldView {
    core::Bounds bounds{};
    WorldStats stats{};
    std::vector<ProducerView> producers;
    std::vector<AgentView> agents;
};
} // namespace world

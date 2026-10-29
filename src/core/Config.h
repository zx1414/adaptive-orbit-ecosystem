#pragma once

#include "core/Types.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace core {

struct WorldConfig {
    double width = 2000.0;
    double height = 2000.0;
    std::uint64_t seed = 42;
    Tick maxTicks = 100000;
    Bounds bounds() const { return Bounds::fromSize(width, height); }
};

struct LightConfig {
    double base = 0.8;
    double amplitude = 0.4;
    Tick period = 2400;
    double phase = 0.0;
    double halfSaturation = 1.0;
    double verticalGradient = 0.2;
};

struct ProducerConfig {
    std::size_t initial = 30;
    std::size_t cap = 100;
    double initialEnergy = 30.0;
    double photo = 2.0;
    double maint = 0.005;
    double repro = 70.0;
    double share = 35.0;
    double max = 300.0;
    double radius = 10.0;
    // Local shading: photosynthesis is divided by (1 + crowding * neighbors).
    // 0 keeps the unconstrained M1 behavior.
    double crowding = 0.0;
    double interactionRadius = 80.0;
};

struct AgentConfig {
    std::size_t initial = 12;
    std::size_t cap = 100;
    double initialEnergy = 30.0;
    double metab = 0.02;
    double maxSpeed = 60.0;
    double moveK = 0.0002;
    double harvest = 0.5;
    double harvestReserve = 0.0; // prey energy left untouched by a harvest
    double radius = 30.0;
    double repro = 80.0;
    double cost = 10.0;
    double child = 30.0;
    double max = 200.0;
    core::Tick lifespan = 0; // 0 disables aging
    double damping = 0.98;
    double avoidanceRadius = 60.0;
};

struct PolicyConfig {
    std::string rule = "rule";
    double mutation = 0.05;
    double senseRadius = 800.0;
    std::size_t slots = 2;
    double damping = 0.98;
};

struct SimConfig {
    WorldConfig world{};
    LightConfig light{};
    ProducerConfig producer{};
    AgentConfig agent{};
    PolicyConfig policy{};
};

} // namespace core

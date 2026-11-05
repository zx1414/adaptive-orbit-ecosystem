#pragma once

#include "core/Types.h"

#include <cstddef>
#include <cstdint>
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



struct PolicyConfig {
    std::string rule = "rule";
    double mutation = 0.05;
    double senseRadius = 800.0;
    std::size_t slots = 2;
    double damping = 0.98;
};

// Cyanobacteria (environment producer) parameters. Declared in core so SimConfig does not
// depend on the environment layer; environment::Cyanobacteria consumes it.
struct CyanoConfig {
    std::size_t initial = 400;
    std::size_t maxNodes = 4000;
    double initialEnergy = 10.0;
    double cap = 40.0;
    double photoRate = 0.02;
    double lightHalfSaturation = 0.5;
    double metabolism = 0.005;
    double reproThreshold = 30.0;
    double reproProbability = 0.001;
    double reproOffset = 60.0;
    double minSeparation = 15.0;
};

// Colony (nucleus + organ balls) parameters. Declared in core so SimConfig stays free of a
// dependency on the colony layer.
struct ColonyConfig {
    std::size_t initialNuclei = 12;
    std::size_t maxNuclei = 200;
    double initialEnergy = 60.0;
    double initialBallsPerNucleus = 3.0;
    // Deploying a ball costs energy from the nucleus; recycling returns a fraction of the
    // ball's stored energy.
    double deployCost = 12.0;
    double recycleReturn = 0.35;
    // A ball further than this from its owner detaches but keeps its energy.
    double detachRadius = 600.0;
    // Free balls within this radius of a nucleus may be adopted.
    double adoptRadius = 120.0;
    double metabolism = 0.01;
    double moveCostK = 0.0002;
    double preyRadius = 30.0;
    // Global cap on balls so a runaway deploy loop cannot exhaust memory.
    std::size_t maxBalls = 4000;
};

struct SimConfig {
    WorldConfig world{};
    LightConfig light{};
    CyanoConfig cyano{};
    ColonyConfig colony{};
    PolicyConfig policy{};
};

} // namespace core
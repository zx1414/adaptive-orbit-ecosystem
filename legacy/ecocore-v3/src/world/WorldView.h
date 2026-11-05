#pragma once
#include "core/Config.h"
#include "core/Types.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace world {
// Environment layer node (cyanobacteria).
struct CyanoView {
    core::EntityId id = core::kInvalidEntityId;
    core::Vec2 position{};
    double energy = 0.0;
    double cap = 1.0;
};
// Consumer: a nucleus.
struct NucleusView {
    core::EntityId id = core::kInvalidEntityId;
    core::Vec2 position{};
    core::Vec2 velocity{};
    double energy = 0.0;
    double cap = 1.0;
    double orbitRadius = 0.0;
};
// An organ ball; ownerId ties it to its nucleus so the frontend can draw the orbit.
struct BallView {
    core::EntityId id = core::kInvalidEntityId;
    core::EntityId ownerId = core::kInvalidEntityId;
    std::uint8_t type = 0;
    core::Vec2 position{};
    double energy = 0.0;
    double cap = 1.0;
};
struct WorldStats {
    core::Tick tick = 0;
    std::size_t cyanobacteria = 0;
    std::size_t nuclei = 0;
    std::size_t balls = 0;
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
    std::vector<CyanoView> cyanobacteria;
    std::vector<NucleusView> nuclei;
    std::vector<BallView> balls;
};
} // namespace world

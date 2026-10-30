#pragma once
// Colony layer: nucleus (consumer / sole decision maker) and organ balls.
// Populated in M6 (docs/M5-PLAN.md); this placeholder exists so the architecture
// dependency guard can reference the layer before its first translation unit lands.

#include "core/Types.h"

namespace colony {

// Ball roles. The ball carries no individual physics parameters: every ball orbits with the
// owning nucleus's single orbitRadius, and type only selects behaviour at the nucleus.
enum class BallType : std::uint8_t { Resource = 0, Scout = 1, Shield = 2 };

} // namespace colony

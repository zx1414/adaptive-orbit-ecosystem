#pragma once
#include <cstdint>

namespace decision {
struct Action {
    enum class Kind : std::uint8_t { Rest = 0, Move = 1, Harvest = 2, DeployBall = 3 };
    Kind kind = Kind::Rest;
    float dirX = 0.0f;
    float dirY = 0.0f;
    float magnitude = 0.0f;
};
} // namespace decision

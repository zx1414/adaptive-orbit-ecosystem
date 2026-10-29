#pragma once
#include "core/Config.h"
#include "core/Types.h"

namespace world {
class Environment {
public:
    Environment(core::Bounds bounds, core::LightConfig light);
    const core::Bounds& bounds() const { return bounds_; }
    const core::LightConfig& config() const { return light_; }
    double lightAt(core::Vec2 position, core::Tick tick) const;
private:
    core::Bounds bounds_;
    core::LightConfig light_;
};
} // namespace world

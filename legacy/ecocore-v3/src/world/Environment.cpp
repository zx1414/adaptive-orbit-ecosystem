#include "world/Environment.h"
#include "core/Math.h"
#include <algorithm>
#include <cmath>

namespace world {
namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}
Environment::Environment(core::Bounds bounds, core::LightConfig light)
    : bounds_(bounds.valid() ? bounds : core::Bounds{}), light_(light) {}
double Environment::lightAt(core::Vec2 position, core::Tick tick) const {
    if (!core::Math::isFinite(position)) return 0.0;
    const double height = std::max(1e-9, bounds_.height());
    const double y = std::clamp((position.y - bounds_.minY) / height, 0.0, 1.0);
    const double period = std::max(1.0, static_cast<double>(light_.period));
    const double phase = (static_cast<double>(tick) / period + light_.phase) * kTwoPi;
    const double day = 0.5 + 0.5 * std::sin(phase);
    const double vertical = 1.0 + light_.verticalGradient * (0.5 - y);
    const double raw = (light_.base + light_.amplitude * day) * vertical;
    return std::isfinite(raw) ? std::max(0.0, raw) : 0.0;
}
} // namespace world

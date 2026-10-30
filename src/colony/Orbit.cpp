#include "colony/Orbit.h"

#include "core/Math.h"

#include <algorithm>
#include <cmath>

namespace colony {
namespace {
constexpr double kMinDistance = 1e-9;
}

core::Vec2 orbitAcceleration(const Ball& ball, const Nucleus& nucleus,
                             const OrbitParams& params) {
    const core::Vec2 delta = ball.pos - nucleus.pos;
    const double distance = delta.length();
    if (!(distance > kMinDistance) || !std::isfinite(distance)) return {};

    const core::Vec2 dir = delta / distance;
    const core::Vec2 tangent{-dir.y, dir.x};

    const double affinity = std::max(0.0, nucleus.params.affinity);
    const double orbitRadius = std::max(kMinDistance, nucleus.params.orbitRadius);

    // Radial spring: pulls the ball back toward orbitRadius from either side, so the radius
    // converges instead of drifting outward or collapsing.
    const double radial = -affinity * (distance - orbitRadius) * params.radialK;
    // Tangential drive: turns approach into circulation. Without it the ball would only
    // oscillate along the radial line.
    const double alongOrbit = affinity * params.tangentialK;

    core::Vec2 acceleration = dir * radial + tangent * alongOrbit;

    // Keep balls out of the nucleus body so they cannot sit at distance zero.
    if (params.nucleusRepelRadius > 0.0 && distance < params.nucleusRepelRadius) {
        const double strength =
            params.nucleusRepelK * (1.0 - distance / params.nucleusRepelRadius);
        acceleration += dir * strength;
    }
    return acceleration;
}

bool isDegenerate(const Ball& ball, const Nucleus& nucleus) {
    return !((ball.pos - nucleus.pos).lengthSq() > kMinDistance * kMinDistance);
}

core::Vec2 ballRepulsion(const Ball& self, const Ball& other, const OrbitParams& params) {
    if (params.ballRepelRadius <= 0.0) return {};
    const core::Vec2 delta = self.pos - other.pos;
    const double distanceSq = delta.lengthSq();
    const double radiusSq = params.ballRepelRadius * params.ballRepelRadius;
    if (!(distanceSq > kMinDistance * kMinDistance) || distanceSq >= radiusSq) return {};
    const double distance = std::sqrt(distanceSq);
    const double strength = params.ballRepelK * (1.0 - distance / params.ballRepelRadius);
    return (delta / distance) * strength;
}

void integrateBall(Ball& ball, core::Vec2 acceleration, const OrbitParams& params) {
    // Order matters: legacy-v2.3.1 adds the acceleration first and damps afterwards. Damping
    // before adding lets the tangential drive win every tick and the ball spirals outward
    // instead of settling on its orbit radius.
    const double damping = std::clamp(params.damping, 0.0, 1.0);
    ball.vel = (ball.vel + acceleration) * damping;
    if (params.maxBallSpeed > 0.0) {
        ball.vel = core::Math::limit(ball.vel, params.maxBallSpeed);
    }
    // Semi-implicit Euler: velocity first, then position with the new velocity. Forgetting the
    // position step leaves the ball frozen at its initial radius while velocity keeps growing.
    ball.pos += ball.vel;
}

double orbitDistance(const Ball& ball, const Nucleus& nucleus) {
    return (ball.pos - nucleus.pos).length();
}

} // namespace colony

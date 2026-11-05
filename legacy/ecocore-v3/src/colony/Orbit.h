#pragma once
#include "colony/Colony.h"
#include "core/Types.h"

namespace colony {

// Orbital force parameters: world constants, not per-ball state. Every ball of a nucleus uses
// the same rule; differences come from the nucleus genes.
struct OrbitParams {
    double radialK = 0.03;
    // The tangential drive adds angular momentum, so it pushes the equilibrium radius outward
    // from orbitRadius: at steady state v = a*d/(1-d) and the radius solves v^2/r = spring.
    // 2.0 (the v2.3.1 value) puts the equilibrium at 1.2x the target; 0.5 keeps it within 4%,
    // which is what makes the orbit read as circular instead of a spiral out.
    double tangentialK = 0.5;
    double damping = 0.90;
    double ballRepelRadius = 14.0;
    double ballRepelK = 1.2;
    double nucleusRepelRadius = 25.0;
    double nucleusRepelK = 3.0;
    double maxBallSpeed = 120.0;
};

core::Vec2 orbitAcceleration(const Ball& ball, const Nucleus& nucleus, const OrbitParams& params);
bool isDegenerate(const Ball& ball, const Nucleus& nucleus);
core::Vec2 ballRepulsion(const Ball& self, const Ball& other, const OrbitParams& params);
void integrateBall(Ball& ball, core::Vec2 acceleration, const OrbitParams& params);
double orbitDistance(const Ball& ball, const Nucleus& nucleus);

} // namespace colony

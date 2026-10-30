#include "TestHarness.h"

#include "colony/Colony.h"
#include "colony/Orbit.h"
#include "core/Random.h"

#include <cmath>
#include <vector>

namespace {
colony::Nucleus makeNucleus(double orbitRadius) {
    colony::Nucleus nucleus;
    nucleus.id = 1;
    nucleus.pos = {0.0, 0.0};
    nucleus.params.orbitRadius = orbitRadius;
    nucleus.params.affinity = 0.6;
    return nucleus;
}
colony::Ball makeBall(core::Vec2 pos) {
    colony::Ball ball;
    ball.id = 1;
    ball.ownerId = 1;
    ball.pos = pos;
    return ball;
}
} // namespace

TEST("orbit acceleration pulls an outer ball inward and an inner ball outward") {
    const colony::Nucleus nucleus = makeNucleus(100.0);
    colony::OrbitParams params;

    const colony::Ball outer = makeBall({200.0, 0.0});
    const core::Vec2 outerAcc = colony::orbitAcceleration(outer, nucleus, params);
    CHECK(outerAcc.x < 0.0);  // toward the nucleus

    const colony::Ball inner = makeBall({50.0, 0.0});
    const core::Vec2 innerAcc = colony::orbitAcceleration(inner, nucleus, params);
    CHECK(innerAcc.x > 0.0);  // away from the nucleus
}

TEST("orbit acceleration has a tangential component that produces circulation") {
    const colony::Nucleus nucleus = makeNucleus(100.0);
    colony::OrbitParams params;
    // Placed exactly on the target radius: the radial spring is zero, so any motion must
    // come from the tangential drive.
    const colony::Ball ball = makeBall({100.0, 0.0});
    const core::Vec2 acc = colony::orbitAcceleration(ball, nucleus, params);
    CHECK_NEAR(acc.x, 0.0, 1e-12);
    CHECK(std::abs(acc.y) > 0.0);
}

TEST("a ball settles into a stable orbit around its nucleus") {
    const colony::Nucleus nucleus = makeNucleus(100.0);
    colony::OrbitParams params;
    colony::Ball ball = makeBall({200.0, 0.0});

    // Integrate for a while; the radius must converge near orbitRadius, not drift away or
    // collapse into the nucleus.
    for (int i = 0; i < 4000; ++i) {
        if (colony::isDegenerate(ball, nucleus)) break;
        colony::integrateBall(ball, colony::orbitAcceleration(ball, nucleus, params), params);
    }
    const double distance = colony::orbitDistance(ball, nucleus);
    CHECK(distance > 0.0);
    // Within 20% of the target radius, per M5 acceptance.
    CHECK(distance > 100.0 * 0.8);
    CHECK(distance < 100.0 * 1.2);
}

TEST("orbit radius is tracked across the whole revolution") {
    const colony::Nucleus nucleus = makeNucleus(120.0);
    colony::OrbitParams params;
    colony::Ball ball = makeBall({120.0, 0.0});

    std::vector<double> radii;
    radii.reserve(3000);
    for (int i = 0; i < 3000; ++i) {
        colony::integrateBall(ball, colony::orbitAcceleration(ball, nucleus, params), params);
        radii.push_back(colony::orbitDistance(ball, nucleus));
    }
    double sum = 0.0;
    for (double r : radii) sum += r;
    const double mean = sum / static_cast<double>(radii.size());
    double variance = 0.0;
    for (double r : radii) variance += (r - mean) * (r - mean);
    variance /= static_cast<double>(radii.size());
    const double deviation = std::sqrt(variance);
    // The standard deviation of the radius must stay under 20% of the target radius.
    CHECK(deviation < 120.0 * 0.2);
    // A pure radial oscillator would keep the mean far off; circulation keeps it near target.
    CHECK(mean > 120.0 * 0.8);
    CHECK(mean < 120.0 * 1.2);
}

TEST("balls sharing one orbit radius repel instead of stacking") {
    colony::OrbitParams params;
    colony::Ball a = makeBall({100.0, 0.0});
    colony::Ball b = makeBall({100.0, 5.0});
    const core::Vec2 push = colony::ballRepulsion(a, b, params);
    CHECK(push.y < 0.0);  // a is pushed away from b
    // Outside the repulsion radius there is no force.
    colony::Ball far = makeBall({100.0, 100.0});
    const core::Vec2 none = colony::ballRepulsion(a, far, params);
    CHECK_NEAR(none.lengthSq(), 0.0, 1e-12);
}

TEST("nucleus genes change the resulting orbit") {
    colony::OrbitParams params;
    colony::Ball ball = makeBall({200.0, 0.0});

    colony::Nucleus tight = makeNucleus(60.0);
    colony::Nucleus wide = makeNucleus(200.0);
    const core::Vec2 toTight = colony::orbitAcceleration(ball, tight, params);
    const core::Vec2 toWide = colony::orbitAcceleration(ball, wide, params);
    // The same ball is pulled harder toward the tighter orbit.
    CHECK(toTight.x < toWide.x);
}

TEST("parameter mutation stays inside the declared ranges") {
    colony::ParamRanges ranges;
    core::Random random(9);
    colony::NucleusParams params;
    colony::randomizeParams(params, random, ranges);
    colony::clampParams(params, ranges);
    CHECK(params.orbitRadius >= ranges.orbitRadiusMin && params.orbitRadius <= ranges.orbitRadiusMax);
    CHECK(params.affinity >= ranges.affinityMin && params.affinity <= ranges.affinityMax);
    for (int i = 0; i < 200; ++i) {
        colony::mutateParams(params, random, 0.5, ranges);
        colony::clampParams(params, ranges);
    }
    CHECK(params.orbitRadius >= ranges.orbitRadiusMin && params.orbitRadius <= ranges.orbitRadiusMax);
    CHECK(params.maxSpeed >= ranges.maxSpeedMin && params.maxSpeed <= ranges.maxSpeedMax);
    CHECK(params.energyThreshold >= ranges.energyThresholdMin &&
          params.energyThreshold <= ranges.energyThresholdMax);
}

TEST("mutation is deterministic for a fixed seed") {
    colony::ParamRanges ranges;
    colony::NucleusParams a;
    colony::NucleusParams b;
    core::Random ra(21);
    core::Random rb(21);
    colony::randomizeParams(a, ra, ranges);
    colony::randomizeParams(b, rb, ranges);
    CHECK_NEAR(a.orbitRadius, b.orbitRadius, 0.0);
    colony::mutateParams(a, ra, 0.2, ranges);
    colony::mutateParams(b, rb, 0.2, ranges);
    CHECK_NEAR(a.orbitRadius, b.orbitRadius, 0.0);
    CHECK_NEAR(a.affinity, b.affinity, 0.0);
}

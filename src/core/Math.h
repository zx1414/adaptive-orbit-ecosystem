#pragma once

#include "core/Types.h"

#include <algorithm>
#include <cmath>

namespace core::Math {

inline bool isFinite(double value) { return std::isfinite(value); }
inline bool isFinite(const Vec2& value) { return isFinite(value.x) && isFinite(value.y); }
inline double finite(double value, double fallback = 0.0) { return isFinite(value) ? value : fallback; }
inline Vec2 finite(const Vec2& value, const Vec2& fallback = {}) {
    return {finite(value.x, fallback.x), finite(value.y, fallback.y)};
}

template <typename T>
constexpr const T& clamp(const T& value, const T& minimum, const T& maximum) {
    return value < minimum ? minimum : (maximum < value ? maximum : value);
}

inline double clamp01(double value) { return clamp(value, 0.0, 1.0); }
inline double safeDivide(double numerator, double denominator, double fallback = 0.0) {
    return isFinite(numerator) && isFinite(denominator) && std::abs(denominator) > 1e-12
        ? numerator / denominator : fallback;
}
inline Vec2 unit(const Vec2& value, const Vec2& fallback = {}, double epsilon = 1e-12) {
    if (!isFinite(value) || !isFinite(epsilon) || epsilon < 0.0) return fallback;
    const double magnitude = value.length();
    return isFinite(magnitude) && magnitude > epsilon ? value / magnitude : fallback;
}
inline Vec2 normalize(const Vec2& value, const Vec2& fallback = {}, double epsilon = 1e-12) {
    return unit(value, fallback, epsilon);
}
inline double distance(const Vec2& a, const Vec2& b) { return (b - a).length(); }
inline Vec2 limit(const Vec2& value, double maximumLength) {
    const double lengthSq = value.lengthSq();
    if (!(maximumLength > 0.0) || !isFinite(maximumLength) || !isFinite(lengthSq)) return {};
    if (lengthSq <= maximumLength * maximumLength) return value;
    return unit(value) * maximumLength;
}

// Reflect only when moving into the supplied unit surface normal.
inline Vec2 bounce(const Vec2& velocity, const Vec2& surfaceNormal, double restitution = 1.0) {
    const Vec2 normal = unit(surfaceNormal);
    const double inwardSpeed = velocity.dot(normal);
    if (inwardSpeed >= 0.0 || normal.lengthSq() == 0.0) return velocity;
    return velocity - normal * ((1.0 + clamp(restitution, 0.0, 1.0)) * inwardSpeed);
}

// Keep a circular entity inside bounds and reflect outward velocity at contacts.
inline void bounce(Vec2& position, Vec2& velocity, const Bounds& bounds,
                   double restitution = 1.0, double radius = 0.0) {
    if (!bounds.valid() || !isFinite(position) || !isFinite(velocity)) return;
    radius = isFinite(radius) ? std::max(0.0, radius) : 0.0;
    restitution = clamp(finite(restitution, 1.0), 0.0, 1.0);
    const double left = std::min(bounds.minX + radius, bounds.maxX);
    const double right = std::max(bounds.minX, bounds.maxX - radius);
    const double bottom = std::min(bounds.minY + radius, bounds.maxY);
    const double top = std::max(bounds.minY, bounds.maxY - radius);
    if (position.x < left) {
        position.x = left;
        if (velocity.x < 0.0) velocity.x = -velocity.x * restitution;
    } else if (position.x > right) {
        position.x = right;
        if (velocity.x > 0.0) velocity.x = -velocity.x * restitution;
    }
    if (position.y < bottom) {
        position.y = bottom;
        if (velocity.y < 0.0) velocity.y = -velocity.y * restitution;
    } else if (position.y > top) {
        position.y = top;
        if (velocity.y > 0.0) velocity.y = -velocity.y * restitution;
    }
}

} // namespace core::Math

namespace core {
inline Vec2 Vec2::normalized(double epsilon) const { return Math::unit(*this, {}, epsilon); }
}

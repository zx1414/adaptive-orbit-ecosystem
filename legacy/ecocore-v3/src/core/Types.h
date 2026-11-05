#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace core {

using EntityId = std::uint64_t;
using AgentId = EntityId;
using ProducerId = EntityId;
using Tick = std::uint64_t;
constexpr EntityId kInvalidEntityId = 0;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2() = default;
    constexpr Vec2(double xValue, double yValue) : x(xValue), y(yValue) {}

    constexpr Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    constexpr Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2 operator*(double scalar) const { return {x * scalar, y * scalar}; }
    constexpr Vec2 operator/(double scalar) const { return {x / scalar, y / scalar}; }
    Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }
    Vec2& operator-=(const Vec2& other) { x -= other.x; y -= other.y; return *this; }
    Vec2& operator*=(double scalar) { x *= scalar; y *= scalar; return *this; }
    Vec2& operator/=(double scalar) { x /= scalar; y /= scalar; return *this; }
    constexpr bool operator==(const Vec2& other) const { return x == other.x && y == other.y; }
    constexpr bool operator!=(const Vec2& other) const { return !(*this == other); }

    constexpr double lengthSq() const { return x * x + y * y; }
    double length() const { return std::sqrt(lengthSq()); }
    constexpr double dot(const Vec2& other) const { return x * other.x + y * other.y; }
    constexpr double cross(const Vec2& other) const { return x * other.y - y * other.x; }
    Vec2 normalized(double epsilon = 1e-12) const;
};

constexpr Vec2 operator*(double scalar, const Vec2& value) { return value * scalar; }
constexpr double dot(const Vec2& a, const Vec2& b) { return a.dot(b); }
constexpr double cross(const Vec2& a, const Vec2& b) { return a.cross(b); }

struct Bounds {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 2000.0;
    double maxY = 2000.0;

    constexpr Bounds() = default;
    constexpr Bounds(double minXValue, double minYValue, double maxXValue, double maxYValue)
        : minX(minXValue), minY(minYValue), maxX(maxXValue), maxY(maxYValue) {}
    static constexpr Bounds fromSize(double width, double height) { return {0.0, 0.0, width, height}; }
    constexpr double width() const { return maxX - minX; }
    constexpr double height() const { return maxY - minY; }
    constexpr Vec2 center() const { return {(minX + maxX) * 0.5, (minY + maxY) * 0.5}; }
    constexpr bool valid() const { return minX <= maxX && minY <= maxY; }
    constexpr bool contains(const Vec2& point) const {
        return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY;
    }
};

enum class EntityKind : std::uint8_t {
    Producer = 0,
    Agent = 1
};

struct EnergyState {
    double energy = 0.0;
    double cap = 1.0;
    bool alive = true;

    double fraction() const { return cap > 0.0 ? energy / cap : 0.0; }
};

} // namespace core

namespace agentsim {
namespace core = ::core;
}

#pragma once
#include <cmath>

// 二维浮点向量，供小球与核的位置/速度/加速度使用。
struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(double s) { x *= s; y *= s; return *this; }

    double lengthSq() const { return x * x + y * y; }
    double length() const { return std::sqrt(lengthSq()); }

    Vec2 normalized() const {
        double l = length();
        if (l < 1e-12) return {0.0, 0.0};
        return {x / l, y / l};
    }

    double dot(const Vec2& o) const { return x * o.x + y * o.y; }
};

inline Vec2 operator*(double s, const Vec2& v) { return v * s; }

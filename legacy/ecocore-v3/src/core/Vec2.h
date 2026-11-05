// agentsim - core/Vec2.h
// 二维向量与 Torus（环形拓扑）几何辅助。仅依赖 <cmath>。（S1：最小环距）
#pragma once
#include <cmath>

namespace agentsim {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

// 把坐标卷回 [0, w) x [0, h)。w/h <= 0 时对应分量原样返回。
inline Vec2 wrap(Vec2 v, double w, double h) {
    Vec2 r = v;
    if (w > 0.0) {
        r.x = std::fmod(v.x, w);
        if (r.x < 0.0) r.x += w;
        if (r.x >= w) r.x -= w; // 数值容错
    }
    if (h > 0.0) {
        r.y = std::fmod(v.y, h);
        if (r.y < 0.0) r.y += h;
        if (r.y >= h) r.y -= h;
    }
    return r;
}

// 最小环距位移向量：从 a 到 b 的最短缠绕位移，分量落在 [-w/2, w/2] / [-h/2, h/2] 内。
inline Vec2 torusDelta(Vec2 a, Vec2 b, double w, double h) {
    Vec2 d;
    d.x = b.x - a.x;
    d.y = b.y - a.y;
    if (w > 0.0) {
        d.x = std::fmod(d.x, w);
        if (d.x > w * 0.5) d.x -= w;
        else if (d.x < -w * 0.5) d.x += w;
    }
    if (h > 0.0) {
        d.y = std::fmod(d.y, h);
        if (d.y > h * 0.5) d.y -= h;
        else if (d.y < -h * 0.5) d.y += h;
    }
    return d;
}

// 【性能 2026-11-01】torusDelta 的快速版：前提是 **a 和 b 都已 wrap 在 [0,w) x [0,h)**。
// 此时 d = b - a ∈ (-w, w)，最短缠绕位移只需一次比较 + 一次加减，不需要 fmod：
//   d > w/2  -> d -= w
//   d < -w/2 -> d += w
// 与 torusDelta 在"输入已 wrap"前提下的结果**逐位相同**（fmod(x,w)==x 当 |x|<w）。
// 为什么需要它：对核物理是 O(n²)，300 核 = 9 万次/核·tick 的环距计算；
// 每次 torusDelta 走 2 次 fmod（库函数调用），实测 4000 tick 从 2s 涨到 23s。
// 调用方保证：实体的 pos 在每帧开头都经过 env.wrap()（Simulation::step 的既有约定）。
inline Vec2 torusDeltaWrapped(Vec2 a, Vec2 b, double w, double h) {
    Vec2 d;
    d.x = b.x - a.x;
    d.y = b.y - a.y;
    if (w > 0.0) {
        const double hw = w * 0.5;
        if (d.x > hw) d.x -= w;
        else if (d.x < -hw) d.x += w;
    }
    if (h > 0.0) {
        const double hh = h * 0.5;
        if (d.y > hh) d.y -= h;
        else if (d.y < -hh) d.y += h;
    }
    return d;
}

// 最小环距标量长度（跨边界取最短缠绕路径）。
inline double torusDistance(Vec2 a, Vec2 b, double w, double h) {
    Vec2 d = torusDelta(a, b, w, h);
    return std::sqrt(d.x * d.x + d.y * d.y);
}

inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

inline double length(Vec2 v) { return std::sqrt(dot(v, v)); }

// 归一化；零向量安全（返回 {0,0}）。
inline Vec2 normalized(Vec2 v) {
    const double len = length(v);
    if (len <= 0.0) return Vec2{0.0, 0.0};
    return Vec2{v.x / len, v.y / len};
}

} // namespace agentsim

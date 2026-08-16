#pragma once
#include "Vec2.h"
#include "NucleusParams.h"

// 核：决策者与进化单元，携带可遗传参数。
struct Nucleus {
    Vec2 pos;
    Vec2 vel;
    double energy = 0.0;
    bool alive = true;
    int id = -1;   // 稳定的谱系 ID（创建时分配，不随数组重排变化）
    int age = 0;   // 存活帧数（每帧 +1，用于新生保护期等）
    double wanderAngle = 0.0;  // 随机游走方向（持久、缓慢转向，避免原地震颤）
    NucleusParams params;

    Nucleus() = default;
    Nucleus(const Vec2& p, double e, const NucleusParams& par)
        : pos(p), energy(e), params(par) {}
};

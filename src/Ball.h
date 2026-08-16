#pragma once
#include "Vec2.h"
#include "BallType.h"

// 小球：环境中的基础单位，不具备决策能力。
struct Ball {
    Vec2 pos;
    Vec2 vel;
    BallType type = BallType::Worker;
    int ownerId = -1;  // -1 表示自由小球；否则为所属核在 nuclei 数组中的索引
};

// 类型对应的渲染字符。
char ballChar(BallType type);

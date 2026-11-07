#pragma once

// 小球类型枚举。增加新类型时只需在此扩展枚举，并同步 BALL_TYPE_COUNT。
enum class BallType : int {
    Shield = 0,  // 护盾型
    Worker = 1,  // 资源型
    Scout  = 2   // 侦察型
};

// 小球类型总数：核参数中每个类型感知数组的长度。
constexpr int BALL_TYPE_COUNT = 3;

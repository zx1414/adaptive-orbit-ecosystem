#pragma once
#include "World.h"

// 控制台可视化：把世界映射到固定大小的字符网格并输出。
class Renderer {
public:
    Renderer(int cols = 80, int rows = 40);
    void render(const World& world);
    void setClear(bool clear) { clear_ = clear; }

private:
    int cols_, rows_;
    bool clear_ = true;
};

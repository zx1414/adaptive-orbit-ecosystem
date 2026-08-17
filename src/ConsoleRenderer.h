#pragma once
#include "IRenderer.h"
#include "World.h"

// 控制台可视化：把世界映射到固定大小的字符网格并输出（IRenderer 的默认实现）。
class ConsoleRenderer : public IRenderer {
public:
    ConsoleRenderer(int cols = 80, int rows = 40);
    void render(const World& world) override;
    void setClear(bool clear) { clear_ = clear; }

private:
    int cols_, rows_;
    bool clear_ = true;
};

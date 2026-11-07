#pragma once

class World;

// 渲染器抽象：把世界状态呈现出来（控制台字符 / Web 等）。
// 约束：World 不得 include 任何 Renderer；渲染器只通过 World 的 const 访问器读数据。
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void render(const World& world) = 0;
};

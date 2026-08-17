#pragma once
#include <cstdint>
#include <mutex>
#include <vector>

#include "IRenderer.h"
#include "World.h"

// Web 可视化渲染器：不直接绘制，而是把世界状态打包成二进制快照，
// 由 HttpServer 经 GET /state 提供给浏览器前端（逐球 6 字节 + 核点 + 统计）。
//
// 快照格式（小端）：
//   头部 64 字节：
//     0  u8   magic 'W' (0x57)
//     1  u8   版本 = 1
//     2  u8   状态（0=运行中，1=已结束）
//     3  u8   保留
//     4  i32  帧号
//     8  f32  世界宽
//     12 f32  世界高
//     16 i32  球数
//     20 i32  存活核数
//     24 i32  护盾球数 / 28 i32 资源球数 / 32 i32 侦察球数
//     36 char 结束原因[28]（零填充 UTF-8）
//   球记录 ×N（6 字节）：x u16, y u16, meta u16（低 2 位类型，(ownerId+1)<<2，0=自由）
//   核记录 ×M（28 字节）：x f32, y f32, energy f32, attackStrength f32,
//                         maxSpeed f32, energyThreshold f32, id i32
class WebRenderer : public IRenderer {
public:
    // 构造后立即构建初始快照（模拟开始前前端也能拿到画面）。
    explicit WebRenderer(const World& world);

    // 每 render_interval 帧调用：重建快照。
    void render(const World& world) override;

    // 线程安全：返回最新快照的完整字节。
    std::vector<uint8_t> snapshotBytes() const;

private:
    void buildSnapshot(const World& world);

    mutable std::mutex mutex_;
    std::vector<uint8_t> bytes_;
};

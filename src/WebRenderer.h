#pragma once
#include <cstdint>
#include <mutex>
#include <vector>

#include "IRenderer.h"
#include "World.h"

// Web 可视化渲染器：不直接绘制，而是把世界状态打包成二进制快照，
// 由 HttpServer 经 GET /state 提供给浏览器前端（逐球 6 字节 + 核点 + 蓝细菌 + 统计）。
//
// 快照格式 v2（小端；逐字节冻结契约见 _agent/prompts/CONTRACT.md 第 2 节）：
//   头部 80 字节：
//     0  u8   magic 'W' (0x57)
//     1  u8   版本 = 2
//     2  u8   状态（0=运行中，1=已结束）
//     3  u8   保留 0
//     4  i32  帧号
//     8  f32  世界宽
//     12 f32  世界高
//     16 i32  球数
//     20 i32  存活核数
//     24 i32  护盾球数 / 28 i32 资源球数 / 32 i32 侦察球数
//     36 char 结束原因[28]（零填充 UTF-8，占 36..63）
//     64 i32  蓝细菌存活节点数
//     68 i32  蓝细菌存活连接数
//     72 f32  蓝细菌平均节点能量（无节点 = 0）
//     76 f32  本帧全局阳光系数
//   球记录 ×球数（6 字节，v1 不变）：
//      0 u16 x（0..65535 线性映射世界宽）
//      2 u16 y
//      4 u16 meta = (ownerBits << 2) | type；ownerBits = 0 表示自由球，
//                   否则 ownerId+1（上限 16383）；低 2 位 = 类型
//   核记录 ×存活核数（32 字节）：
//      0 f32 x / 4 f32 y / 8 f32 energy / 12 f32 attackStrength / 16 f32 maxSpeed
//     20 f32 energyThreshold / 24 u32 id / 28 f32 hue（0..360 度，遗传色相）
//   蓝细菌节点 ×蓝细菌节点数（24 字节，4 字节对齐）：
//      0 f32 x / 4 f32 y / 8 f32 energy
//     12 u32 id（稳定谱系 ID，供 tooltip / CSV 交叉引用）
//     16 u16 linkCount / 18 u16 colonyId（所属聚落）/ 20 u16 age
//     22 u16 flags ← bit0 = 存活，bit1..2 = 类型（0 = Normal）
//   蓝细菌连接 ×蓝细菌连接数（8 字节）：
//      0 u32 a / 4 u32 b ← 快照内 cyanoNodes 数组下标（**不是**稳定 id），保证 a < b
//   记录顺序固定：header → balls → nuclei → cyanoNodes → cyanoLinks。
//   偏移：nuclOff = 80 + 球数*6；cyanoNodeOff = nuclOff + 核数*32；
//         cyanoLinkOff = cyanoNodeOff + 节点数*24。
//   总长 = 80 + 球数*6 + 核数*32 + 节点数*24 + 连接数*8。
//   关闭蓝细菌（无节点/连接）时计数与平均能量写 0，但头部仍是 80 字节的 v2
//   （前端统一按 v2 解析，不做 v1/v2 双解析）。
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

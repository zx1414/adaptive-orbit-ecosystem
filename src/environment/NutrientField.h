// agentsim - environment/NutrientField.h
// P1 物质循环的环境侧：残骸（debris）/ 可吸收养分（nutrient）的固定网格场。
//
// 职责边界（契约 docs/NEXT-ECOLOGY-CONTRACT.md 第 4 节）：
//   - 只记录"物质"，不涉及能量：本模块不认识 AgentLedger，不产生/扣减任何能量；
//     残骸与养分都不能替代光照凭空给生产者增加可用能量。
//   - 纯确定性、**零 RNG**：不 include core/PCG32.h，不抽随机数，不依赖遍历顺序
//     之外的隐式状态。同输入序列 ⇒ 同状态（逐位），这是存档可复现的前提。
//   - 四层单向依赖：environment 是最底层之一，本模块只 include <cmath>/<cstddef>
//     /<vector> 与 core/Vec2.h，不碰 sim/ decision/ behavior/ ledger/ 等上层。
//
// 分区定义（唯一口径，测试与可视化都以此为准）：
//   cols = max(1, floor(worldW / regionSize))，rows = max(1, floor(worldH / regionSize))；
//   cellW = worldW / cols，cellH = worldH / rows（均分整个环面，故末列/末行
//   不会被截断；每个区域的实际边长因此 >= regionSize）。
//   下标：q = wrap(p)，ix = clamp(floor(q.x / cellW), 0, cols-1)，iy 同式
//   （行主存储：index = iy * cols + ix）。
//   用 cellW 而不是 regionSize 做步长是刻意的：分区边界必须与 cellCenter
//   共用同一个常量，否则"中心点落回自己区域"这条自洽性会在浮点末端失守。
//   clamp 兜住 q.x/cellW 在 q.x 贴近 worldW 时可能算出的 cols（浮点舍入）。
//   区域中心 center = {(ix + 0.5) * cellW, (iy + 0.5) * cellH}。
//   环面语义：坐标先 wrap（wrap() 与 core/Vec2.h 同语义），故 x = worldW-ε 与
//   x = 0 落在**相邻**的两个区域（末格与首格），而不是同一格、也不是越界索引。
//
// 分解数学（decompose）：
//   每帧对**每个区域**：converted = debris * rate；debris -= converted；
//   nutrient += converted。rate 由调用方（组合根）给定并先 clamp 到 [0,1]，
//   本模块不自己做时间管理、不知道 dt。转换量恒有
//   0 <= converted <= debris（对 rate ∈ [0,1] 恒成立，含浮点舍入）⇒ 库存绝不
//   为负、不产生 NaN。区域内零和由"减残骸与加养分用同一个 converted 量"保证，
//   只剩每区域 1 ulp 级的加法舍入差；单测按 1e-12 相对容差断言
//   |Δdebris| == |Δnutrient| 且 |Δ(debris+nutrient)| ≈ 0。返回值 = Σconverted。
#pragma once
#include <cstddef>
#include <vector>

#include "../core/Vec2.h"

namespace agentsim {

// 单个区域的两种物质量。沿用仓库的聚合 struct 风格（同 CyanoNode / TerrainBlob），
// 无自定义构造函数，便于存档按字段写读。
struct NutrientCell {
    double debris = 0.0;    // 尚在分解的残骸
    double nutrient = 0.0;  // 可吸收养分
};

class NutrientField {
public:
    // 空场（0 区域、全 0 库存）。组合根若用默认构造，必须在可用前 configure()。
    NutrientField() = default;
    // 建场：按 worldW/worldH 与 regionSize 划分环面网格，每个区域的
    // **nutrient** 初值 = initialPerRegion，debris 一律从 0 起。理由：开局地表
    // 养分是"可吸收"库存；残骸应全部由死亡等真实来源产生，否则开局就有一条
    // 无来源的分解流，把物质守恒的口径弄脏。
    // 非法输入（worldW/worldH <= 0 或 regionSize <= 0）退化为 1x1 网格（单区域），
    // 保证 cellCount() >= 1 且任何坐标都有归属；initialPerRegion < 0 视为 0。
    NutrientField(double worldW, double worldH, double regionSize,
                  double initialPerRegion);

    // ---- 网格几何（测试/可视化/存档用）----
    void configure(double worldW, double worldH, double regionSize,
                   double initialPerRegion);
    double worldWidth() const { return worldW_; }
    double worldHeight() const { return worldH_; }
    double regionSize() const { return regionSize_; }
    int cols() const { return cols_; }
    int rows() const { return rows_; }
    std::size_t cellCount() const { return cells_.size(); }

    // 坐标 -> 分区下标（环面 wrap + clamp）。返回的行主下标恒在 [0, cellCount())。
    std::size_t indexAt(Vec2 p) const;
    std::size_t indexOf(int ix, int iy) const;
    // 下标 -> 网格坐标（反解，供存档校验/可视化）。
    int ixOf(std::size_t index) const;
    int iyOf(std::size_t index) const;
    // 区域中心坐标（环面内，恒在 [0, worldW) x [0, worldH)）。
    Vec2 cellCenter(std::size_t index) const;
    // 坐标所在区域的中心（= cellCenter(indexAt(p))）。
    Vec2 centerAt(Vec2 p) const;

    // 坐标所在区域（只读）。恒有效（越界/非法坐标也落到某个合法区域）。
    const NutrientCell& cellAt(Vec2 p) const;
    const NutrientCell& cell(std::size_t index) const { return cells_[index]; }

    // ---- 物质操作（全部是"物质"，不涉及能量）----
    // 从该区域取养分：返回实际取得 = clamp(want, 0, 存量)。取超量只给存量，
    // want <= 0 或 NaN 时不动并返回 0；库存恒 >= 0。
    double takeNutrient(Vec2 p, double want);
    // 向该区域加养分（amount <= 0 或 NaN 时无操作；不做上限钳制）。
    void addNutrient(Vec2 p, double amount);
    // 向该区域加残骸（同上）。
    void addDebris(Vec2 p, double amount);

    // 每帧分解：每区域 debris -> nutrient，rate 由调用方给定（先 clamp 到 [0,1]）。
    // 返回本帧转化总量 Σ(debris * rate)。rate <= 0 或 NaN 时无操作、返回 0。
    double decompose(double ratePerTick);

    // ---- 总量（观测/审计/守恒断言用；全部 O(n)，不与其它模块共享状态）----
    double totalDebris() const;
    double totalNutrient() const;
    double total() const { return totalDebris() + totalNutrient(); }

    // ---- 存档 / 测试用的白盒通道（纯追加，语义同 CyanoField::restoreForSaveLoad）----
    // 逐区域原样回填（下标 = 确定性序）；size 必须等于 cellCount()，否则忽略
    // （返回 false）以免半途改写破坏库存。几何（world/regionSize/cols/rows）由
    // configure() 先对齐，本方法不动几何。
    bool restoreCells(const std::vector<NutrientCell>& cells);
    const std::vector<NutrientCell>& cells() const { return cells_; }

private:
    double worldW_ = 0.0;
    double worldH_ = 0.0;
    double regionSize_ = 1.0;
    double cellW_ = 1.0;
    double cellH_ = 1.0;
    int cols_ = 1;
    int rows_ = 1;
    std::vector<NutrientCell> cells_{};
};

} // namespace agentsim

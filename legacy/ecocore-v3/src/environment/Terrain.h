// agentsim - environment/Terrain.h
// 环境层地形：只读查询对象（障碍 / 水域 / 温度区），不认识智能体。
// 障碍阻挡移动（resolveMove 推出）；水域提高移动耗能；温度区给局部温度偏移。
// 所有距离判定用 Torus 最小环距（跨边界正确）。
#pragma once
#include <cstddef>
#include <vector>

#include "../core/Config.h"
#include "../core/PCG32.h"
#include "../core/Vec2.h"

namespace agentsim {

class Terrain {
public:
    Terrain() = default;
    Terrain(const TerrainConfig& cfg, double worldW, double worldH, PCG32& rng);
    explicit Terrain(const SimConfig& config, PCG32& rng);
    explicit Terrain(const TerrainConfig& cfg, PCG32& rng);

    void setWorldSize(double w, double h);
    // （重新）生成：显式 blob 非空的组直接采用（不消耗 rng）；其余组按 count
    // 用 rng 均匀撒点（避开边界 margin=半径，同组两两退避不重叠，64 次上限）。
    void generate(const TerrainConfig& cfg, PCG32& rng);
    void generate(const TerrainConfig& cfg, double worldW, double worldH, PCG32& rng);

    bool blocked(Vec2 pos) const;               // 是否被障碍占据
    bool inWater(Vec2 pos) const;               // 是否在水域内
    double moveCostMultiplier(Vec2 pos) const;  // 移动耗能倍率：1 正常，水域 >1
    double temperatureOffset(Vec2 pos) const;   // 温度偏移（叠加到温度场采样值）
    // 想移动到 to 的实体滑到最近合法位置：to 被阻挡时沿 from→to 二分回退 8 次；
    // 全程被挡（含 from 本身被挡）则返回 from。
    Vec2 resolveMove(Vec2 from, Vec2 to) const;

    std::size_t obstacleCount() const { return obstacles_.size(); }
    std::size_t waterCount() const { return waters_.size(); }
    std::size_t thermalZoneCount() const { return thermalZones_.size(); }

    const std::vector<TerrainBlob>& obstacles() const { return obstacles_; }
    const std::vector<TerrainBlob>& waters() const { return waters_; }
    const std::vector<TerrainBlob>& thermalZones() const { return thermalZones_; }
    double worldWidth() const { return worldW_; }
    double worldHeight() const { return worldH_; }

private:
    // 圆形 blob 判定：Torus 最小环距 <= 半径（半径 <=0 视为不存在）。
    bool inside(const TerrainBlob& b, double fallbackR, Vec2 pos) const;

    TerrainConfig cfg_{};
    std::vector<TerrainBlob> obstacles_;
    std::vector<TerrainBlob> waters_;
    std::vector<TerrainBlob> thermalZones_;
    double worldW_ = 0.0;
    double worldH_ = 0.0;
};

} // namespace agentsim

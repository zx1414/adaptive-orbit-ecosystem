// agentsim - environment/Terrain.cpp
// 地形实现：圆形 blob + Torus 环距判定；确定性仅依赖传入 rng 状态。
#include "Terrain.h"

#include <cmath>

namespace agentsim {

Terrain::Terrain(const TerrainConfig& cfg, double worldW, double worldH, PCG32& rng) {
    generate(cfg, worldW, worldH, rng);
}

Terrain::Terrain(const SimConfig& config, PCG32& rng)
    : Terrain(config.terrain, config.world.width, config.world.height, rng) {}

Terrain::Terrain(const TerrainConfig& cfg, PCG32& rng) {
    generate(cfg, rng);
}

void Terrain::setWorldSize(double w, double h) {
    worldW_ = w;
    worldH_ = h;
}

void Terrain::generate(const TerrainConfig& cfg, PCG32& rng) {
    generate(cfg, worldW_, worldH_, rng);
}

void Terrain::generate(const TerrainConfig& cfg, double worldW, double worldH,
                       PCG32& rng) {
    cfg_ = cfg;
    worldW_ = worldW;
    worldH_ = worldH;
    obstacles_ = cfg.obstacles;   // 显式 blob 组直接采用（不消耗 rng）。
    waters_ = cfg.waters;
    thermalZones_ = cfg.thermalZones;
    if (worldW <= 0.0 || worldH <= 0.0) return; // 无有效世界：只接受显式 blob
    const double marginX = worldW * 0.02;
    const double marginY = worldH * 0.02;
    // 撒点辅助：count 个圆心，半径取组默认；避开边界 margin 并与已放的同类
    // blob 保持 2*r 间距（朴素退避 64 次，超限就放弃该点，保证终止与确定性）。
    auto scatter = [this, worldW, worldH, marginX, marginY, &rng](
                       std::size_t count, double r,
                       std::vector<TerrainBlob>& out) {
        for (std::size_t n = 0; n < count; ++n) {
            Vec2 best{};
            bool ok = false;
            for (int attempt = 0; attempt < 64 && !ok; ++attempt) {
                Vec2 c{marginX + rng.nextDouble() * (worldW - 2.0 * marginX),
                       marginY + rng.nextDouble() * (worldH - 2.0 * marginY)};
                ok = true;
                for (const TerrainBlob& b : out) {
                    if (torusDistance(c, Vec2{b.x, b.y}, worldW, worldH) <
                        2.0 * r) {
                        ok = false;
                        break;
                    }
                }
                if (ok) best = c;
            }
            if (ok) {
                TerrainBlob b;
                b.x = best.x;
                b.y = best.y;
                b.radius = r;
                out.push_back(b);
            }
        }
    };
    if (cfg.obstacles.empty()) {
        obstacles_.clear();
        scatter(cfg.obstacleCount, cfg.obstacleRadius, obstacles_);
    }
    if (cfg.waters.empty()) {
        waters_.clear();
        scatter(cfg.waterCount, cfg.waterRadius, waters_);
    }
    if (cfg.thermalZones.empty()) {
        thermalZones_.clear();
        scatter(cfg.thermalZoneCount, cfg.thermalRadius, thermalZones_);
        for (TerrainBlob& b : thermalZones_) b.strength = cfg.thermalStrength;
    }
}

bool Terrain::inside(const TerrainBlob& b, double fallbackR, Vec2 pos) const {
    const double r = b.radius > 0.0 ? b.radius : fallbackR;
    if (r <= 0.0) return false;
    return torusDistance(Vec2{b.x, b.y}, pos, worldW_, worldH_) <= r;
}

bool Terrain::blocked(Vec2 pos) const {
    for (const TerrainBlob& b : obstacles_)
        if (inside(b, cfg_.obstacleRadius, pos)) return true;
    return false;
}

bool Terrain::inWater(Vec2 pos) const {
    for (const TerrainBlob& b : waters_)
        if (inside(b, cfg_.waterRadius, pos)) return true;
    return false;
}

double Terrain::moveCostMultiplier(Vec2 pos) const {
    return inWater(pos) ? cfg_.waterCostMultiplier : 1.0;
}

double Terrain::temperatureOffset(Vec2 pos) const {
    // 线性衰减圆盘：中心 = strength，边界 = 0，跨边界连续（Torus 环距）。
    double sum = 0.0;
    for (const TerrainBlob& b : thermalZones_) {
        const double r = b.radius > 0.0 ? b.radius : cfg_.thermalRadius;
        if (r <= 0.0) continue;
        const double d = torusDistance(Vec2{b.x, b.y}, pos, worldW_, worldH_);
        if (d < r) sum += b.strength * (1.0 - d / r);
    }
    return sum;
}

Vec2 Terrain::resolveMove(Vec2 from, Vec2 to) const {
    const Vec2 target = wrap(to, worldW_, worldH_);
    // 出发点被挡（实体已陷入）：保持历史语义——目标合法则一步逃出，否则不动。
    if (blocked(from)) return blocked(target) ? from : target;
    // R6-P1-2 防隧穿：端点都合法不代表线段合法——一步 46~60 可整体跨过 r=24 的
    // 障碍（boost 满速时穿过圆心）。先采样线段 1/4、1/2、3/4 处（Torus 最短路
    // 径插值 from + t*torusDelta(from,to) + wrap），任一落入障碍则记 hi0=该采
    // 样点并走二分回退，把落点挡在障碍前而非穿到对面。无命中（hi0==1.0）时
    // 与旧版逐位一致：目标合法直接返回 target。
    const Vec2 d = torusDelta(from, target, worldW_, worldH_);
    double hi0 = 1.0;                       // 首个被挡采样点的 t。
    if (!(d.x == 0.0 && d.y == 0.0)) {
        for (int k = 1; k <= 3; ++k) {
            const double ts = 0.25 * static_cast<double>(k);
            const Vec2 mid = wrap(Vec2{from.x + ts * d.x, from.y + ts * d.y},
                                  worldW_, worldH_);
            if (blocked(mid)) { hi0 = ts; break; }
        }
    }
    if (!blocked(target) && hi0 >= 1.0) return target;  // 目标本就合法且线段无阻挡
    if (d.x == 0.0 && d.y == 0.0) return from;
    // 沿 from→target 二分回退 8 次：lo 合法、hi 被挡，收敛到最近的合法点。
    // hi 初值取 hi0：隧穿采样命中时 < 1.0，落点挡在障碍前而不是障碍对面。
    double lo = 0.0;
    double hi = hi0;
    for (int i = 0; i < 8; ++i) {
        const double mid = 0.5 * (lo + hi);
        const Vec2 cand{wrap(Vec2{from.x + mid * d.x, from.y + mid * d.y},
                             worldW_, worldH_)};
        if (blocked(cand)) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    if (lo <= 0.0) return from;             // 全程被挡：原地不动
    return wrap(Vec2{from.x + lo * d.x, from.y + lo * d.y}, worldW_, worldH_);
}

} // namespace agentsim

// 蓝细菌模块实现。
//
// 设计要点（与 _agent/prompts/CONTRACT.md §3 冻结接口一致）：
//  * 本模块**不 include World.h**，也不持有 World&：产出的自由小球通过 step() 的
//    outBalls 输出，由 World 自己追加进它的 balls_，避免循环依赖，且可脱离 World 独立自测。
//  * links_ 里存的是 nodes_ 的**数组下标**。节点被吃掉（consumeNode）只标 alive=0，
//    真正的移除发生在**下一次 step 开头**，并同步紧凑化 nodes_ + 重映射 links_ 下标，
//    保证 World.cpp 在同一帧内拿到的下标始终有效，且 links_ 永不悬空。
//  * 随机源只有调用方传入的 std::mt19937& rng：类内不存 RNG，可复现。

#include "Cyanobacteria.h"

#include <algorithm>
#include <cmath>

namespace {

// 圆周率（本模块按契约只依赖少数标准头，不引入 <numbers>）。
constexpr double kTwoPi = 6.283185307179586476925286766559;

// 极小正数兜底：避免除零、对数定义域问题。
constexpr double kTiny = 1e-9;

// 转化率下限的绝对兜底，保证返回值恒 > 0（即使配置里 minRate<=0）。
constexpr double kMinPositiveRate = 1e-12;

// 网格维度上限（防御异常配置导致巨量桶分配）。
constexpr int kMaxGridDim = 4096;

// 成链/断链滞回：断链距离 = 成链距离 × 该倍数，避免间距在阈值附近反复连断。
constexpr double kBreakDistanceFactor = 1.5;

// 节点连接数缓存上限（uint16_t 上限）。
constexpr uint16_t kMaxLinkCountCache = static_cast<uint16_t>(0xFFFFu);

// 世界坐标 → 网格单元下标（越界时钳制到网格内）。
int gridCoord(double v, double cellSize, int maxCells) {
    if (cellSize <= 0.0 || maxCells <= 0) return 0;
    int c = static_cast<int>(std::floor(v / cellSize));
    if (c < 0) c = 0;
    else if (c > maxCells - 1) c = maxCells - 1;
    return c;
}

// 光合响应：饱和函数 sun/(sun+半饱和)，避免阳光越大增益无限增长。
double lightUptake(double sunLevel, double halfSaturation) {
    if (!(sunLevel > 0.0)) return 0.0;
    const double hs = (halfSaturation > 0.0) ? halfSaturation : kTiny;
    return sunLevel / (sunLevel + hs);
}

// 位置 + 时间的阳光场（昼夜系数 day 由调用方预先算好，避免逐节点重复三角函数）。
double sunField(double x, double y, int frame, const SunParams& s, double worldH, double day) {
    const double h = (worldH > 0.0) ? worldH : 1.0;
    // 垂直梯度：世界顶部阳光强、底部弱（y 向下）。
    double ny = y / h;
    if (ny < 0.0) ny = 0.0;
    else if (ny > 1.0) ny = 1.0;
    double vertical = 1.0 - s.verticalGradient * ny;
    if (vertical < 0.0) vertical = 0.0;

    // 局部扰动：空间频率 noiseFrequency + 时间漂移 noiseSpeed，两个正交正弦叠加避免单一斜条纹。
    const double t = s.noiseSpeed * static_cast<double>(frame);
    const double noise = std::sin(s.noiseFrequency * x + t) * std::cos(s.noiseFrequency * y - 0.7 * t);

    double level = day * vertical * (1.0 + s.noiseAmplitude * noise);
    if (!(level > 0.0)) level = 0.0;  // 兼顾 NaN / 负值
    return level;
}

}  // namespace

// ---------------- 纯函数：核捕食转化率 ----------------
// 设计意图：形成「机动性↑ ⇒ 消化效率↓」的取舍（越能跑/越能加速的核，吃蓝细菌的效率越低）。
// 四个模式：
//   0 常数        ：与机动性无关（只随遗传消化系数缩放），作为对照组。
//   1 速度×加速度反比：两项都参与惩罚，单调递减（默认模式，取舍最强）。
//   2 幂律        ：rate × (ref/实际)^(exponent×w) 形式，单调递减，曲线可调。
//   3 仅加速度    ：只惩罚加速度项，与速度无关。
// 任何输入（含 NaN/负数/极端值）下都保证返回 ∈ [max(minRate,1e-12), maxRate] 且 > 0。
double cyanoConversionRate(const CyanoConversion& c, double maxSpeed, double maxAccel, double digestion) {
    // --- 输出区间 ---
    double lo = (std::isfinite(c.minRate) && c.minRate > kMinPositiveRate) ? c.minRate : kMinPositiveRate;
    double hi = (std::isfinite(c.maxRate)) ? c.maxRate : lo;
    if (hi < lo) hi = lo;

    // --- 输入净化 ---
    const double speed = (std::isfinite(maxSpeed) && maxSpeed > 0.0) ? maxSpeed : 0.0;
    const double accel = (std::isfinite(maxAccel) && maxAccel > 0.0) ? maxAccel : 0.0;
    const double dig = (std::isfinite(digestion) && digestion > 0.0) ? digestion : 0.0;
    const double rate = (std::isfinite(c.rate)) ? c.rate : 1.0;
    const double speedRef = (std::isfinite(c.speedRef) && c.speedRef > kTiny) ? c.speedRef : 1.0;
    const double accelRef = (std::isfinite(c.accelRef) && c.accelRef > kTiny) ? c.accelRef : 1.0;
    const double wSpeed = (std::isfinite(c.wSpeed) && c.wSpeed > 0.0) ? c.wSpeed : 0.0;
    const double wAccel = (std::isfinite(c.wAccel) && c.wAccel > 0.0) ? c.wAccel : 0.0;
    const double exponent = (std::isfinite(c.exponent) && c.exponent > 0.0) ? c.exponent : 1.0;

    // 幂律模式下的归一化比值（下限保护，避免 0 机动性时比值发散）。
    const double speedRatio = speedRef / std::max(speed, 0.1 * speedRef);
    const double accelRatio = accelRef / std::max(accel, 0.1 * accelRef);

    double base = rate;
    switch (c.mode) {
        case 0:
            // 常数：不做机动性惩罚（仅由遗传消化系数缩放，见下方 dig）。
            base = rate;
            break;
        case 2:
            // 幂律：机动性越大 → 比值越小 → 转化率越低。
            base = rate * std::pow(speedRatio, exponent * wSpeed) * std::pow(accelRatio, exponent * wAccel);
            break;
        case 3:
            // 仅加速度项。
            base = rate / (1.0 + wAccel * (accel / accelRef));
            break;
        case 1:
        default:
            // 速度 × 加速度反比（默认 / 未知模式回退到最保守的取舍形式）。
            base = rate / ((1.0 + wSpeed * (speed / speedRef)) * (1.0 + wAccel * (accel / accelRef)));
            break;
    }

    double v = base * dig;
    if (!std::isfinite(v)) v = lo;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    if (!(v > 0.0)) v = lo;  // lo 恒 > 0，保证返回值严格为正
    return v;
}

// ---------------- 纯函数：阳光模型 ----------------
double sunDayFactor(int frame, const SunParams& s) {
    if (!(s.dayPeriodFrames > 0.0) || !std::isfinite(s.dayPeriodFrames)) {
        // 周期非法：退化为恒定值（仍保证 >= 0）。
        return (std::isfinite(s.baseLevel) && s.baseLevel > 0.0) ? s.baseLevel : 0.0;
    }
    const double phase = kTwoPi * static_cast<double>(frame) / s.dayPeriodFrames;
    const double base = std::isfinite(s.baseLevel) ? s.baseLevel : 0.0;
    const double amp = std::isfinite(s.dayAmplitude) ? s.dayAmplitude : 0.0;
    double v = base + amp * std::sin(phase);
    if (!(v > 0.0)) v = 0.0;
    return v;
}

double sunLevelAt(const Vec2& p, int frame, const SunParams& s, double worldW, double worldH) {
    const double day = sunDayFactor(frame, s);
    // 世界外坐标钳制到世界范围内，保证场在边界外也有定义（worldW 在此被使用）。
    double x = p.x;
    if (std::isfinite(worldW) && worldW > 0.0) {
        if (x < 0.0) x = 0.0;
        else if (x > worldW) x = worldW;
    }
    return sunField(x, p.y, frame, s, worldH, day);
}

// ---------------- Cyanobacteria ----------------
Cyanobacteria::Cyanobacteria(double worldWidth, double worldHeight, const CyanoParams& params)
    : worldW_((std::isfinite(worldWidth) && worldWidth > 0.0) ? worldWidth : 1.0),
      worldH_((std::isfinite(worldHeight) && worldHeight > 0.0) ? worldHeight : 1.0),
      params_(params) {
    // 网格单元边长取成链距离：邻居查询只需扫描 3×3 单元即可覆盖 linkDistance。
    // 具体尺寸计算与空网格构建统一交给 reshapeGrid()，避免与 setParams/setWorldSize 漂移。
    reshapeGrid();
}

void Cyanobacteria::reshapeGrid() {
    cellSize_ = (params_.linkDistance > 1.0) ? params_.linkDistance : 1.0;
    int c = static_cast<int>(std::ceil(worldW_ / cellSize_));
    if (c < 1) c = 1;
    else if (c > kMaxGridDim) c = kMaxGridDim;
    int r = static_cast<int>(std::ceil(worldH_ / cellSize_));
    if (r < 1) r = 1;
    else if (r > kMaxGridDim) r = kMaxGridDim;
    cols_ = c;
    rows_ = r;
    rebuildGrid();
}

void Cyanobacteria::rebuildGrid() {
    const size_t cells = static_cast<size_t>(cols_) * static_cast<size_t>(rows_);
    nodeCells_.assign(cells, std::vector<uint32_t>());
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const CyanoNode& nd = nodes_[i];
        if (nd.alive == 0) continue;  // 死亡节点不入格（等待下一次 step 移除）
        const int cx = gridCoord(nd.pos.x, cellSize_, cols_);
        const int cy = gridCoord(nd.pos.y, cellSize_, rows_);
        const size_t ci = static_cast<size_t>(cy) * static_cast<size_t>(cols_) + static_cast<size_t>(cx);
        if (ci < nodeCells_.size()) nodeCells_[ci].push_back(static_cast<uint32_t>(i));
    }
}

void Cyanobacteria::initialize(std::mt19937& rng, int totalNodes, int colonyCount, double initialEnergy) {
    nodes_.clear();
    links_.clear();
    nextNodeId_ = 1;

    if (totalNodes <= 0) {
        rebuildGrid();
        return;
    }

    const int colonies = (colonyCount > 0) ? colonyCount : 1;
    const double e0 = (std::isfinite(initialEnergy) && initialEnergy > 0.0) ? initialEnergy : 0.0;

    // 各聚落中心（确定性依赖 rng 调用顺序）。
    std::uniform_real_distribution<double> px(0.0, worldW_);
    std::uniform_real_distribution<double> py(0.0, worldH_);
    std::vector<Vec2> centers(static_cast<size_t>(colonies));
    for (int i = 0; i < colonies; ++i) {
        centers[static_cast<size_t>(i)] = Vec2(px(rng), py(rng));
    }

    // 聚落撒点半径：世界短边的 12%，保证「菌落形态」而非全图均匀噪声。
    const double minSide = (worldW_ < worldH_) ? worldW_ : worldH_;
    const double spread = minSide * 0.12;
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_real_distribution<double> angle(0.0, kTwoPi);

    nodes_.reserve(static_cast<size_t>(totalNodes));
    for (int i = 0; i < totalNodes; ++i) {
        const int colony = i % colonies;
        const Vec2 c = centers[static_cast<size_t>(colony)];
        const double a = angle(rng);
        const double r = spread * std::sqrt(unit(rng));  // 圆盘内均匀撒点
        Vec2 p(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
        if (p.x < 0.0) p.x = 0.0;
        else if (p.x > worldW_) p.x = worldW_;
        if (p.y < 0.0) p.y = 0.0;
        else if (p.y > worldH_) p.y = worldH_;

        CyanoNode nd;
        nd.pos = p;
        nd.energy = e0;
        nd.id = nextNodeId_++;
        nd.colonyId = static_cast<uint16_t>(colony % 65536);
        nd.type = static_cast<uint8_t>(CyanoNodeType::Normal);
        nd.alive = 1;
        nd.linkCount = 0;
        nd.age = 0;
        nodes_.push_back(nd);
        // 注意：**节点之间不预建连接**，连接由后续帧的连接维护自动形成。
    }

    rebuildGrid();
}

void Cyanobacteria::step(std::mt19937& rng, int frame, const SunParams& sun,
                         std::vector<CyanoBall>& outBalls) {
    // 空群落零开销短路：关闭蓝细菌（initialCyanoNodes<=0）时与基线完全一致（也不消耗 rng）。
    if (nodes_.empty()) return;

    // ---------- 0. 前置清理：真正移除上一帧/被吃掉标记死亡的节点 ----------
    // 紧凑化 nodes_，同时重映射 links_ 的全部下标（存的是数组下标，不移除会让 a/b 悬空）。
    {
        const size_t n = nodes_.size();
        size_t aliveTotal = 0;
        for (size_t i = 0; i < n; ++i) {
            if (nodes_[i].alive != 0) ++aliveTotal;
        }
        if (aliveTotal != n) {
            std::vector<int32_t> remap(n, -1);
            std::vector<CyanoNode> kept;
            kept.reserve(aliveTotal);
            for (size_t i = 0; i < n; ++i) {
                if (nodes_[i].alive == 0) continue;
                remap[i] = static_cast<int32_t>(kept.size());
                kept.push_back(nodes_[i]);
            }
            nodes_.swap(kept);

            std::vector<CyanoLink> newLinks;
            newLinks.reserve(links_.size());
            for (size_t li = 0; li < links_.size(); ++li) {
                const CyanoLink& l = links_[li];
                if (l.a >= n || l.b >= n) continue;  // 越界防御
                const int32_t a = remap[l.a];
                const int32_t b = remap[l.b];
                if (a < 0 || b < 0) continue;  // 任一端已死亡 → 断链
                if (a == b) continue;
                CyanoLink nl;
                nl.a = static_cast<uint32_t>(a < b ? a : b);
                nl.b = static_cast<uint32_t>(a < b ? b : a);
                nl.flow = 0.0;
                newLinks.push_back(nl);
            }
            links_.swap(newLinks);
            rebuildGrid();  // 下标全变了，网格必须重建
        }
    }
    if (nodes_.empty()) return;

    // 局部工具：把新节点增量插入空间网格（节点基本不动，不做每帧全量重建）。
    auto insertIntoGrid = [&](uint32_t idx, const Vec2& p) {
        if (nodeCells_.empty() || cols_ <= 0 || rows_ <= 0) return;
        const int cx = gridCoord(p.x, cellSize_, cols_);
        const int cy = gridCoord(p.y, cellSize_, rows_);
        const size_t ci = static_cast<size_t>(cy) * static_cast<size_t>(cols_) + static_cast<size_t>(cx);
        if (ci < nodeCells_.size()) nodeCells_[ci].push_back(idx);
    };

    // 局部工具：半径内是否存在存活节点（用于繁殖的最小间距检查，只在触发繁殖时调用）。
    auto hasAliveNodeWithin = [&](const Vec2& p, double radius) {
        if (!(radius > 0.0) || nodeCells_.empty()) return false;
        const double r2 = radius * radius;
        const int cx0 = gridCoord(p.x - radius, cellSize_, cols_);
        const int cx1 = gridCoord(p.x + radius, cellSize_, cols_);
        const int cy0 = gridCoord(p.y - radius, cellSize_, rows_);
        const int cy1 = gridCoord(p.y + radius, cellSize_, rows_);
        for (int cy = cy0; cy <= cy1; ++cy) {
            const size_t rowBase = static_cast<size_t>(cy) * static_cast<size_t>(cols_);
            for (int cx = cx0; cx <= cx1; ++cx) {
                const size_t ci = rowBase + static_cast<size_t>(cx);
                if (ci >= nodeCells_.size()) continue;
                for (uint32_t j : nodeCells_[ci]) {
                    if (static_cast<size_t>(j) >= nodes_.size()) continue;
                    const CyanoNode& nd = nodes_[j];
                    if (nd.alive == 0) continue;
                    const double dx = nd.pos.x - p.x;
                    const double dy = nd.pos.y - p.y;
                    if (dx * dx + dy * dy < r2) return true;
                }
            }
        }
        return false;
    };

    // ---------- 1. 光合作用 + 代谢消耗 + 年龄 ----------
    // 昼夜系数每帧只算一次；逐节点只算位置相关的空间部分。
    const double day = sunDayFactor(frame, sun);
    const size_t frameStartNodes = nodes_.size();
    for (size_t i = 0; i < frameStartNodes; ++i) {
        CyanoNode& nd = nodes_[i];
        if (nd.alive == 0) continue;
        const double level = sunField(nd.pos.x, nd.pos.y, frame, sun, worldH_, day);
        nd.energy += params_.photosynthesisRate * lightUptake(level, params_.lightHalfSaturation);
        nd.energy -= params_.metabolismRate;
        ++nd.age;
        if (!(nd.energy > 0.0)) {  // 能量耗尽 → 死亡（下一次 step 开头才真正移除）
            nd.energy = 0.0;
            nd.alive = 0;
        }
    }

    // ---------- 2. 连接维护（每 linkCheckInterval 帧一次，绝不做每帧 O(N²)） ----------
    const int interval = (params_.linkCheckInterval > 0) ? params_.linkCheckInterval : 1;
    const int phase = (frame >= 0) ? (frame % interval) : 0;
    if (phase == 0) {
        auto maintainLinks = [&]() {
            const size_t n = nodes_.size();
            const double linkD = params_.linkDistance;
            const int cap = (params_.maxLinksPerNode > 0) ? params_.maxLinksPerNode : 0;
            if (n == 0 || !(linkD > 0.0) || cap <= 0) return;
            const double linkD2 = linkD * linkD;

            // 本轮临时邻接表（节点下标 → 邻居节点下标）：用于去重与度数上限判断。
            std::vector<std::vector<uint32_t>> adj(n);
            for (size_t li = 0; li < links_.size(); ++li) {
                const CyanoLink& l = links_[li];
                if (l.a >= n || l.b >= n) continue;
                if (nodes_[l.a].alive == 0 || nodes_[l.b].alive == 0) continue;
                adj[l.a].push_back(l.b);
                adj[l.b].push_back(l.a);
            }
            std::vector<uint16_t> deg(n, 0);
            for (size_t i = 0; i < n; ++i) {
                const size_t d = adj[i].size();
                deg[i] = (d > static_cast<size_t>(kMaxLinkCountCache))
                             ? kMaxLinkCountCache
                             : static_cast<uint16_t>(d);
            }

            for (size_t i = 0; i < n; ++i) {
                if (nodes_[i].alive == 0) continue;
                if (deg[i] >= static_cast<uint16_t>(cap > 65535 ? 65535 : cap)) continue;
                const CyanoNode& a = nodes_[i];
                const int cx0 = gridCoord(a.pos.x - linkD, cellSize_, cols_);
                const int cx1 = gridCoord(a.pos.x + linkD, cellSize_, cols_);
                const int cy0 = gridCoord(a.pos.y - linkD, cellSize_, rows_);
                const int cy1 = gridCoord(a.pos.y + linkD, cellSize_, rows_);
                bool iFull = false;
                for (int cy = cy0; cy <= cy1 && !iFull; ++cy) {
                    const size_t rowBase = static_cast<size_t>(cy) * static_cast<size_t>(cols_);
                    for (int cx = cx0; cx <= cx1 && !iFull; ++cx) {
                        const size_t ci = rowBase + static_cast<size_t>(cx);
                        if (ci >= nodeCells_.size()) continue;
                        const std::vector<uint32_t>& bucket = nodeCells_[ci];
                        for (uint32_t j : bucket) {
                            if (deg[i] >= cap) {  // 本节点已达连接上限
                                iFull = true;
                                break;
                            }
                            if (static_cast<size_t>(j) >= n || j == i) continue;
                            if (nodes_[j].alive == 0) continue;
                            if (deg[j] >= cap) continue;
                            const CyanoNode& b = nodes_[j];
                            const double dx = b.pos.x - a.pos.x;
                            const double dy = b.pos.y - a.pos.y;
                            if (dx * dx + dy * dy > linkD2) continue;
                            // 已连接？扫本节点邻居列表（度 ≤ cap，代价极小）。
                            if (std::find(adj[i].begin(), adj[i].end(), j) != adj[i].end()) continue;
                            CyanoLink nl;
                            nl.a = static_cast<uint32_t>(i);
                            nl.b = j;
                            nl.flow = 0.0;
                            if (nl.a > nl.b) std::swap(nl.a, nl.b);  // 契约：a < b
                            links_.push_back(nl);
                            adj[i].push_back(j);
                            adj[j].push_back(static_cast<uint32_t>(i));
                            ++deg[i];
                            ++deg[j];
                        }
                    }
                }
            }
        };
        maintainLinks();
    }

    // ---------- 3. 能量传输（沿连接，高→低，保证双方非负） ----------
    for (size_t li = 0; li < links_.size(); ++li) {
        CyanoLink& l = links_[li];
        if (l.a >= nodes_.size() || l.b >= nodes_.size()) {
            l.flow = 0.0;
            continue;
        }
        CyanoNode& na = nodes_[l.a];
        CyanoNode& nb = nodes_[l.b];
        if (na.alive == 0 || nb.alive == 0) {
            l.flow = 0.0;
            continue;
        }
        double diff = na.energy - nb.energy;
        CyanoNode* hi = nullptr;
        CyanoNode* lo = nullptr;
        if (diff > params_.transferDeadband) {
            hi = &na;
            lo = &nb;
        } else if (-diff > params_.transferDeadband) {
            hi = &nb;
            lo = &na;
        } else {
            l.flow = 0.0;  // 死区内不传输，避免能量抖动
            continue;
        }
        double delta = params_.transferRate * (hi->energy - lo->energy) * 0.5;
        if (!(delta > 0.0)) {
            l.flow = 0.0;
            continue;
        }
        if (delta > hi->energy) delta = hi->energy;  // 高能端不为负
        hi->energy -= delta;
        lo->energy += delta;
        if (!(hi->energy > 0.0)) hi->energy = 0.0;   // 浮点兜底
        if (!(lo->energy > 0.0)) lo->energy = 0.0;
        // flow 符号约定：>0 表示能量自 a 流向 b。
        l.flow = (hi == &na) ? delta : -delta;
    }

    // ---------- 4. 节点繁殖（消耗较多能量 → 附近生成新节点；过近则取消） ----------
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_real_distribution<double> angle(0.0, kTwoPi);
    // 只让「本帧开始时就存在」的节点繁殖/产球：新节点从下一帧起才参与，避免同帧级联放大。
    const size_t reproductionEnd = frameStartNodes;
    for (size_t i = 0; i < reproductionEnd; ++i) {
        CyanoNode& nd = nodes_[i];
        if (nd.alive == 0) continue;
        if (nd.energy < params_.reproductionCost) continue;
        if (params_.reproductionProbability > 0.0 &&
            unit(rng) >= params_.reproductionProbability) {
            continue;
        }
        if (!(params_.reproductionProbability > 0.0)) continue;

        const double a = angle(rng);
        const double r = params_.reproductionOffset * (0.8 + 0.4 * unit(rng));
        Vec2 p(nd.pos.x + std::cos(a) * r, nd.pos.y + std::sin(a) * r);
        if (p.x < 0.0) p.x = 0.0;
        else if (p.x > worldW_) p.x = worldW_;
        if (p.y < 0.0) p.y = 0.0;
        else if (p.y > worldH_) p.y = worldH_;

        // 与已有节点（含父节点）过近 → 取消本次繁殖（需求给出的「取消或合并」策略取「取消」，避免重叠）。
        if (hasAliveNodeWithin(p, params_.minNodeSeparation)) continue;

        CyanoNode child;
        child.pos = p;
        child.energy = params_.reproductionCost * 0.5;  // 子节点能量由繁殖成本派生
        child.id = nextNodeId_++;
        child.colonyId = nd.colonyId;  // 继承所属聚落与群落规则
        child.type = static_cast<uint8_t>(CyanoNodeType::Normal);
        child.alive = 1;
        child.linkCount = 0;
        child.age = 0;

        nd.energy -= params_.reproductionCost;  // 先扣费（push_back 后 nd 可能失效）
        if (!(nd.energy > 0.0)) nd.energy = 0.0;
        nodes_.push_back(child);
        insertIntoGrid(static_cast<uint32_t>(nodes_.size() - 1), p);
        // 不在此处预建连接：由下一轮连接维护自动成链。
    }

    // ---------- 5. 产生小球（自由小球的主要来源之一） ----------
    for (size_t i = 0; i < reproductionEnd; ++i) {
        CyanoNode& nd = nodes_[i];
        if (nd.alive == 0) continue;
        if (nd.energy < params_.ballSpawnCost) continue;
        if (!(params_.ballSpawnProbability > 0.0)) continue;
        if (unit(rng) >= params_.ballSpawnProbability) continue;

        nd.energy -= params_.ballSpawnCost;
        if (!(nd.energy > 0.0)) nd.energy = 0.0;

        const double a = angle(rng);
        const double r = 5.0 * unit(rng);
        CyanoBall ball;
        ball.pos = Vec2(nd.pos.x + std::cos(a) * r, nd.pos.y + std::sin(a) * r);
        const double va = angle(rng);
        const double vs = 0.2 + 0.8 * unit(rng);
        ball.vel = Vec2(std::cos(va) * vs, std::sin(va) * vs);
        // 冻结的 CyanoParams 未含球类型字段：默认产出系统基础资源（Worker/资源型）。
        ball.type = BallType::Worker;
        outBalls.push_back(ball);
        // 本模块不处理球的物理与归属：球由 World 追加为**自由小球**。
    }

    // ---------- 6. 清理：断链（死亡 / 过远）+ 同步 linkCount 缓存 ----------
    const double breakD = params_.linkDistance * kBreakDistanceFactor;
    const double breakD2 = breakD * breakD;
    size_t keep = 0;
    for (size_t li = 0; li < links_.size(); ++li) {
        const CyanoLink l = links_[li];
        bool ok = (l.a < nodes_.size() && l.b < nodes_.size());
        if (ok) {
            const CyanoNode& na = nodes_[l.a];
            const CyanoNode& nb = nodes_[l.b];
            ok = (na.alive != 0 && nb.alive != 0);
            if (ok && breakD > 0.0) {
                const double dx = na.pos.x - nb.pos.x;
                const double dy = na.pos.y - nb.pos.y;
                ok = (dx * dx + dy * dy) <= breakD2;
            }
        }
        if (ok) {
            if (keep != li) links_[keep] = l;
            ++keep;
        }
    }
    links_.resize(keep);

    for (size_t i = 0; i < nodes_.size(); ++i) nodes_[i].linkCount = 0;
    for (size_t li = 0; li < links_.size(); ++li) {
        const CyanoLink& l = links_[li];
        if (l.a < nodes_.size() && l.b < nodes_.size()) {
            if (nodes_[l.a].linkCount < kMaxLinkCountCache) ++nodes_[l.a].linkCount;
            if (nodes_[l.b].linkCount < kMaxLinkCountCache) ++nodes_[l.b].linkCount;
        }
    }
}

int Cyanobacteria::nodesNear(const Vec2& pos, double radius, int* outIdx, double* outDistSq,
                             int maxOut) const {
    // 热路径（World::absorbBalls 里每个核每帧调用）：**无任何堆分配**，只写调用方缓冲区。
    if (outIdx == nullptr || outDistSq == nullptr || maxOut <= 0) return 0;
    if (!(radius > 0.0) || nodes_.empty() || nodeCells_.empty()) return 0;
    const double r2 = radius * radius;
    const int cx0 = gridCoord(pos.x - radius, cellSize_, cols_);
    const int cx1 = gridCoord(pos.x + radius, cellSize_, cols_);
    const int cy0 = gridCoord(pos.y - radius, cellSize_, rows_);
    const int cy1 = gridCoord(pos.y + radius, cellSize_, rows_);

    int found = 0;
    for (int cy = cy0; cy <= cy1; ++cy) {
        const size_t rowBase = static_cast<size_t>(cy) * static_cast<size_t>(cols_);
        for (int cx = cx0; cx <= cx1; ++cx) {
            const size_t ci = rowBase + static_cast<size_t>(cx);
            if (ci >= nodeCells_.size()) continue;
            const std::vector<uint32_t>& bucket = nodeCells_[ci];
            for (uint32_t j : bucket) {
                if (static_cast<size_t>(j) >= nodes_.size()) continue;
                const CyanoNode& nd = nodes_[j];
                if (nd.alive == 0) continue;
                const double dx = nd.pos.x - pos.x;
                const double dy = nd.pos.y - pos.y;
                const double d2 = dx * dx + dy * dy;
                if (d2 > r2) continue;
                outIdx[found] = static_cast<int>(j);
                outDistSq[found] = d2;
                ++found;
                if (found >= maxOut) return found;  // 容量用尽：立即返回实际写入个数
            }
        }
    }
    return found;
}

double Cyanobacteria::consumeNode(uint32_t idx) {
    // 只标死亡、**不立即移除**：保证同一帧内 World 手上的下标仍然有效（links_ 重映射推迟到下一次 step）。
    if (idx >= nodes_.size()) return 0.0;
    CyanoNode& nd = nodes_[idx];
    if (nd.alive == 0) return 0.0;
    double e = nd.energy;
    if (!(e > 0.0)) e = 0.0;  // 兼容 NaN / 负值
    nd.energy = 0.0;
    nd.alive = 0;
    return e;
}

int Cyanobacteria::aliveNodeCount() const {
    int c = 0;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].alive != 0) ++c;
    }
    return c;
}

int Cyanobacteria::aliveLinkCount() const {
    const size_t n = nodes_.size();
    int c = 0;
    for (size_t li = 0; li < links_.size(); ++li) {
        const CyanoLink& l = links_[li];
        if (l.a >= n || l.b >= n) continue;
        if (nodes_[l.a].alive != 0 && nodes_[l.b].alive != 0) ++c;  // 只统计两端都存活
    }
    return c;
}

double Cyanobacteria::averageNodeEnergy() const {
    double sum = 0.0;
    int c = 0;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].alive == 0) continue;
        sum += nodes_[i].energy;
        ++c;
    }
    return (c > 0) ? (sum / static_cast<double>(c)) : 0.0;
}

// ==================== v2.2.0：玩家在线调控 / 存档 v2 ====================

void Cyanobacteria::setParams(const CyanoParams& p) {
    // linkDistance 决定空间网格单元边长；只有它变了才需要重建网格。
    const bool gridShapeChanged = (p.linkDistance != params_.linkDistance);
    params_ = p;
    if (gridShapeChanged) {
        reshapeGrid();  // 零 RNG
    }
}

void Cyanobacteria::setWorldSize(double w, double h) {
    worldW_ = (std::isfinite(w) && w > 0.0) ? w : 1.0;
    worldH_ = (std::isfinite(h) && h > 0.0) ? h : 1.0;
    // 节点位置钳制回新世界范围（读档/运行中缩小世界时防止节点落在界外）。
    for (size_t i = 0; i < nodes_.size(); ++i) {
        CyanoNode& nd = nodes_[i];
        if (nd.pos.x < 0.0) nd.pos.x = 0.0;
        else if (nd.pos.x > worldW_) nd.pos.x = worldW_;
        if (nd.pos.y < 0.0) nd.pos.y = 0.0;
        else if (nd.pos.y > worldH_) nd.pos.y = worldH_;
    }
    reshapeGrid();  // 零 RNG
}

void Cyanobacteria::clear() {
    nodes_.clear();
    links_.clear();
    nextNodeId_ = 1;
    reshapeGrid();  // 空网格；零 RNG
}

int Cyanobacteria::addNodes(std::mt19937& rng, int count, int colonyCount, double initialEnergy) {
    if (count <= 0) return 0;
    const int colonies = (colonyCount > 0) ? colonyCount : 1;
    const double e0 = (std::isfinite(initialEnergy) && initialEnergy > 0.0) ? initialEnergy : 0.0;

    // 聚落中心：已有存活节点 → 以存活节点为中心（模拟"菌落扩张/爆发"）；
    // 空群落 → 随机撒聚落中心（与 initialize 的语义一致）。
    std::uniform_real_distribution<double> px(0.0, worldW_);
    std::uniform_real_distribution<double> py(0.0, worldH_);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_real_distribution<double> angle(0.0, kTwoPi);

    std::vector<uint32_t> aliveIdx;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].alive != 0) aliveIdx.push_back(static_cast<uint32_t>(i));
    }
    std::vector<Vec2> centers(static_cast<size_t>(colonies));
    if (!aliveIdx.empty()) {
        for (int i = 0; i < colonies; ++i) {
            const size_t pick = static_cast<size_t>(i) % aliveIdx.size();
            centers[static_cast<size_t>(i)] = nodes_[aliveIdx[pick]].pos;
        }
    } else {
        for (int i = 0; i < colonies; ++i) {
            centers[static_cast<size_t>(i)] = Vec2(px(rng), py(rng));
        }
    }

    const double minSide = (worldW_ < worldH_) ? worldW_ : worldH_;
    const double spread = minSide * 0.12;  // 与 initialize 相同的聚落半径

    int added = 0;
    for (int k = 0; k < count; ++k) {
        const int colony = k % colonies;
        const Vec2 c = centers[static_cast<size_t>(colony)];
        const double a = angle(rng);
        const double r = spread * std::sqrt(unit(rng));
        Vec2 p(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
        if (p.x < 0.0) p.x = 0.0;
        else if (p.x > worldW_) p.x = worldW_;
        if (p.y < 0.0) p.y = 0.0;
        else if (p.y > worldH_) p.y = worldH_;

        CyanoNode nd;
        nd.pos = p;
        nd.energy = e0;
        nd.id = nextNodeId_++;
        nd.colonyId = static_cast<uint16_t>(colony % 65536);
        nd.type = static_cast<uint8_t>(CyanoNodeType::Normal);
        nd.alive = 1;
        nd.linkCount = 0;
        nd.age = 0;
        nodes_.push_back(nd);
        ++added;
    }
    // 不预建连接：由后续帧的连接维护自动成链（与 initialize 一致）。
    rebuildGrid();  // 零 RNG
    return added;
}

void Cyanobacteria::save(std::vector<CyanoNode>& outNodes, std::vector<CyanoLink>& outLinks,
                         uint32_t& outNextNodeId) const {
    outNodes = nodes_;
    outLinks = links_;
    outNextNodeId = nextNodeId_;
}

bool Cyanobacteria::load(const std::vector<CyanoNode>& inNodes, const std::vector<CyanoLink>& inLinks,
                         uint32_t inNextNodeId) {
    nodes_ = inNodes;
    links_.clear();
    links_.reserve(inLinks.size());
    uint32_t maxId = 0;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        CyanoNode& nd = nodes_[i];
        if (nd.id > maxId) maxId = nd.id;
        // 位置钳制回世界范围（存档可能来自不同尺寸的世界）。
        if (nd.pos.x < 0.0) nd.pos.x = 0.0;
        else if (nd.pos.x > worldW_) nd.pos.x = worldW_;
        if (nd.pos.y < 0.0) nd.pos.y = 0.0;
        else if (nd.pos.y > worldH_) nd.pos.y = worldH_;
        nd.linkCount = 0;
    }
    const size_t n = nodes_.size();
    for (size_t li = 0; li < inLinks.size(); ++li) {
        CyanoLink l = inLinks[li];
        if (l.a >= n || l.b >= n) continue;  // 越界端点：丢弃（存档损坏防御）
        if (l.a == l.b) continue;            // 自环：丢弃
        if (l.a > l.b) std::swap(l.a, l.b);  // 契约：a < b（nodes_ 下标）
        links_.push_back(l);
    }
    // 恢复 linkCount 缓存（与 step() 第 6 段口径一致：只计两端存活）。
    for (size_t li = 0; li < links_.size(); ++li) {
        const CyanoLink& l = links_[li];
        if (nodes_[l.a].linkCount < kMaxLinkCountCache) ++nodes_[l.a].linkCount;
        if (nodes_[l.b].linkCount < kMaxLinkCountCache) ++nodes_[l.b].linkCount;
    }
    // nextNodeId 必须大于已用最大 id，否则后续繁殖会撞 id。
    nextNodeId_ = (inNextNodeId > maxId) ? inNextNodeId : (maxId + 1);
    if (nextNodeId_ == 0) nextNodeId_ = 1;
    rebuildGrid();  // 零 RNG
    return true;
}

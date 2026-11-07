#pragma once
#include <cstdint>
#include <random>
#include <vector>
#include "BallType.h"
#include "Vec2.h"

enum class CyanoNodeType : uint8_t { Normal = 0 };
constexpr int CYANO_NODE_TYPE_COUNT = 1;

struct CyanoNode {
    Vec2 pos;
    double energy = 1.0;
    uint32_t id = 0;         // 稳定 ID，全局唯一，跨帧有效
    uint16_t colonyId = 0;   // 所属聚落
    uint8_t type = 0;
    uint8_t alive = 1;
    uint16_t linkCount = 0;  // 当前连接数（缓存，由群落维护）
    int age = 0;
};

struct CyanoLink { uint32_t a = 0, b = 0; double flow = 0.0; };  // a < b（nodes_ 数组下标）
struct CyanoBall { Vec2 pos; Vec2 vel; BallType type = BallType::Worker; };

// 阳光模型参数
struct SunParams {
    double baseLevel = 1.0;
    double dayAmplitude = 0.7;
    double dayPeriodFrames = 1200.0;
    double verticalGradient = 0.3;
    double noiseAmplitude = 0.1;
    double noiseFrequency = 0.01;
    double noiseSpeed = 0.002;
};

// 群落规则参数
struct CyanoParams {
    double photosynthesisRate = 0.02;
    double lightHalfSaturation = 0.5;
    double metabolismRate = 0.005;
    double linkDistance = 80.0;
    int    maxLinksPerNode = 6;
    int    linkCheckInterval = 4;
    double transferRate = 0.05;
    double transferDeadband = 0.05;
    double ballSpawnCost = 8.0;
    double ballSpawnProbability = 0.002;
    double reproductionCost = 20.0;
    double reproductionProbability = 0.001;
    double reproductionOffset = 25.0;
    double minNodeSeparation = 15.0;
};

// 核捕食转化率函数的配置（POD，**不得依赖 WorldConfig / World.h**）
struct CyanoConversion {
    int    mode = 1;        // 0=常数 1=速度×加速度反比（取舍） 2=幂律 3=仅加速度
    double rate = 1.0;
    double exponent = 1.0;
    double speedRef = 60.0;
    double accelRef = 3.0;
    double wSpeed = 1.0;    // 模式 1/2 中速度项的权重
    double wAccel = 1.0;    // 模式 1/2/3 中加速度项的权重
    double minRate = 0.001;
    double maxRate = 10.0;
};

// 纯函数：转化率。digestion 来自核的遗传参数 cyanoDigestion。
// 必须保证返回值恒 ∈ [c.minRate, c.maxRate] 且 > 0。
// 设计意图：形成「机动性↑ ⇒ 消化效率↓」的取舍。
double cyanoConversionRate(const CyanoConversion& c, double maxSpeed, double maxAccel, double digestion);

// 纯函数：全局昼夜系数（与位置无关），frame 从 0 开始。
double sunDayFactor(int frame, const SunParams& s);
// 纯函数：位置 + 时间的完整阳光值（>= 0）。
double sunLevelAt(const Vec2& p, int frame, const SunParams& s, double worldW, double worldH);

class Cyanobacteria {
public:
    Cyanobacteria(double worldWidth, double worldHeight, const CyanoParams& params);

    // 若干聚落形式随机撒点；**节点之间不预建连接**（由后续帧自动形成）。
    void initialize(std::mt19937& rng, int totalNodes, int colonyCount, double initialEnergy);

    // 每帧主更新（内部自带空间网格，避免 O(N²)）：
    //   光合 → 代谢(死) → 连接维护(每 linkCheckInterval 帧) → 能量传输 → 繁殖 → 产球 → 清理死亡/断链
    // outBalls: 本帧新产生的自由小球，由 World 追加（模块自己不碰 World）。
    void step(std::mt19937& rng, int frame, const SunParams& sun,
              std::vector<CyanoBall>& outBalls);

    // ---- 供核捕食（同一帧内索引有效；节点移除只发生在下一次 step 开头）----
    // **无分配**：捕食在 World::absorbBalls() 里对每个核每帧调用，禁止在这里 new/vector 分配。
    // 写入 outIdx/outDistSq（容量 maxOut），**返回实际写入个数**；不保证按距离排序。
    int nodesNear(const Vec2& pos, double radius, int* outIdx, double* outDistSq, int maxOut) const;
    CyanoNode& nodeAt(uint32_t idx) { return nodes_[idx]; }
    const CyanoNode& nodeAt(uint32_t idx) const { return nodes_[idx]; }
    // 标记死亡并返回其能量（已被吃掉/无效则返回 0）；**不立即移除**，避免索引失效。
    double consumeNode(uint32_t idx);

    // ---- 统计与只读访问 ----
    int aliveNodeCount() const;
    int aliveLinkCount() const;          // ⚠️ 只统计两端都 alive 的连接
    double averageNodeEnergy() const;
    const std::vector<CyanoNode>& nodes() const { return nodes_; }
    const std::vector<CyanoLink>& links() const { return links_; }
    const CyanoParams& params() const { return params_; }
    bool empty() const { return nodes_.empty(); }
    double worldWidth() const { return worldW_; }
    double worldHeight() const { return worldH_; }
    uint32_t nextNodeId() const { return nextNodeId_; }

    // ---- v2.2.0：玩家在线调控 / 存档 v2（全部不消耗 RNG 者已在注释标明）----
    // 更新规则参数（不重建节点；linkDistance 变化时重建内部空间网格）。零 RNG。
    void setParams(const CyanoParams& p);
    // 世界尺寸变化（读档/细调世界尺寸）时重建内部空间网格并把节点钳制回世界内。零 RNG。
    void setWorldSize(double w, double h);
    // 清空群落并复位 nextNodeId（关闭蓝细菌用）。**零 RNG**。
    void clear();
    // 在现有聚落附近追加 count 个节点（玩家 bloom / 在线开启）；返回实际添加数。
    // **消耗主 rng**（玩家行为），调用顺序固定：聚落中心（仅空群落时）→ 每节点角度/半径。
    int addNodes(std::mt19937& rng, int count, int colonyCount, double initialEnergy);
    // 存档 v2：原样导出节点/连接/nextNodeId（不消耗 RNG）。
    void save(std::vector<CyanoNode>& outNodes, std::vector<CyanoLink>& outLinks,
              uint32_t& outNextNodeId) const;
    // 存档 v2 恢复：校验连接端点后重建内部网格与 linkCount 缓存（不消耗 RNG）。
    bool load(const std::vector<CyanoNode>& inNodes, const std::vector<CyanoLink>& inLinks,
              uint32_t inNextNodeId);

private:
    double worldW_, worldH_;
    CyanoParams params_;
    std::vector<CyanoNode> nodes_;
    std::vector<CyanoLink> links_;
    uint32_t nextNodeId_ = 1;
    int cols_ = 0, rows_ = 0;
    double cellSize_ = 0.0;
    std::vector<std::vector<uint32_t>> nodeCells_;  // 内部空间网格（节点桶）
    void rebuildGrid();
    // 按当前 world 尺寸 + linkDistance 重算 cols_/rows_/cellSize_ 并重建网格。
    void reshapeGrid();
};

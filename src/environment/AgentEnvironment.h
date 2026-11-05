// agentsim - environment/AgentEnvironment.h
// 环境层：Torus 世界包装。R1-B（蓝细菌取代食物）后本类只保留环面几何、
// tick 推进、障碍谓词与 SimConfig 只读引用——Food 实体及其生成/查询/消耗/
// 再生/统计（spawnInitialFood / queryFood / consumeFood / regrowFood /
// foodCount / totalFoodEnergy / foods()）已整体删除，第一营养级完全由
// CyanoField（environment/CyanoNode.h）承担。
// 不含实体（核/小球）状态；实体状态归行为层所有。
#pragma once
#include <cstdint>

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Vec2.h"

namespace agentsim {

class AgentEnvironment {
public:
    AgentEnvironment() = default;
    explicit AgentEnvironment(const SimConfig& config);

    // R6-P1-1：可选障碍谓词（函数指针 + 上下文，避免 environment 层直接依赖
    // Terrain、保持分层单向）。R1-B：Food 撒点已删除，本谓词保留为环境层
    // 通用"不可放置"判定接口（当前生产路径无人调用；单测覆盖其惰性语义）。
    using BlockedFn = bool (*)(void* ctx, Vec2 pos);
    void setBlockedPredicate(BlockedFn fn, void* ctx = nullptr);
    bool blockedAt(Vec2 p) const { return blockedFn_ != nullptr && blockedFn_(blockedCtx_, p); }

    // 环境步进：推进内部 tick 计数。
    void tick();

    // Torus 几何转发（环境是环面坐标的唯一权威）。
    Vec2 wrap(Vec2 v) const;
    Vec2 torusDelta(Vec2 a, Vec2 b) const;
    double torusDistance(Vec2 a, Vec2 b) const;

    Tick tickCount() const { return tick_; }

    const SimConfig& config() const { return config_; }

private:
    SimConfig config_{};
    Tick tick_ = 0;

    // R6-P1-1：障碍谓词（未设置恒 false）。
    BlockedFn blockedFn_ = nullptr;
    void* blockedCtx_ = nullptr;
};

} // namespace agentsim

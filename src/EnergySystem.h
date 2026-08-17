#pragma once
#include <array>
#include <ostream>
#include "EventBus.h"

class Nucleus;

// 能量收支原因：能量统计按原因归类，mod 增加新机制时扩展此枚举。
enum class EnergyReason {
    ABSORB,        // 吸收小球
    COMBAT,        // 核间争斗转移
    METABOLISM,    // 代谢消耗
    REPRODUCTION,  // 繁殖代价
    AGING,         // 预留：衰老
    CROWDING,      // 预留：拥挤
    PLAGUE,        // 示例 mod / 后续瘟疫
    BALL_LOSS      // 失球惩罚（Phase 0.5）
};

// 核能量收支的唯一通道：所有能量变化必须经 apply()。
// 按原因累计统计（delta>0 记 in，delta<0 记 out），并统一死亡判定。
class EnergySystem {
public:
    explicit EnergySystem(EventBus& bus) : bus_(bus) {}

    // 唯一入口：修改能量、累计统计、判定死亡。
    // 若目标已死亡则忽略（保持与旧代码"死亡即静止"一致）。
    double apply(Nucleus& n, double delta, EnergyReason why);

    // 调试：按原因汇总收支。
    void printSummary(std::ostream& os) const;

    // 累计统计（in/out，不清零，跨帧累计）。
    double totalIn(EnergyReason why) const { return totals_[(size_t)why][0]; }
    double totalOut(EnergyReason why) const { return totals_[(size_t)why][1]; }

private:
    static constexpr size_t kReasonCount = 8;  // 与 EnergyReason 枚举项数一致
    EventBus& bus_;
    std::array<double[2], kReasonCount> totals_{};  // [reason][0]=in, [1]=out
};

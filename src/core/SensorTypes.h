// agentsim - core/SensorTypes.h
// 感知/决策共享向量类型（中立头）。G1 修复：SensorVector/ActionVector 原先定义在
// decision/IDecisionPolicy.h，造成 探测->决策 的逆向依赖；上移到 core/ 后依赖链恢复单向：
// environment -> perception -> decision -> behavior -> ledger（GOAL G2 / S4）。
//
// R2 契约（V2-SPEC-AMEND-1）：感知 18 -> 24 维；ActionVector 新增"点名施力"与
// "社会参数调制"两组字段。本文件是 R2 两路并行工作的**唯一接口契约**，主代理维护，
// 子代理只读不改（要改先在报告里提）。
#pragma once

#include "core/Types.h"

namespace agentsim {

// ---- 施力目标（决策层可以点名让核花能量推的球）----
// 容错与性能：固定 8 槽栈上数组，无堆分配（热路径：每核每帧一次）。
// 排序约定：按环面距离升序，items[0] 是最近的球（感知层保证），决策层可直接用下标。
constexpr int kMaxForceTargets = 8;

struct ForceTarget {
    EntityId id = 0;      // 球的实体 id
    double dx = 0.0;      // 核->球的**最近像**环面位移（不是核->球直线，跨接缝时不同）
    double dy = 0.0;
    double dist = 0.0;    // 环面距离 = sqrt(dx^2+dy^2)
    int type = 0;         // 0=Resource 1=Scout 2=Shield（与 behavior::BallType 对齐）
    // 注：曾有 double energy 字段（"球当前储能，决策可据此判断值不值得推"），
    // 2026-11-01 删除 —— 8 个策略没有一个读它（ForageBank/RiskAverse 只用 dist/dx/dy），
    // 是死字段。GOAL §二.6「能删就删」。删除同时消除了它与
    // "行为层是唯一写点" 守卫的假阳性（targets.items[i].energy = 形状像实体写入）。
};

struct TargetList {
    int count = 0;                       // 有效项数 ∈ [0, kMaxForceTargets]
    ForceTarget items[kMaxForceTargets]{};
};

struct SensorVector {
    // 24 维。s[0..17] 语义与旧版逐位不变（回归安全），s[18..23] 为 R2 新增：
    //   s[18] = 最近邻核的环面距离 / nucleusSightRadius（钳到 [0,1]，1 = 无邻居或超出视野）
    //   s[19] = 最近邻核相对方向 x（-1..1，无邻居时 0）
    //   s[20] = 最近邻核相对方向 y（-1..1，无邻居时 0）
    //   s[21] = 自身能量 / capacity（钳到 [0,1]；capacity <= 0 时 0）
    //   s[22] = 范围内可施力球数量 / kMaxForceTargets
    //   s[23] = 最近可施力球的环面距离 / senseRadius（钳到 [0,1]，无目标时 1）
    // 所有按下标遍历的代码自动适配；PerceptronPolicy::Params::w 必须同步扩到 [2][kDim]。
    static constexpr int kDim = 24;
    double v[kDim] = {};
    double& operator[](int i) { return v[i]; }
    double operator[](int i) const { return v[i]; }
};

struct ActionVector {
    double dirX = 0.0;   // 期望方向（未归一化也可）
    double dirY = 0.0;
    double boost = 0.0;  // 0..1 冲刺意愿，影响速度

    // ---- R2 新增：点名施力（V2-SPEC-AMEND-1 §2）----
    // forceSlot = TargetList 下标；-1 表示本帧不施力（默认，也是"不花能量"的唯一方式）。
    // forceX/forceY 是期望的加速度矢量（未归一化）：行为层负责
    //   1) 校验 slot 有效、球仍在 genome.forceRange 内
    //   2) 把 |a| 钳到 maxAccel
    //   3) cost = genome.forceCostK * |a|^2，付不起则按比例缩小
    //   4) ledger.dissipate("applyForce", cost)，然后球 vel += a
    // 行为层是唯一写点，决策层只表达意图。
    int forceSlot = -1;
    double forceX = 0.0;
    double forceY = 0.0;

    // ---- R2 新增：社会参数调制（V2-SPEC-AMEND-1 §1）----
    // 每项 ∈ [-1,1]（越界由行为层钳制），对应：
    //   mod[0] -> genome.nucleusSightRadius
    //   mod[1] -> genome.nucleusPersonalSpace
    //   mod[2] -> genome.nucleusRepelStrength
    //   mod[3] -> genome.nucleusCohesion
    // 行为层按 eff = base * (1 + kSocialModLimit * mod) 计算本帧生效值，kSocialModLimit = 0.4。
    // **基因本身不被改写**（表型可塑，非拉马克式继承）；调制不消耗能量，每帧重算。
    double mod[4] = {0.0, 0.0, 0.0, 0.0};
};

} // namespace agentsim

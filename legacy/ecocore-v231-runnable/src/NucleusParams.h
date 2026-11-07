#pragma once
#include <array>
#include <random>
#include "BallType.h"

// 遗传参数的随机初始化范围与变异裁剪范围（均可在环境配置文件中调节）。
struct ParamRanges {
    // ---- 随机初始化范围（均匀分布）----
    double affinityInitMin = 0.1, affinityInitMax = 1.0;
    double orbitRadiusInitMin = 30.0, orbitRadiusInitMax = 200.0;
    double absorbPreferenceInitMin = 0.5, absorbPreferenceInitMax = 1.5;
    double repelStrengthInitMin = 0.5, repelStrengthInitMax = 3.0;
    double attackRangeInitMin = 20.0, attackRangeInitMax = 200.0;
    double attackStrengthInitMin = 10.0, attackStrengthInitMax = 100.0;
    double avoidRangeInitMin = 30.0, avoidRangeInitMax = 300.0;
    double avoidStrengthInitMin = 10.0, avoidStrengthInitMax = 80.0;
    double maxSpeedInitMin = 30.0, maxSpeedInitMax = 100.0;
    double energyThresholdInitMin = 80.0, energyThresholdInitMax = 200.0;
    double mutationRateInitMin = 0.01, mutationRateInitMax = 0.2;

    // 新增：蓝细菌相关参数范围
    double cyanoPreferenceInitMin = 0.0,  cyanoPreferenceInitMax = 1.0;
    double cyanoDigestionInitMin  = 0.1,  cyanoDigestionInitMax  = 1.0;
    double maxAccelInitMin        = 1.0,  maxAccelInitMax        = 20.0;

    // ---- 变异裁剪范围（clamp）----
    double affinityClampMin = 1e-4;  // affinity 只有下限（之后重新归一化）
    double orbitRadiusClampMin = 10.0, orbitRadiusClampMax = 300.0;
    double absorbPreferenceClampMin = 0.1, absorbPreferenceClampMax = 3.0;
    double repelStrengthClampMin = 0.1, repelStrengthClampMax = 6.0;
    double attackRangeClampMin = 5.0, attackRangeClampMax = 400.0;
    double attackStrengthClampMin = 1.0, attackStrengthClampMax = 300.0;
    double avoidRangeClampMin = 10.0, avoidRangeClampMax = 600.0;
    double avoidStrengthClampMin = 1.0, avoidStrengthClampMax = 200.0;
    double maxSpeedClampMin = 5.0, maxSpeedClampMax = 300.0;
    double energyThresholdClampMin = 20.0, energyThresholdClampMax = 1000.0;
    double mutationRateClampMin = 0.001, mutationRateClampMax = 0.5;

    // 新增：蓝细菌相关参数 clamp
    double cyanoPreferenceClampMin = 0.0,  cyanoPreferenceClampMax = 1.0;
    double cyanoDigestionClampMin  = 0.05, cyanoDigestionClampMax  = 1.0;
    double maxAccelClampMin        = 0.5,  maxAccelClampMax        = 50.0;
};


// 核的可遗传参数，全部为浮点数。
// 类型感知参数按 BallType 枚举顺序索引（Shield, Worker, Scout）。
struct NucleusParams {
    std::array<double, BALL_TYPE_COUNT> affinity{};         // 吸引力权重（总和归一化为 1）
    std::array<double, BALL_TYPE_COUNT> orbitRadius{};      // 期望该类型小球环绕半径
    std::array<double, BALL_TYPE_COUNT> absorbPreference{}; // 吸收该类型小球时的能量倍率
    std::array<double, BALL_TYPE_COUNT> repelStrength{};    // 小球过于接近核时的排斥强度

    // 自身行为参数
    double attackRange = 100.0;
    double attackStrength = 50.0;
    double avoidRange = 150.0;
    double avoidStrength = 40.0;
    double maxSpeed = 60.0;
    double energyThreshold = 120.0;
    double mutationRate = 0.05;

    // 新增：蓝细菌相关遗传参数（顺序固定：cyanoPreference → cyanoDigestion → maxAccel）
    double cyanoPreference = 0.0;  // 0..1，捕食蓝细菌倾向；== 0.0 表示完全不吃（验收 #9）
    double cyanoDigestion  = 0.5;  // 0..1，消化效率（遗传，与机动性形成取舍）
    double maxAccel        = 6.0;  // 绝对最大加速度（世界单位/帧²），仅用于转化率公式，不改物理

    // 在给定范围内均匀随机初始化全部参数。
    void randomize(std::mt19937& rng, const ParamRanges& r, bool withCyano = false);
    // 以给定幅度对全部参数做随机扰动（亲和力随后重新归一化）。
    void mutate(std::mt19937& rng, double rate, const ParamRanges& r, bool withCyano = false);
    // 归一化亲和力，使总和为 1。
    void normalizeAffinity();
};
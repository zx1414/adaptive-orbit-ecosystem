// agentsim - app/RuntimeEnv.cpp
// R3-B：EnvChange{key,value} -> SimConfig 字段映射的实现（映射唯一权威）。
// 总账对 docs/PARAM-MAPPING.md：
//   genome 24 键 -> colony.genomeRanges（Init 收敛到 v + Clamp 扩到包含 v，见 genomeRange）
//   light 10 键 -> 7 键 sun.* / 半饱和 -> cyanoTuning / warmth+grid -> runtime.*
//   food 12 键 -> ball_* 7 键 -> ball.*；spawn_probability/cost/min/max -> cyanoTuning
//                 （ball_spawn_probability 是 CyanoParams.ballSpawnProbability 的 schema 拼写）；
//                 regrow_food 拒（Food 实体已删，废弃键）
//   pressure 9 键 -> basal_cost 与 respiration_base 同落 nucleus.respirationBase（键二义，
//                 EnvControl 压力轴用 basal_cost、schema 另有 respiration_base，语义同字段）；
//                 speed_cost_k -> nucleus.locomotionCost（applyConfig 同步 bp_ 立即生效）；
//                 mutation_coefficient -> colony.mutationCoefficient（下一胎变异生效）；
//                 water_cost_multiplier -> terrain.waterCostMultiplier（Terrain 构造期快照，
//                 本局不回灌 terrain_，下一局生效——Terrain 无参数热写口，见报告）；
//                 其余 4 键存 nucleus.*（R3-A 消费）
//   producer 4 / cyano 20 键 -> cyanoTuning.*（applyConfig 逐字段直通 CyanoField）
//   world 11 键 -> nuclei/cyano_nodes/sense_radius/max_fps/render_interval/autosave_interval/
//                 balls 接；width/height/seed/ticks 拒（世界几何/种子/总帧数不可热改）
//   ball 15 / nucleus 8 键 -> ball.* / nucleus.*（R3-A/R4 消费）；cyano_predation_radius
//                 -> nucleus.cyanoPredationRadius（applyConfig 同步 bp_ 立即生效）
//   另接：cyano_enabled（EnvControl cyano=on/off 产出的变更键）-> runtime.cyanoEnabled；
//         cyano_ball_spawn_probability / cyano_ball_spawn_cost（EnvControl food 轴档位与
//         细调的 R1-C 拼写，schema 无此键）作为同义别名映射到同一字段——别名若不接，
//         axis.food 换档产出的变更会全部落进 unwired（参数页"改了没反应"）。
//
// 映射表：一张静态 Entry[]（key -> 写入函数），一次构建、查表执行；
// 全部写入函数为无捕获 lambda（隐式转函数指针），无 113 个 if-else。
//
// 【语义决策：genome 键的 Init/Clamp 双写】任务卡要求"Init 和 Clamp 都设成同值"。
// GenomeRanges 每个基因有 4 个数（InitMin/InitMax/ClampMin/ClampMax），"同值"按
// 下述方式落地（注释即规格，报告同步）：
//   1) Init 区间收敛到点 [v, v]——此后出生的核该基因基准 = v（"运行时改出生范围"）；
//   2) Clamp 区间同步**扩到包含 v**（v 越出旧区间时扩边，区间内则不动）。
// 为什么 Clamp 扩宽而不是也收敛到 v：genomeMutate 的变异步长 =
// mutationRate * (clampMax - clampMin) * coeff——Clamp 收敛成点后步长恒 0，
// 该基因进化死亡。任务卡的立意是"改的是范围不是个体基因、Init/Clamp 一起改才
// 自洽"：自洽的硬约束是"出生点必须落在 Clamp 区间内"（否则 genomeClamp 把每个
// 新生儿拉回旧区间，Init 白改），扩宽恰好满足；收敛是超出立意的副作用，不采纳。
// 对已存在核零影响：基因只读不写（Genome.h 红线），变异系数/区间在下一胎生效。
//
// 确定性红线：本文件不消耗 RNG；值域卫生由 EnvControl（钳制）与 ParamSchema（校验）
// 负责，本模块只做非有限值防御（NaN/Inf 拒绝且不进 unwired——那是脏值不是未知键）。
#include "app/RuntimeEnv.h"

#include <cstddef>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace agentsim::app {
namespace {

// ---- 写入函数签名：拿到 SimConfig 与目标值，负责一切类型收窄/钳制 ----
using Writer = void (*)(SimConfig&, double);

struct Entry {
    const char* key;
    Writer write;
};

// ---- genome 组共用：Init 收敛 + Clamp 扩宽（语义见文件头）----
void genomeRange(SimConfig& cfg, double v,
                 double GenomeRanges::*initLo, double GenomeRanges::*initHi,
                 double GenomeRanges::*clampLo, double GenomeRanges::*clampHi) {
    GenomeRanges& r = cfg.colony.genomeRanges;
    r.*initLo = v;
    r.*initHi = v;
    if (v < r.*clampLo) r.*clampLo = v;
    if (v > r.*clampHi) r.*clampHi = v;
}

// ---- 非负整数族写入器（size_t / int；负值按 0 处理，四舍五入）----
std::size_t toSizeT(double v) {
    if (!(v > 0.0)) return 0;  // NaN/负/0 -> 0
    const double r = v + 0.5;
    return r >= 9.2e18 ? 9200000000000000000ull : static_cast<std::size_t>(r);
}
int toNonNegInt(double v) {
    if (!(v > 0.0)) return 0;
    const double r = v + 0.5;
    return r >= 2.1e9 ? 2100000000 : static_cast<int>(r);
}

// ---- 映射表（按 ParamSchema 分组顺序排列；count 由 wiredKeyCount 断言）----
// 返回 const vector 引用而不是裸数组指针：sizeof(指针) = 8 的坑会让
// entryTableCount() 恒等于 1（实测全部键 miss 落进 unwired 的回归）。
const std::vector<Entry>& entryTable() {
    static const std::vector<Entry> table = [] {
        static const Entry rows[] = {
        // ===== world（7 接；width/height/seed/ticks 在拒绝名单）=====
        {"nuclei", [](SimConfig& c, double v) { c.colony.nuclei = toSizeT(v); }},
        {"balls", [](SimConfig& c, double v) { c.runtime.initialFreeBalls = toNonNegInt(v); }},
        {"cyano_nodes", [](SimConfig& c, double v) { c.cyanoNodeCount = toSizeT(v); }},
        {"sense_radius", [](SimConfig& c, double v) { c.policy.senseRadius = v; }},
        {"max_fps", [](SimConfig& c, double v) { c.runtime.maxFps = v; }},
        {"render_interval", [](SimConfig& c, double v) { c.runtime.renderInterval = toNonNegInt(v); }},
        {"autosave_interval", [](SimConfig& c, double v) { c.runtime.autosaveInterval = toNonNegInt(v); }},

        // ===== light（10 全接）=====
        {"sun_base_level", [](SimConfig& c, double v) { c.sun.baseLevel = v; }},
        {"sun_day_amplitude", [](SimConfig& c, double v) { c.sun.dayAmplitude = v; }},
        {"sun_day_period_frames", [](SimConfig& c, double v) { c.sun.dayPeriodFrames = v; }},
        {"sun_vertical_gradient", [](SimConfig& c, double v) { c.sun.verticalGradient = v; }},
        {"sun_noise_amplitude", [](SimConfig& c, double v) { c.sun.noiseAmplitude = v; }},
        {"sun_noise_frequency", [](SimConfig& c, double v) { c.sun.noiseFrequency = v; }},
        {"sun_noise_speed", [](SimConfig& c, double v) { c.sun.noiseSpeed = v; }},
        {"cyano_light_half_saturation", [](SimConfig& c, double v) { c.cyanoTuning.lightHalfSaturation = v; }},
        {"light_warmth", [](SimConfig& c, double v) { c.runtime.lightWarmth = v; }},
        {"light_sample_grid", [](SimConfig& c, double v) { c.runtime.lightSampleGrid = toNonNegInt(v); }},

        // ===== food（11 接；regrow_food 拒绝）=====
        {"ball_energy", [](SimConfig& c, double v) { c.ball.energy = v; }},
        {"ball_radius", [](SimConfig& c, double v) { c.ball.radius = v; }},
        {"ball_vel_range", [](SimConfig& c, double v) { c.ball.velRange = v; }},
        {"ball_initial_energy", [](SimConfig& c, double v) { c.ball.initialEnergy = v; }},
        {"ball_energy_cap", [](SimConfig& c, double v) { c.ball.energyCap = v; }},
        {"ball_spawn_probability", [](SimConfig& c, double v) { c.cyanoTuning.ballSpawnProbability = v; }},
        {"ball_spawn_cost", [](SimConfig& c, double v) { c.cyanoTuning.ballSpawnCost = v; }},
        {"ball_spawn_min_dist", [](SimConfig& c, double v) { c.cyanoTuning.ballSpawnMinDist = v; }},
        {"ball_spawn_max_dist", [](SimConfig& c, double v) { c.cyanoTuning.ballSpawnMaxDist = v; }},
        {"ball_loss_cost", [](SimConfig& c, double v) { c.ball.lossCost = v; }},
        {"detach_radius", [](SimConfig& c, double v) { c.ball.detachRadius = v; }},

        // ===== pressure（9 全接）=====
        {"basal_cost", [](SimConfig& c, double v) { c.nucleus.respirationBase = v; }},
        {"respiration_base", [](SimConfig& c, double v) { c.nucleus.respirationBase = v; }},  // 与 basal_cost 同字段（映射表去重决策）
        {"speed_cost_k", [](SimConfig& c, double v) { c.nucleus.locomotionCost = v; }},
    {"migration_bonus", [](SimConfig& c, double v) { c.nucleus.migrationBonus = v; }},   // R9-A
    {"migration_radius", [](SimConfig& c, double v) { c.nucleus.migrationRadius = v; }}, // R9-A
        {"speed_cost_exponent", [](SimConfig& c, double v) { c.nucleus.speedCostExponent = v; }},
        {"reproduction_cost_ratio", [](SimConfig& c, double v) { c.nucleus.reproductionCostRatio = v; }},
        {"child_energy_ratio", [](SimConfig& c, double v) { c.nucleus.childEnergyRatio = v; }},
        {"reproduction_offset", [](SimConfig& c, double v) { c.nucleus.reproductionOffset = v; }},
        {"mutation_coefficient", [](SimConfig& c, double v) { c.colony.mutationCoefficient = v; }},
        {"water_cost_multiplier", [](SimConfig& c, double v) { c.terrain.waterCostMultiplier = v; }},

        // ===== producer（4 全接）=====
        {"cyano_photosynthesis_rate", [](SimConfig& c, double v) { c.cyanoTuning.photosynthesisRate = v; }},
        {"cyano_metabolism_rate", [](SimConfig& c, double v) { c.cyanoTuning.metabolismRate = v; }},
        {"cyano_reproduction_probability", [](SimConfig& c, double v) { c.cyanoTuning.reproductionProbability = v; }},
        {"cyano_reproduction_offset", [](SimConfig& c, double v) { c.cyanoTuning.reproductionOffset = v; }},

        // ===== cyano（20 全接）=====
        {"cyano_colonies", [](SimConfig& c, double v) { c.runtime.cyanoColonies = toNonNegInt(v); }},
        {"cyano_initial_energy", [](SimConfig& c, double v) { c.runtime.cyanoInitialEnergy = v; }},
        {"cyano_energy_cap", [](SimConfig& c, double v) { c.cyanoTuning.energyCap = v; }},
        {"cyano_link_distance", [](SimConfig& c, double v) { c.cyanoTuning.linkRadius = v; }},
        {"cyano_max_links", [](SimConfig& c, double v) { c.cyanoTuning.linkMaxNeighbors = toNonNegInt(v); }},
        {"cyano_link_check_interval", [](SimConfig& c, double v) { c.cyanoTuning.linkCheckInterval = toNonNegInt(v); }},
        {"cyano_transfer_rate", [](SimConfig& c, double v) { c.cyanoTuning.linkTransferRate = v; }},
        {"cyano_transfer_deadband", [](SimConfig& c, double v) { c.cyanoTuning.linkDeadband = v; }},
        {"cyano_min_node_separation", [](SimConfig& c, double v) { c.cyanoTuning.minSeparation = v; }},
        {"cyano_predation_radius", [](SimConfig& c, double v) { c.nucleus.cyanoPredationRadius = v; }},
        {"cyano_conversion_mode", [](SimConfig& c, double v) { c.cyanoTuning.conversionMode = toNonNegInt(v); }},
        {"cyano_conversion_rate", [](SimConfig& c, double v) { c.cyanoTuning.conversionRate = v; }},
        {"cyano_conversion_exponent", [](SimConfig& c, double v) { c.cyanoTuning.conversionExponent = v; }},
        {"cyano_conversion_speed_ref", [](SimConfig& c, double v) { c.cyanoTuning.conversionSpeedRef = v; }},
        {"cyano_conversion_accel_ref", [](SimConfig& c, double v) { c.cyanoTuning.conversionAccelRef = v; }},
        {"cyano_conversion_w_speed", [](SimConfig& c, double v) { c.cyanoTuning.conversionWSpeed = v; }},
        {"cyano_conversion_w_accel", [](SimConfig& c, double v) { c.cyanoTuning.conversionWAccel = v; }},
        {"cyano_conversion_min_rate", [](SimConfig& c, double v) { c.cyanoTuning.conversionMinRate = v; }},
        {"cyano_conversion_max_rate", [](SimConfig& c, double v) { c.cyanoTuning.conversionMaxRate = v; }},
        {"cyano_hue_jitter", [](SimConfig& c, double v) { c.cyanoTuning.hueJitterSigma = v; }},

        // ===== genome（24 全接；GenomeRanges 对同族三类型共享同一区间，
        // 三个 *_resource/_scout/_shield 键写同一区间——schema 的 min/max/default
        // 三键也完全相同，语义即"同族同区间"，见 Genome.h 字段注释）=====
        {"affinity_resource", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::affinityInitMin, &GenomeRanges::affinityInitMax,
                        &GenomeRanges::affinityClampMin, &GenomeRanges::affinityClampMax); }},
        {"affinity_scout", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::affinityInitMin, &GenomeRanges::affinityInitMax,
                        &GenomeRanges::affinityClampMin, &GenomeRanges::affinityClampMax); }},
        {"affinity_shield", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::affinityInitMin, &GenomeRanges::affinityInitMax,
                        &GenomeRanges::affinityClampMin, &GenomeRanges::affinityClampMax); }},
        {"orbit_radius_resource", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::orbitRadiusInitMin, &GenomeRanges::orbitRadiusInitMax,
                        &GenomeRanges::orbitRadiusClampMin, &GenomeRanges::orbitRadiusClampMax); }},
        {"orbit_radius_scout", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::orbitRadiusInitMin, &GenomeRanges::orbitRadiusInitMax,
                        &GenomeRanges::orbitRadiusClampMin, &GenomeRanges::orbitRadiusClampMax); }},
        {"orbit_radius_shield", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::orbitRadiusInitMin, &GenomeRanges::orbitRadiusInitMax,
                        &GenomeRanges::orbitRadiusClampMin, &GenomeRanges::orbitRadiusClampMax); }},
        {"absorb_preference_resource", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::absorbPreferenceInitMin, &GenomeRanges::absorbPreferenceInitMax,
                        &GenomeRanges::absorbPreferenceClampMin, &GenomeRanges::absorbPreferenceClampMax); }},
        {"absorb_preference_scout", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::absorbPreferenceInitMin, &GenomeRanges::absorbPreferenceInitMax,
                        &GenomeRanges::absorbPreferenceClampMin, &GenomeRanges::absorbPreferenceClampMax); }},
        {"absorb_preference_shield", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::absorbPreferenceInitMin, &GenomeRanges::absorbPreferenceInitMax,
                        &GenomeRanges::absorbPreferenceClampMin, &GenomeRanges::absorbPreferenceClampMax); }},
        {"repel_strength_resource", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::repelStrengthInitMin, &GenomeRanges::repelStrengthInitMax,
                        &GenomeRanges::repelStrengthClampMin, &GenomeRanges::repelStrengthClampMax); }},
        {"repel_strength_scout", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::repelStrengthInitMin, &GenomeRanges::repelStrengthInitMax,
                        &GenomeRanges::repelStrengthClampMin, &GenomeRanges::repelStrengthClampMax); }},
        {"repel_strength_shield", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::repelStrengthInitMin, &GenomeRanges::repelStrengthInitMax,
                        &GenomeRanges::repelStrengthClampMin, &GenomeRanges::repelStrengthClampMax); }},
        {"nucleus_sight_radius", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::nucleusSightRadiusInitMin, &GenomeRanges::nucleusSightRadiusInitMax,
                        &GenomeRanges::nucleusSightRadiusClampMin, &GenomeRanges::nucleusSightRadiusClampMax); }},
        {"nucleus_personal_space", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::nucleusPersonalSpaceInitMin, &GenomeRanges::nucleusPersonalSpaceInitMax,
                        &GenomeRanges::nucleusPersonalSpaceClampMin, &GenomeRanges::nucleusPersonalSpaceClampMax); }},
        {"nucleus_repel_strength", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::nucleusRepelStrengthInitMin, &GenomeRanges::nucleusRepelStrengthInitMax,
                        &GenomeRanges::nucleusRepelStrengthClampMin, &GenomeRanges::nucleusRepelStrengthClampMax); }},
        {"nucleus_cohesion", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::nucleusCohesionInitMin, &GenomeRanges::nucleusCohesionInitMax,
                        &GenomeRanges::nucleusCohesionClampMin, &GenomeRanges::nucleusCohesionClampMax); }},
        {"force_range", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::forceRangeInitMin, &GenomeRanges::forceRangeInitMax,
                        &GenomeRanges::forceRangeClampMin, &GenomeRanges::forceRangeClampMax); }},
        {"force_cost_k", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::forceCostKInitMin, &GenomeRanges::forceCostKInitMax,
                        &GenomeRanges::forceCostKClampMin, &GenomeRanges::forceCostKClampMax); }},
        {"max_speed", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::maxSpeedInitMin, &GenomeRanges::maxSpeedInitMax,
                        &GenomeRanges::maxSpeedClampMin, &GenomeRanges::maxSpeedClampMax); }},
        {"max_accel", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::maxAccelInitMin, &GenomeRanges::maxAccelInitMax,
                        &GenomeRanges::maxAccelClampMin, &GenomeRanges::maxAccelClampMax); }},
        {"energy_threshold", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::energyThresholdInitMin, &GenomeRanges::energyThresholdInitMax,
                        &GenomeRanges::energyThresholdClampMin, &GenomeRanges::energyThresholdClampMax); }},
        {"mutation_rate", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::mutationRateInitMin, &GenomeRanges::mutationRateInitMax,
                        &GenomeRanges::mutationRateClampMin, &GenomeRanges::mutationRateClampMax); }},
        {"cyano_preference", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::cyanoPreferenceInitMin, &GenomeRanges::cyanoPreferenceInitMax,
                        &GenomeRanges::cyanoPreferenceClampMin, &GenomeRanges::cyanoPreferenceClampMax); }},
        {"cyano_digestion", [](SimConfig& c, double v) {
            genomeRange(c, v, &GenomeRanges::cyanoDigestionInitMin, &GenomeRanges::cyanoDigestionInitMax,
                        &GenomeRanges::cyanoDigestionClampMin, &GenomeRanges::cyanoDigestionClampMax); }},

        // ===== ball（15 全接；R4 球力场消费）=====
        {"ball_damping", [](SimConfig& c, double v) { c.ball.damping = v; }},
        {"radial_k", [](SimConfig& c, double v) { c.ball.radialK = v; }},
        {"tangential_k", [](SimConfig& c, double v) { c.ball.tangentialK = v; }},
        {"near_repel_radius", [](SimConfig& c, double v) { c.ball.nearRepelRadius = v; }},
        {"ball_ball_repel_radius", [](SimConfig& c, double v) { c.ball.ballRepelRadius = v; }},
        {"ball_ball_repel_k", [](SimConfig& c, double v) { c.ball.ballRepelK = v; }},
        {"same_owner_attract_radius", [](SimConfig& c, double v) { c.ball.sameOwnerAttractRadius = v; }},
        {"same_owner_attract_k", [](SimConfig& c, double v) { c.ball.sameOwnerAttractK = v; }},
        {"ball_max_speed_shield", [](SimConfig& c, double v) { c.ball.maxSpeedShield = v; }},
        {"ball_max_speed_resource", [](SimConfig& c, double v) { c.ball.maxSpeedResource = v; }},
        {"ball_max_speed_scout", [](SimConfig& c, double v) { c.ball.maxSpeedScout = v; }},
        {"follow_coupling", [](SimConfig& c, double v) { c.ball.followCoupling = v; }},
        {"follow_boost", [](SimConfig& c, double v) { c.ball.followBoost = v; }},
        {"bounce_restitution", [](SimConfig& c, double v) { c.ball.bounceRestitution = v; }},
        {"wrap_boundary", [](SimConfig& c, double v) { c.ball.wrapBoundary = v >= 0.5; }},

        // ===== nucleus（8 全接；min_separation/repel_k/forage_*/wander_* R3-A 消费）=====
        {"nucleus_damping", [](SimConfig& c, double v) { c.nucleus.damping = v; }},
        {"nucleus_min_separation", [](SimConfig& c, double v) { c.nucleus.minSeparation = v; }},
        {"nucleus_repel_k", [](SimConfig& c, double v) { c.nucleus.repelK = v; }},
        {"forage_k", [](SimConfig& c, double v) { c.nucleus.forageK = v; }},
        {"forage_radius", [](SimConfig& c, double v) { c.nucleus.forageRadius = v; }},
        {"wander_k", [](SimConfig& c, double v) { c.nucleus.wanderK = v; }},
        {"wander_turn_rate", [](SimConfig& c, double v) { c.nucleus.wanderTurnRate = v; }},
        {"nucleus_init_energy_ratio", [](SimConfig& c, double v) { c.nucleus.nucleusInitEnergyRatio = v; }},

        // ===== 运行面专用键 =====
        {"cyano_enabled", [](SimConfig& c, double v) { c.runtime.cyanoEnabled = v >= 0.5; }},

        // ===== EnvControl food 轴/细调的 R1-C 拼写别名（schema 无此键；
        // 不接的话 axis.food 换档与该细调杆会整体落进 unwired）=====
        {"cyano_ball_spawn_probability", [](SimConfig& c, double v) { c.cyanoTuning.ballSpawnProbability = v; }},
        {"cyano_ball_spawn_cost", [](SimConfig& c, double v) { c.cyanoTuning.ballSpawnCost = v; }},
        };
        return std::vector<Entry>(rows, rows + sizeof(rows) / sizeof(rows[0]));
    }();
    return table;
}

std::size_t entryTableCount() {
    return entryTable().size();
}

// ---- 拒绝名单：设计意图内的"不可热改/废弃"，不算未接线（不告警、不进 unwired）----
//   width/height：世界几何，改了等于重开局（所有已生成实体坐标/地形/蓝细菌播种
//                 都绑定旧尺寸）；
//   seed：        同 seed 双跑是全项目确定性门禁的根基，运行中换种子 = 换世界；
//   ticks：       CLI 批跑的总帧数契约（serve 模式不消费它），运行中改无意义；
//   regrow_food： 废弃键——Food 实体整链路已删（V2-SPEC-AMEND-1 / R1-B），永不接线。
const char* const kRejectKeys[] = {"width", "height", "seed", "ticks", "regrow_food"};

// ---- 未接线诊断（进程级；单线程访问约定与 HttpServer 一致）----
struct Unwired {
    std::vector<std::string> keys;      // 首次出现顺序，去重
    std::vector<std::string> warned;    // 已打过 stderr 警告的 key（每 key 恰一次）
};
Unwired& unwired() {
    static Unwired u;
    return u;
}

const Entry* findEntry(const std::string& key) {
    for (const Entry& e : entryTable()) {
        if (key == e.key) return &e;
    }
    return nullptr;
}

bool isRejectKey(const std::string& key) {
    for (const char* k : kRejectKeys) {
        if (key == k) return true;
    }
    return false;
}

} // namespace

int RuntimeEnv::applyAll(SimConfig& cfg, const std::vector<EnvChange>& changes) {
    int ok = 0;
    for (const EnvChange& ch : changes) {
        if (applyOne(cfg, ch.key, ch.value)) ++ok;
        // 失败条目不中断批次："一批里混有合法/非法 key，合法的仍生效"是验收用例。
    }
    return ok;
}

bool RuntimeEnv::applyOne(SimConfig& cfg, const std::string& key, double value) {
    // 值卫生：NaN/Inf 是脏值不是未知键——拒绝但不进 unwired、不告警。
    if (!std::isfinite(value)) return false;

    if (isRejectKey(key)) return false;  // 设计意图内拒绝：静默（注释见 kRejectKeys）

    const Entry* e = findEntry(key);
    if (e != nullptr) {
        e->write(cfg, value);
        return true;
    }

    // 未知 key：stderr 警告一次 + 计入 unwired（去重）。
    Unwired& u = unwired();
    bool seen = false;
    for (const std::string& k : u.keys) {
        if (k == key) { seen = true; break; }
    }
    if (!seen) u.keys.push_back(key);
    bool warnedAlready = false;
    for (const std::string& k : u.warned) {
        if (k == key) { warnedAlready = true; break; }
    }
    if (!warnedAlready) {
        u.warned.push_back(key);
        std::fprintf(stderr, "warning: RuntimeEnv: unknown parameter key \"%s\" (value %g) - not wired\n",
                     key.c_str(), value);
    }
    return false;
}

std::vector<std::string> RuntimeEnv::unwiredKeys() {
    return unwired().keys;  // 拷贝出站：调用方持有快照，内部集合继续累积
}

void RuntimeEnv::resetUnwired() {
    Unwired& u = unwired();
    u.keys.clear();
    u.warned.clear();  // 警告随诊断集一起复位（单测隔离：同名 key 可再次告警）
}

int RuntimeEnv::wiredKeyCount() {
    // 表行数 = schema 已接键 + cyano_enabled + 2 个 EnvControl 拼写别名；
    // 拒绝名单 5 键（width/height/seed/ticks/regrow_food）不算接线。
    return static_cast<int>(entryTableCount());
}

} // namespace agentsim::app

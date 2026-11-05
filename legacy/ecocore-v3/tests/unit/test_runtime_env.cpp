// agentsim - tests/unit/test_runtime_env.cpp
// R3-B：RuntimeEnv（EnvChange -> SimConfig 映射）单测。
// 与 test_envcontrol.cpp 同理：tools/build.ps1 的单测目标不链 src/app，
// 直接 #include 翻译单元。约定（test_http.cpp 注释）：每个 app/*.cpp 只允许被
// 一个测试翻译单元并源——EnvControl.cpp / ParamSchema.cpp 已归 test_envcontrol.cpp，
// 本文件只并 RuntimeEnv.cpp（其头文件正常 include）。
#include "../src/app/RuntimeEnv.cpp"

#include <cmath>
#include <string>
#include <vector>

#include "sim/Simulation.h"
#include "test_framework.h"

namespace {

using agentsim::GenomeRanges;
using agentsim::SimConfig;
using agentsim::app::EnvChange;
using agentsim::app::RuntimeEnv;

// 断言"值已写进 cfg"的快捷构造。
EnvChange ch(const char* key, double value) {
    EnvChange c;
    c.key = key;
    c.value = value;
    return c;
}

// 每个 TEST 独立 SimConfig + 干净的 unwired 诊断集（进程级状态的测试隔离）。
struct Fresh {
    SimConfig cfg;
    Fresh() { RuntimeEnv::resetUnwired(); }
    ~Fresh() { RuntimeEnv::resetUnwired(); }
};

// 默认 GenomeRanges 的字节快照（检测"确实动了 ranges 而不是没动"）。
GenomeRanges snapRanges(const SimConfig& c) { return c.colony.genomeRanges; }

} // namespace

// ---------- P0：genome 24 键 -> GenomeRanges（Init/Clamp 双写） ----------

TEST(runtime_env_genome_keys_change_ranges) {
    Fresh f;
    const GenomeRanges before = snapRanges(f.cfg);
    CHECK(RuntimeEnv::applyOne(f.cfg, "max_speed", 111.0));
    const GenomeRanges& r = f.cfg.colony.genomeRanges;
    // Init 区间收敛到点 [v, v]（出生基准 = v）。
    CHECK_NEAR(r.maxSpeedInitMin, 111.0, 1e-12);
    CHECK_NEAR(r.maxSpeedInitMax, 111.0, 1e-12);
    // Clamp 扩宽到包含 v（默认 5..300 已含 111，故不动）。
    CHECK_NEAR(r.maxSpeedClampMin, before.maxSpeedClampMin, 1e-12);
    CHECK_NEAR(r.maxSpeedClampMax, before.maxSpeedClampMax, 1e-12);
}

TEST(runtime_env_genome_key_outside_clamp_widens_clamp) {
    Fresh f;
    CHECK(RuntimeEnv::applyOne(f.cfg, "max_speed", 500.0));
    const GenomeRanges& r = f.cfg.colony.genomeRanges;
    CHECK_NEAR(r.maxSpeedInitMin, 500.0, 1e-12);
    CHECK_NEAR(r.maxSpeedInitMax, 500.0, 1e-12);
    CHECK_NEAR(r.maxSpeedClampMax, 500.0, 1e-12);  // 旧 300 < 500：扩到 500
    CHECK_NEAR(r.maxSpeedClampMin, 5.0, 1e-12);    // 下限不动
}

TEST(runtime_env_genome_below_clamp_min_widens_down) {
    Fresh f;
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_digestion", 0.01));
    const GenomeRanges& r = f.cfg.colony.genomeRanges;
    CHECK_NEAR(r.cyanoDigestionInitMin, 0.01, 1e-12);
    CHECK_NEAR(r.cyanoDigestionInitMax, 0.01, 1e-12);
    CHECK_NEAR(r.cyanoDigestionClampMin, 0.01, 1e-12);  // 旧 0.05 > 0.01：扩下限
    CHECK_NEAR(r.cyanoDigestionClampMax, 1.0, 1e-12);
}

TEST(runtime_env_all_24_genome_keys_wired) {
    // 24 个 genome 键逐一应用后 Init 区间必须变化（防"表里写漏一行"）。
    static const char* const kKeys[] = {
        "affinity_resource", "affinity_scout", "affinity_shield",
        "orbit_radius_resource", "orbit_radius_scout", "orbit_radius_shield",
        "absorb_preference_resource", "absorb_preference_scout",
        "absorb_preference_shield",
        "repel_strength_resource", "repel_strength_scout", "repel_strength_shield",
        "nucleus_sight_radius", "nucleus_personal_space", "nucleus_repel_strength",
        "nucleus_cohesion", "force_range", "force_cost_k",
        "max_speed", "max_accel", "energy_threshold", "mutation_rate",
        "cyano_preference", "cyano_digestion",
    };
    for (const char* key : kKeys) {
        Fresh f;
        const GenomeRanges before = snapRanges(f.cfg);
        CHECK(RuntimeEnv::applyOne(f.cfg, key, 0.7777));
        const GenomeRanges& r = f.cfg.colony.genomeRanges;
        // 每键至少 Init 区间收敛（0.7777 可能恰在某 Init 区间中点，比较前后）。
        const bool initChanged =
            r.maxSpeedInitMin != before.maxSpeedInitMin ||
            r.affinityInitMin != before.affinityInitMin ||
            r.orbitRadiusInitMin != before.orbitRadiusInitMin ||
            r.absorbPreferenceInitMin != before.absorbPreferenceInitMin ||
            r.repelStrengthInitMin != before.repelStrengthInitMin ||
            r.maxAccelInitMin != before.maxAccelInitMin ||
            r.energyThresholdInitMin != before.energyThresholdInitMin ||
            r.mutationRateInitMin != before.mutationRateInitMin ||
            r.cyanoPreferenceInitMin != before.cyanoPreferenceInitMin ||
            r.cyanoDigestionInitMin != before.cyanoDigestionInitMin ||
            r.nucleusSightRadiusInitMin != before.nucleusSightRadiusInitMin ||
            r.nucleusPersonalSpaceInitMin != before.nucleusPersonalSpaceInitMin ||
            r.nucleusRepelStrengthInitMin != before.nucleusRepelStrengthInitMin ||
            r.nucleusCohesionInitMin != before.nucleusCohesionInitMin ||
            r.forceRangeInitMin != before.forceRangeInitMin ||
            r.forceCostKInitMin != before.forceCostKInitMin ||
            r.affinityInitMax != before.affinityInitMax ||
            r.orbitRadiusInitMax != before.orbitRadiusInitMax ||
            r.absorbPreferenceInitMax != before.absorbPreferenceInitMax ||
            r.repelStrengthInitMax != before.repelStrengthInitMax ||
            r.maxSpeedInitMax != before.maxSpeedInitMax ||
            r.maxAccelInitMax != before.maxAccelInitMax ||
            r.energyThresholdInitMax != before.energyThresholdInitMax ||
            r.mutationRateInitMax != before.mutationRateInitMax ||
            r.cyanoPreferenceInitMax != before.cyanoPreferenceInitMax ||
            r.cyanoDigestionInitMax != before.cyanoDigestionInitMax ||
            r.nucleusSightRadiusInitMax != before.nucleusSightRadiusInitMax ||
            r.nucleusPersonalSpaceInitMax != before.nucleusPersonalSpaceInitMax ||
            r.nucleusRepelStrengthInitMax != before.nucleusRepelStrengthInitMax ||
            r.nucleusCohesionInitMax != before.nucleusCohesionInitMax ||
            r.forceRangeInitMax != before.forceRangeInitMax ||
            r.forceCostKInitMax != before.forceCostKInitMax ||
            r.affinityClampMin != before.affinityClampMin ||
            r.orbitRadiusClampMin != before.orbitRadiusClampMin ||
            r.absorbPreferenceClampMin != before.absorbPreferenceClampMin ||
            r.repelStrengthClampMin != before.repelStrengthClampMin ||
            r.maxSpeedClampMin != before.maxSpeedClampMin ||
            r.maxAccelClampMin != before.maxAccelClampMin ||
            r.energyThresholdClampMin != before.energyThresholdClampMin ||
            r.mutationRateClampMin != before.mutationRateClampMin ||
            r.cyanoPreferenceClampMin != before.cyanoPreferenceClampMin ||
            r.cyanoDigestionClampMin != before.cyanoDigestionClampMin ||
            r.nucleusSightRadiusClampMin != before.nucleusSightRadiusClampMin ||
            r.nucleusPersonalSpaceClampMin != before.nucleusPersonalSpaceClampMin ||
            r.nucleusRepelStrengthClampMin != before.nucleusRepelStrengthClampMin ||
            r.nucleusCohesionClampMin != before.nucleusCohesionClampMin ||
            r.forceRangeClampMin != before.forceRangeClampMin ||
            r.forceCostKClampMin != before.forceCostKClampMin ||
            r.affinityClampMax != before.affinityClampMax ||
            r.orbitRadiusClampMax != before.orbitRadiusClampMax ||
            r.absorbPreferenceClampMax != before.absorbPreferenceClampMax ||
            r.repelStrengthClampMax != before.repelStrengthClampMax ||
            r.maxSpeedClampMax != before.maxSpeedClampMax ||
            r.maxAccelClampMax != before.maxAccelClampMax ||
            r.energyThresholdClampMax != before.energyThresholdClampMax ||
            r.mutationRateClampMax != before.mutationRateClampMax ||
            r.cyanoPreferenceClampMax != before.cyanoPreferenceClampMax ||
            r.cyanoDigestionClampMax != before.cyanoDigestionClampMax ||
            r.nucleusSightRadiusClampMax != before.nucleusSightRadiusClampMax ||
            r.nucleusPersonalSpaceClampMax != before.nucleusPersonalSpaceClampMax ||
            r.nucleusRepelStrengthClampMax != before.nucleusRepelStrengthClampMax ||
            r.nucleusCohesionClampMax != before.nucleusCohesionClampMax ||
            r.forceRangeClampMax != before.forceRangeClampMax ||
            r.forceCostKClampMax != before.forceCostKClampMax;
        CHECK(initChanged);
        if (!initChanged) {
            std::printf("    (key %s did not change ranges)\n", key);
        }
    }
}

// ---------- P0：light 组 -> SunConfig ----------

TEST(runtime_env_light_keys_write_sun) {
    Fresh f;
    CHECK(RuntimeEnv::applyOne(f.cfg, "sun_base_level", 1.35));
    CHECK_NEAR(f.cfg.sun.baseLevel, 1.35, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "sun_day_amplitude", 0.7));
    CHECK_NEAR(f.cfg.sun.dayAmplitude, 0.7, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "sun_day_period_frames", 500.0));
    CHECK_NEAR(f.cfg.sun.dayPeriodFrames, 500.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "sun_vertical_gradient", 0.25));
    CHECK_NEAR(f.cfg.sun.verticalGradient, 0.25, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "sun_noise_amplitude", 0.3));
    CHECK_NEAR(f.cfg.sun.noiseAmplitude, 0.3, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "sun_noise_frequency", 2.5));
    CHECK_NEAR(f.cfg.sun.noiseFrequency, 2.5, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "sun_noise_speed", 0.75));
    CHECK_NEAR(f.cfg.sun.noiseSpeed, 0.75, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_light_half_saturation", 0.9));
    CHECK_NEAR(f.cfg.cyanoTuning.lightHalfSaturation, 0.9, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "light_warmth", 0.8));
    CHECK_NEAR(f.cfg.runtime.lightWarmth, 0.8, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "light_sample_grid", 16));
    CHECK(f.cfg.runtime.lightSampleGrid == 16);
}

// ---------- P0：cyano / producer 组 -> CyanoTuning ----------

TEST(runtime_env_cyano_and_producer_keys_write_tuning) {
    Fresh f;
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_photosynthesis_rate", 2.5));
    CHECK_NEAR(f.cfg.cyanoTuning.photosynthesisRate, 2.5, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_metabolism_rate", 0.03));
    CHECK_NEAR(f.cfg.cyanoTuning.metabolismRate, 0.03, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_reproduction_probability", 0.02));
    CHECK_NEAR(f.cfg.cyanoTuning.reproductionProbability, 0.02, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_reproduction_offset", 200.0));
    CHECK_NEAR(f.cfg.cyanoTuning.reproductionOffset, 200.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_energy_cap", 150.0));
    CHECK_NEAR(f.cfg.cyanoTuning.energyCap, 150.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_link_distance", 80.0));
    CHECK_NEAR(f.cfg.cyanoTuning.linkRadius, 80.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_max_links", 5));
    CHECK(f.cfg.cyanoTuning.linkMaxNeighbors == 5);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_link_check_interval", 4));
    CHECK(f.cfg.cyanoTuning.linkCheckInterval == 4);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_transfer_rate", 0.2));
    CHECK_NEAR(f.cfg.cyanoTuning.linkTransferRate, 0.2, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_transfer_deadband", 2.0));
    CHECK_NEAR(f.cfg.cyanoTuning.linkDeadband, 2.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_min_node_separation", 15.0));
    CHECK_NEAR(f.cfg.cyanoTuning.minSeparation, 15.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_predation_radius", 20.0));
    CHECK_NEAR(f.cfg.nucleus.cyanoPredationRadius, 20.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_conversion_mode", 2));
    CHECK(f.cfg.cyanoTuning.conversionMode == 2);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_conversion_rate", 0.8));
    CHECK_NEAR(f.cfg.cyanoTuning.conversionRate, 0.8, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_conversion_exponent", 1.5));
    CHECK_NEAR(f.cfg.cyanoTuning.conversionExponent, 1.5, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_conversion_speed_ref", 50.0));
    CHECK_NEAR(f.cfg.cyanoTuning.conversionSpeedRef, 50.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_conversion_accel_ref", 70.0));
    CHECK_NEAR(f.cfg.cyanoTuning.conversionAccelRef, 70.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_conversion_w_speed", 1.2));
    CHECK_NEAR(f.cfg.cyanoTuning.conversionWSpeed, 1.2, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_conversion_w_accel", 1.3));
    CHECK_NEAR(f.cfg.cyanoTuning.conversionWAccel, 1.3, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_conversion_min_rate", 0.08));
    CHECK_NEAR(f.cfg.cyanoTuning.conversionMinRate, 0.08, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_conversion_max_rate", 1.2));
    CHECK_NEAR(f.cfg.cyanoTuning.conversionMaxRate, 1.2, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_hue_jitter", 12.0));
    CHECK_NEAR(f.cfg.cyanoTuning.hueJitterSigma, 12.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_colonies", 6));
    CHECK(f.cfg.runtime.cyanoColonies == 6);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_initial_energy", 50.0));
    CHECK_NEAR(f.cfg.runtime.cyanoInitialEnergy, 50.0, 1e-12);
}

// ---------- P1：pressure / nucleus / ball / food 组 ----------

TEST(runtime_env_pressure_keys_wired) {
    Fresh f;
    CHECK(RuntimeEnv::applyOne(f.cfg, "basal_cost", 0.02));
    CHECK_NEAR(f.cfg.nucleus.respirationBase, 0.02, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "respiration_base", 0.03));
    CHECK_NEAR(f.cfg.nucleus.respirationBase, 0.03, 1e-12);  // 同字段去重
    CHECK(RuntimeEnv::applyOne(f.cfg, "speed_cost_k", 0.001));
    CHECK_NEAR(f.cfg.nucleus.locomotionCost, 0.001, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "speed_cost_exponent", 2.0));
    CHECK_NEAR(f.cfg.nucleus.speedCostExponent, 2.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "reproduction_cost_ratio", 0.45));
    CHECK_NEAR(f.cfg.nucleus.reproductionCostRatio, 0.45, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "child_energy_ratio", 0.6));
    CHECK_NEAR(f.cfg.nucleus.childEnergyRatio, 0.6, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "reproduction_offset", 30.0));
    CHECK_NEAR(f.cfg.nucleus.reproductionOffset, 30.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "mutation_coefficient", 0.5));
    CHECK_NEAR(f.cfg.colony.mutationCoefficient, 0.5, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "water_cost_multiplier", 2.5));
    CHECK_NEAR(f.cfg.terrain.waterCostMultiplier, 2.5, 1e-12);
}

TEST(runtime_env_nucleus_and_ball_keys_wired) {
    Fresh f;
    CHECK(RuntimeEnv::applyOne(f.cfg, "nucleus_damping", 0.85));
    CHECK_NEAR(f.cfg.nucleus.damping, 0.85, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "nucleus_min_separation", 15.0));
    CHECK_NEAR(f.cfg.nucleus.minSeparation, 15.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "nucleus_repel_k", 3.0));
    CHECK_NEAR(f.cfg.nucleus.repelK, 3.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "forage_k", 1.5));
    CHECK_NEAR(f.cfg.nucleus.forageK, 1.5, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "forage_radius", 120.0));
    CHECK_NEAR(f.cfg.nucleus.forageRadius, 120.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "wander_k", 0.7));
    CHECK_NEAR(f.cfg.nucleus.wanderK, 0.7, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "wander_turn_rate", 0.8));
    CHECK_NEAR(f.cfg.nucleus.wanderTurnRate, 0.8, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "nucleus_init_energy_ratio", 1.2));
    CHECK_NEAR(f.cfg.nucleus.nucleusInitEnergyRatio, 1.2, 1e-12);

    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_damping", 0.95));
    CHECK_NEAR(f.cfg.ball.damping, 0.95, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "radial_k", 0.06));
    CHECK_NEAR(f.cfg.ball.radialK, 0.06, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "tangential_k", 1.2));
    CHECK_NEAR(f.cfg.ball.tangentialK, 1.2, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "near_repel_radius", 9.0));
    CHECK_NEAR(f.cfg.ball.nearRepelRadius, 9.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_ball_repel_radius", 7.0));
    CHECK_NEAR(f.cfg.ball.ballRepelRadius, 7.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_ball_repel_k", 0.6));
    CHECK_NEAR(f.cfg.ball.ballRepelK, 0.6, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "same_owner_attract_radius", 50.0));
    CHECK_NEAR(f.cfg.ball.sameOwnerAttractRadius, 50.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "same_owner_attract_k", 0.08));
    CHECK_NEAR(f.cfg.ball.sameOwnerAttractK, 0.08, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_max_speed_shield", 18.0));
    CHECK_NEAR(f.cfg.ball.maxSpeedShield, 18.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_max_speed_resource", 22.0));
    CHECK_NEAR(f.cfg.ball.maxSpeedResource, 22.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_max_speed_scout", 35.0));
    CHECK_NEAR(f.cfg.ball.maxSpeedScout, 35.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "follow_coupling", 5.0));
    CHECK_NEAR(f.cfg.ball.followCoupling, 5.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "follow_boost", 1.8));
    CHECK_NEAR(f.cfg.ball.followBoost, 1.8, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "bounce_restitution", 0.8));
    CHECK_NEAR(f.cfg.ball.bounceRestitution, 0.8, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "wrap_boundary", 0.0));
    CHECK(!f.cfg.ball.wrapBoundary);
    CHECK(RuntimeEnv::applyOne(f.cfg, "wrap_boundary", 1.0));
    CHECK(f.cfg.ball.wrapBoundary);

    // food 组的 ball_* 与 world 组。
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_energy", 12.0));
    CHECK_NEAR(f.cfg.ball.energy, 12.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_radius", 1.5));
    CHECK_NEAR(f.cfg.ball.radius, 1.5, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_vel_range", 6.0));
    CHECK_NEAR(f.cfg.ball.velRange, 6.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_initial_energy", 12.0));
    CHECK_NEAR(f.cfg.ball.initialEnergy, 12.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_energy_cap", 600.0));
    CHECK_NEAR(f.cfg.ball.energyCap, 600.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_spawn_probability", 0.03));
    CHECK_NEAR(f.cfg.cyanoTuning.ballSpawnProbability, 0.03, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_spawn_cost", 35.0));
    CHECK_NEAR(f.cfg.cyanoTuning.ballSpawnCost, 35.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_spawn_min_dist", 12.0));
    CHECK_NEAR(f.cfg.cyanoTuning.ballSpawnMinDist, 12.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_spawn_max_dist", 40.0));
    CHECK_NEAR(f.cfg.cyanoTuning.ballSpawnMaxDist, 40.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "ball_loss_cost", 6.0));
    CHECK_NEAR(f.cfg.ball.lossCost, 6.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "detach_radius", 70.0));
    CHECK_NEAR(f.cfg.ball.detachRadius, 70.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "nuclei", 20));
    CHECK(f.cfg.colony.nuclei == 20u);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_nodes", 48));
    CHECK(f.cfg.cyanoNodeCount == 48u);
    CHECK(RuntimeEnv::applyOne(f.cfg, "sense_radius", 30.0));
    CHECK_NEAR(f.cfg.policy.senseRadius, 30.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "max_fps", 30.0));
    CHECK_NEAR(f.cfg.runtime.maxFps, 30.0, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "render_interval", 2));
    CHECK(f.cfg.runtime.renderInterval == 2);
    CHECK(RuntimeEnv::applyOne(f.cfg, "autosave_interval", 100));
    CHECK(f.cfg.runtime.autosaveInterval == 100);
    CHECK(RuntimeEnv::applyOne(f.cfg, "balls", 10));
    CHECK(f.cfg.runtime.initialFreeBalls == 10);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_enabled", 0.0));
    CHECK(!f.cfg.runtime.cyanoEnabled);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_enabled", 1.0));
    CHECK(f.cfg.runtime.cyanoEnabled);

    // EnvControl food 轴拼写别名。
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_ball_spawn_probability", 0.04));
    CHECK_NEAR(f.cfg.cyanoTuning.ballSpawnProbability, 0.04, 1e-12);
    CHECK(RuntimeEnv::applyOne(f.cfg, "cyano_ball_spawn_cost", 40.0));
    CHECK_NEAR(f.cfg.cyanoTuning.ballSpawnCost, 40.0, 1e-12);
}

// ---------- 拒绝与未知 ----------

TEST(runtime_env_world_geometry_rejected_silently) {
    Fresh f;
    const double w = f.cfg.world.width;
    const double h = f.cfg.world.height;
    const unsigned long long seed = f.cfg.world.seed;
    const unsigned long long ticks = f.cfg.world.maxTicks;
    CHECK(!RuntimeEnv::applyOne(f.cfg, "width", 9999.0));
    CHECK(!RuntimeEnv::applyOne(f.cfg, "height", 9999.0));
    CHECK(!RuntimeEnv::applyOne(f.cfg, "seed", 777.0));
    CHECK(!RuntimeEnv::applyOne(f.cfg, "ticks", 555.0));
    CHECK(!RuntimeEnv::applyOne(f.cfg, "regrow_food", 1.0));  // 废弃键：Food 已删
    // 值未被改动。
    CHECK_NEAR(f.cfg.world.width, w, 1e-12);
    CHECK_NEAR(f.cfg.world.height, h, 1e-12);
    CHECK(f.cfg.world.seed == seed);
    CHECK(f.cfg.world.maxTicks == ticks);
    // 拒绝不算"未接线"：不进 unwiredKeys（关键契约）。
    CHECK(RuntimeEnv::unwiredKeys().empty());
}

TEST(runtime_env_unknown_key_fails_and_enters_unwired) {
    Fresh f;
    CHECK(!RuntimeEnv::applyOne(f.cfg, "definitely_not_a_key", 1.0));
    std::vector<std::string> u = RuntimeEnv::unwiredKeys();
    CHECK(u.size() == 1u);
    CHECK(!u.empty() && u[0] == "definitely_not_a_key");
    // 同 key 重复出现：去重（不重复入集）。
    CHECK(!RuntimeEnv::applyOne(f.cfg, "definitely_not_a_key", 2.0));
    CHECK(RuntimeEnv::unwiredKeys().size() == 1u);
    // 第二个未知 key。
    CHECK(!RuntimeEnv::applyOne(f.cfg, "another_bogus", 3.0));
    CHECK(RuntimeEnv::unwiredKeys().size() == 2u);
    // 接线键不进 unwired。
    CHECK(RuntimeEnv::applyOne(f.cfg, "sun_base_level", 2.0));
    CHECK(RuntimeEnv::unwiredKeys().size() == 2u);
}

TEST(runtime_env_nan_inf_rejected_not_unwired) {
    // NaN/Inf 是脏值：拒绝但不进 unwired、不告警（那是值错不是键错）。
    Fresh f;
    CHECK(!RuntimeEnv::applyOne(f.cfg, "sun_base_level", std::nan("")));
    CHECK(!RuntimeEnv::applyOne(f.cfg, "sun_base_level", HUGE_VAL));
    CHECK(!RuntimeEnv::applyOne(f.cfg, "mystery_key", std::nan("")));
    CHECK(RuntimeEnv::unwiredKeys().empty());
    CHECK_NEAR(f.cfg.sun.baseLevel, 0.5, 1e-12);  // 未被污染
}

TEST(runtime_env_apply_all_mixed_batch_partial_success) {
    // 验收用例：一批里混有合法/非法 key，合法的仍生效；返回值 = 成功条数。
    Fresh f;
    std::vector<EnvChange> batch;
    batch.push_back(ch("sun_base_level", 1.8));
    batch.push_back(ch("not_a_real_key", 1.0));
    batch.push_back(ch("width", 4000.0));      // 拒绝
    batch.push_back(ch("basal_cost", 0.02));
    batch.push_back(ch("energy_threshold", 150.0));
    const int ok = RuntimeEnv::applyAll(f.cfg, batch);
    CHECK(ok == 3);
    CHECK_NEAR(f.cfg.sun.baseLevel, 1.8, 1e-12);
    CHECK_NEAR(f.cfg.nucleus.respirationBase, 0.02, 1e-12);
    CHECK_NEAR(f.cfg.colony.genomeRanges.energyThresholdInitMin, 150.0, 1e-12);
    CHECK_NEAR(f.cfg.colony.genomeRanges.energyThresholdInitMax, 150.0, 1e-12);
    CHECK_NEAR(f.cfg.world.width, 1024.0, 1e-12);  // 拒绝键未生效（R10 世界默认 1024）
    // 只有未知键进 unwired（拒绝键不进）。
    std::vector<std::string> u = RuntimeEnv::unwiredKeys();
    CHECK(u.size() == 1u);
    CHECK(!u.empty() && u[0] == "not_a_real_key");
}

TEST(runtime_env_reset_unwired_clears) {
    Fresh f;
    CHECK(!RuntimeEnv::applyOne(f.cfg, "bogus_one", 1.0));
    CHECK(RuntimeEnv::unwiredKeys().size() == 1u);
    RuntimeEnv::resetUnwired();
    CHECK(RuntimeEnv::unwiredKeys().empty());
}

TEST(runtime_env_wired_key_count_matches_plan) {
    // 106 schema 键 + cyano_enabled + 2 别名 = 109 表行；拒绝名单 5 键不在表内。
    // （113 - 5 拒绝 + 2 别名 + 1 cyano_enabled = 111？不——schema 113 键中
    //  5 个被拒，剩 108 接线 + 2 别名 + cyano_enabled = 111。断言按此口径。）
    const int wired = RuntimeEnv::wiredKeyCount();
    CHECK(wired == 113);  // R9-A：+migration_bonus/migration_radius;
}

// ---------- 端到端：EnvControl 产出 -> RuntimeEnv 应用 -> SimConfig 变了 ----------
// （EnvControl 本体在 test_envcontrol.cpp 全覆盖；这里只验证接缝。）

TEST(runtime_env_envcontrol_to_config_pipeline) {
    Fresh f;
    using agentsim::app::EnvControl;
    using agentsim::app::EnvState;
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    // 轴快调：light -> 晴朗（3 档）。
    CHECK(EnvControl::apply("axis.light", "3", st, out, err));
    CHECK(RuntimeEnv::applyAll(f.cfg, out) == static_cast<int>(out.size()));
    CHECK_NEAR(f.cfg.sun.baseLevel, 1.8, 1e-12);     // 晴朗档 sun_base_level
    CHECK_NEAR(f.cfg.sun.dayAmplitude, 0.7, 1e-12);  // 晴朗档 sun_day_amplitude
    // 细调：生产者光合速率。
    out.clear();
    CHECK(EnvControl::apply("fine.cyano_photosynthesis_rate", "2.75", st, out, err));
    CHECK(RuntimeEnv::applyAll(f.cfg, out) == static_cast<int>(out.size()));
    CHECK_NEAR(f.cfg.cyanoTuning.photosynthesisRate, 2.75, 1e-12);
    // cyano 开关产出 cyano_enabled 变更。
    out.clear();
    CHECK(EnvControl::apply("cyano", "off", st, out, err));
    CHECK(out.size() == 1u);
    CHECK(RuntimeEnv::applyAll(f.cfg, out) == 1);
    CHECK(!f.cfg.runtime.cyanoEnabled);
    // 变更后的 genome 键只动 ranges（模拟下一胎生效），世界其他面不受影响。
    out.clear();
    CHECK(EnvControl::apply("fine.sun_base_level", "0.9", st, out, err));
    CHECK(RuntimeEnv::applyAll(f.cfg, out) == 1);
    CHECK_NEAR(f.cfg.sun.baseLevel, 0.9, 1e-12);
    CHECK(RuntimeEnv::unwiredKeys().empty());
}

// ---------- 端到端：应用一批变更后跑 200 tick，审计仍 ok（验收清单第 5 条）----------

TEST(runtime_env_apply_then_run_200_ticks_audit_ok) {
    Fresh f;
    // 一批覆盖各映射组的变更（含 genome/light/cyano/pressure/ball）。
    std::vector<EnvChange> batch;
    batch.push_back(ch("sun_base_level", 1.2));
    batch.push_back(ch("sun_day_amplitude", 0.3));
    batch.push_back(ch("cyano_photosynthesis_rate", 1.5));
    batch.push_back(ch("cyano_metabolism_rate", 0.015));
    batch.push_back(ch("basal_cost", 0.006));
    batch.push_back(ch("speed_cost_k", 0.0003));
    batch.push_back(ch("mutation_coefficient", 0.3));
    batch.push_back(ch("cyano_light_half_saturation", 0.4));
    batch.push_back(ch("cyano_transfer_rate", 0.15));
    batch.push_back(ch("cyano_link_distance", 70.0));
    batch.push_back(ch("cyano_predation_radius", 16.0));
    batch.push_back(ch("energy_threshold", 120.0));   // genome：新生核基准
    batch.push_back(ch("max_speed", 80.0));           // genome
    batch.push_back(ch("cyano_preference", 0.6));     // genome
    batch.push_back(ch("max_fps", 0.0));
    CHECK(RuntimeEnv::applyAll(f.cfg, batch) == static_cast<int>(batch.size()));
    CHECK(RuntimeEnv::unwiredKeys().empty());
    // 用改过的配置起仿真（组合根：构造期消费 genomeRanges/库存），
    // applyConfig 热改一轮（serve 帧循环语义），跑 200 tick，审计必须仍 ok。
    agentsim::Simulation sim(f.cfg);
    sim.applyConfig(f.cfg);
    CHECK(sim.runTo(200));
    CHECK(sim.auditOk());
    CHECK(sim.tick() == 200);
    CHECK(sim.nucleusCount() > 0);
}

TEST(runtime_env_add_cyano_nodes_event_semantics) {
    Fresh f;
    f.cfg.cyanoNodeCount = 16;
    f.cfg.cyanoMaxNodes = 32;
    agentsim::Simulation sim(f.cfg);
    const std::size_t before = sim.cyanoNodes().size();
    CHECK(before >= 16u);
    // 事件 bloom：追加 8 个节点。
    const int added = sim.addCyanoNodes(8);
    CHECK(added == 8);
    CHECK(sim.cyanoNodes().size() == before + 8u);
    // 护栏：maxNodes=32，请求 100 只补到 32。
    const int added2 = sim.addCyanoNodes(100);
    CHECK(added2 == 32 - static_cast<int>(before) - 8);
    CHECK(sim.cyanoNodes().size() == 32u);
    // 追加后跑 50 tick：新节点从 0 能量光合生长，审计仍 ok。
    CHECK(sim.runTo(250));
    CHECK(sim.auditOk());
    double cyanoEnergy = 0.0;
    for (const agentsim::CyanoNode& c : sim.cyanoNodes()) cyanoEnergy += c.energy;
    CHECK(cyanoEnergy > 0.0);  // 光合确实喂到了追加节点
}

TEST(runtime_env_apply_config_hot_effects_next_frame) {
    // 热改证明：同一仿真对象，改 sun 光强 + cyano 参数后下一帧口径变化。
    Fresh f;
    f.cfg.cyanoNodeCount = 8;
    f.cfg.cyanoMaxNodes = 64;
    agentsim::Simulation sim(f.cfg);
    CHECK(sim.runTo(30));
    CHECK(sim.auditOk());
    // 热改：光合拉满 + 光照拉满（用 RuntimeEnv 的合法路径生成目标 cfg）。
    SimConfig next = f.cfg;
    CHECK(RuntimeEnv::applyOne(next, "cyano_photosynthesis_rate", 5.0));
    CHECK(RuntimeEnv::applyOne(next, "sun_base_level", 2.5));
    sim.applyConfig(next);
    // CyanoParams 白盒同步立即可见（下一帧 cyano_.step() 按新参数结算）。
    CHECK_NEAR(sim.cyanoParams().photosynthesisRate, 5.0, 1e-12);
    CHECK_NEAR(sim.cyanoParams().lightHalfSaturation, 0.5, 1e-12);
    // genomeRanges 热改：只影响之后出生的核——已存在的 genome 逐位不动。
    const double hueBefore = sim.nuclei().empty() ? -1.0 : sim.nuclei()[0].genome.hue;
    const double threshBefore =
        sim.nuclei().empty() ? -1.0 : sim.nuclei()[0].genome.energyThreshold;
    CHECK(RuntimeEnv::applyOne(next, "energy_threshold", 300.0));
    sim.applyConfig(next);
    CHECK_NEAR(sim.nuclei()[0].genome.energyThreshold, threshBefore, 1e-12);
    CHECK_NEAR(sim.nuclei()[0].genome.hue, hueBefore, 1e-12);  // 基因不能后天改
    CHECK_NEAR(sim.cfgProbe().colony.genomeRanges.energyThresholdInitMin, 300.0, 1e-12);
    // 世界几何/种子拒绝热改（applyConfig 的 merge 保护）。
    CHECK(RuntimeEnv::applyOne(next, "sun_noise_amplitude", 0.1));
    next.world.width = 9999.0;  // 直接篡改 next（绕过 RuntimeEnv 模拟上游 bug）
    sim.applyConfig(next);
    CHECK_NEAR(sim.cfgProbe().world.width, 1024.0, 1e-12);  // merge 还原（R10）
    CHECK(sim.auditOk());
}

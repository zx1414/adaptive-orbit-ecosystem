#include "EnvConfig.h"
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}  // namespace

bool EnvConfig::load(const std::string& path, WorldConfig& cfg) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;  // 空行/注释

        size_t eq = t.find('=');
        if (eq == std::string::npos) {
            std::cerr << "[EnvConfig] 第 " << lineNo << " 行缺少 '='，已跳过: " << t << "\n";
            continue;
        }
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));
        // 去掉内联注释
        size_t hash = val.find('#');
        if (hash != std::string::npos) val = trim(val.substr(0, hash));

        auto toD = [&]() -> double { return std::stod(val); };
        auto toI = [&]() -> int { return std::stoi(val); };
        auto toB = [&]() -> bool {
            std::string v = val;
            for (char& c : v) c = (char)std::tolower((unsigned char)c);
            return v == "on" || v == "true" || v == "1" || v == "yes";
        };
        // 解析 "lo, hi" 或单个值（单个值则 lo==hi）
        auto toPair = [&](double& lo, double& hi) {
            size_t comma = val.find(',');
            if (comma == std::string::npos) {
                lo = std::stod(val);
                hi = lo;
            } else {
                lo = std::stod(trim(val.substr(0, comma)));
                hi = std::stod(trim(val.substr(comma + 1)));
            }
        };
        try {
            if (key == "width") cfg.width = toD();
            else if (key == "height") cfg.height = toD();
            else if (key == "seed") cfg.seed = toI();
            else if (key == "balls") cfg.initialBalls = toI();
            else if (key == "nuclei") cfg.initialNuclei = toI();
            else if (key == "frames") cfg.maxFrames = toI();
            else if (key == "render_interval") cfg.renderInterval = toI();
            else if (key == "sample_interval") cfg.sampleInterval = toI();
            else if (key == "trend_interval") cfg.trendInterval = toI();
            else if (key == "max_fps") cfg.maxFps = toD();
            else if (key == "visualization") cfg.visualization = toB();
            else if (key == "clear_screen") cfg.clearScreen = toB();
            else if (key == "grid_cols") cfg.gridCols = toI();
            else if (key == "grid_rows") cfg.gridRows = toI();
            else if (key == "lock_console") cfg.lockConsole = toB();
            else if (key == "pause_on_exit") cfg.pauseOnExit = toB();
            else if (key == "max_nuclei") cfg.maxNuclei = toI();
            else if (key == "territory_per_nucleus") cfg.territoryPerNucleus = toD();
            else if (key == "absorb_radius") cfg.absorbRadius = toD();
            else if (key == "ball_spawn_probability") cfg.ballSpawnProbability = toD();
            else if (key == "base_ball_energy") cfg.baseBallEnergy = toD();
            else if (key == "grid_cell_size") cfg.gridCellSize = toD();
            else if (key == "ball_damping") cfg.ballDamping = toD();
            else if (key == "radial_k") cfg.radialK = toD();
            else if (key == "tangential_k") cfg.tangentialK = toD();
            else if (key == "near_repel_radius") cfg.nearRepelRadius = toD();
            else if (key == "ball_ball_repel_radius") cfg.ballBallRepelRadius = toD();
            else if (key == "ball_ball_repel_k") cfg.ballBallRepelK = toD();
            else if (key == "ball_same_owner_attract_radius") cfg.ballSameOwnerAttractRadius = toD();
            else if (key == "ball_same_owner_attract_k") cfg.ballSameOwnerAttractK = toD();
            else if (key == "nucleus_damping") cfg.nucleusDamping = toD();
            else if (key == "attack_k") cfg.attackK = toD();
            else if (key == "avoid_k") cfg.avoidK = toD();
            else if (key == "combat_rate") cfg.combatRate = toD();
            else if (key == "influence_margin") cfg.influenceMargin = toD();
            else if (key == "forage_k") cfg.forageK = toD();
            else if (key == "forage_radius") cfg.forageRadius = toD();
            else if (key == "wander_k") cfg.wanderK = toD();
            else if (key == "wander_turn_rate") cfg.wanderTurnRate = toD();
            else if (key == "basal_cost") cfg.basalCost = toD();
            else if (key == "speed_cost_k") cfg.speedCostK = toD();
            else if (key == "speed_cost_exponent") cfg.speedCostExponent = toD();
            else if (key == "reproduction_cost_ratio") cfg.reproductionCostRatio = toD();
            else if (key == "child_energy_ratio") cfg.childEnergyRatio = toD();
            else if (key == "nucleus_init_energy_ratio") cfg.nucleusInitEnergyRatio = toD();
            else if (key == "reproduction_offset") cfg.reproductionOffset = toD();
            else if (key == "nucleus_min_separation") cfg.nucleusMinSeparation = toD();
            else if (key == "nucleus_repel_k") cfg.nucleusRepelK = toD();
            else if (key == "newborn_grace_frames") cfg.newbornGraceFrames = toI();
            else if (key == "ball_max_speed_shield") cfg.ballMaxSpeedShield = toD();
            else if (key == "ball_max_speed_worker") cfg.ballMaxSpeedWorker = toD();
            else if (key == "ball_max_speed_scout") cfg.ballMaxSpeedScout = toD();
            else if (key == "ball_vel_range") cfg.ballVelRange = toD();
            else if (key == "bounce_restitution") cfg.bounceRestitution = toD();
            else if (key == "follow_boost") cfg.followBoost = toD();
            else if (key == "follow_coupling") cfg.followCoupling = toD();
            else if (key == "detach_radius") cfg.detachRadius = toD();
            else if (key == "ball_loss_cost") cfg.ballLossCost = toD();
            // 遗传参数：初始化范围（lo, hi）
            else if (key == "affinity_init") toPair(cfg.ranges.affinityInitMin, cfg.ranges.affinityInitMax);
            else if (key == "orbit_radius_init") toPair(cfg.ranges.orbitRadiusInitMin, cfg.ranges.orbitRadiusInitMax);
            else if (key == "absorb_preference_init") toPair(cfg.ranges.absorbPreferenceInitMin, cfg.ranges.absorbPreferenceInitMax);
            else if (key == "repel_strength_init") toPair(cfg.ranges.repelStrengthInitMin, cfg.ranges.repelStrengthInitMax);
            else if (key == "attack_range_init") toPair(cfg.ranges.attackRangeInitMin, cfg.ranges.attackRangeInitMax);
            else if (key == "attack_strength_init") toPair(cfg.ranges.attackStrengthInitMin, cfg.ranges.attackStrengthInitMax);
            else if (key == "avoid_range_init") toPair(cfg.ranges.avoidRangeInitMin, cfg.ranges.avoidRangeInitMax);
            else if (key == "avoid_strength_init") toPair(cfg.ranges.avoidStrengthInitMin, cfg.ranges.avoidStrengthInitMax);
            else if (key == "max_speed_init") toPair(cfg.ranges.maxSpeedInitMin, cfg.ranges.maxSpeedInitMax);
            else if (key == "energy_threshold_init") toPair(cfg.ranges.energyThresholdInitMin, cfg.ranges.energyThresholdInitMax);
            else if (key == "mutation_rate_init") toPair(cfg.ranges.mutationRateInitMin, cfg.ranges.mutationRateInitMax);
            // 遗传参数：变异裁剪范围（lo, hi）
            else if (key == "affinity_clamp_min") cfg.ranges.affinityClampMin = toD();
            else if (key == "orbit_radius_clamp") toPair(cfg.ranges.orbitRadiusClampMin, cfg.ranges.orbitRadiusClampMax);
            else if (key == "absorb_preference_clamp") toPair(cfg.ranges.absorbPreferenceClampMin, cfg.ranges.absorbPreferenceClampMax);
            else if (key == "repel_strength_clamp") toPair(cfg.ranges.repelStrengthClampMin, cfg.ranges.repelStrengthClampMax);
            else if (key == "attack_range_clamp") toPair(cfg.ranges.attackRangeClampMin, cfg.ranges.attackRangeClampMax);
            else if (key == "attack_strength_clamp") toPair(cfg.ranges.attackStrengthClampMin, cfg.ranges.attackStrengthClampMax);
            else if (key == "avoid_range_clamp") toPair(cfg.ranges.avoidRangeClampMin, cfg.ranges.avoidRangeClampMax);
            else if (key == "avoid_strength_clamp") toPair(cfg.ranges.avoidStrengthClampMin, cfg.ranges.avoidStrengthClampMax);
            else if (key == "max_speed_clamp") toPair(cfg.ranges.maxSpeedClampMin, cfg.ranges.maxSpeedClampMax);
            else if (key == "energy_threshold_clamp") toPair(cfg.ranges.energyThresholdClampMin, cfg.ranges.energyThresholdClampMax);
            else if (key == "mutation_rate_clamp") toPair(cfg.ranges.mutationRateClampMin, cfg.ranges.mutationRateClampMax);
            else std::cerr << "[EnvConfig] 未知键，已忽略: " << key << "\n";
        } catch (const std::exception&) {
            std::cerr << "[EnvConfig] 第 " << lineNo << " 行值解析失败: " << key << " = " << val << "\n";
        }
    }
    return true;
}

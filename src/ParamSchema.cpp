#include "ParamSchema.h"
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
// 构造宏：非 Pair 字段（默认值取自 WorldConfig{} 的默认成员初始化）。
#define DEF(k, g, d, T, field, mn, mx) \
    {k, g, d, ParamDef::Type::T, offsetof(WorldConfig, field), 0, mn, mx, \
     (double)WorldConfig{}.field, 0.0}
// 遗传范围 Pair 字段：位于 cfg.ranges 子结构内。
#define DEF_RANGE(k, g, d, loField, hiField, mn, mx) \
    {k, g, d, ParamDef::Type::Pair, \
     offsetof(WorldConfig, ranges) + offsetof(ParamRanges, loField), \
     offsetof(WorldConfig, ranges) + offsetof(ParamRanges, hiField), \
     mn, mx, (double)kDefaults.ranges.loField, (double)kDefaults.ranges.hiField}

// 默认值副本：表项用 (double)kDefaults.field 取默认值。
const WorldConfig kDefaults;

const char* GRP_WORLD = "世界与运行";
const char* GRP_ENV = "环境与资源";
const char* GRP_BALL = "小球动力学";
const char* GRP_NUC = "核动力学";
const char* GRP_GENE = "遗传参数范围";

// 说明格式：含义；调节效果（调大/调小会发生什么）。
// 注意：所有键名必须与 EnvConfig.cpp 的解析键一致（coverageCheck 兜底防漂移）。
const std::vector<ParamDef>& buildTable() {
    static const std::vector<ParamDef> table = {
        DEF("width", GRP_WORLD, "世界宽度（地图横向尺寸）；面积越大，按面积自动计算的承载力越高", Double, width, 100, 100000),
        DEF("height", GRP_WORLD, "世界高度（地图纵向尺寸）；与宽度共同决定世界大小", Double, height, 100, 100000),
        DEF("seed", GRP_WORLD, "随机种子；同种子 + 同参数可完全复现，换种子得到一次全新的演化", Int, seed, 0, 2147483647),
        DEF("balls", GRP_WORLD, "初始自由小球数量；小球是能量的唯一来源，越多食物越充足", Int, initialBalls, 0, 100000),
        DEF("nuclei", GRP_WORLD, "初始随机核数量；核越多竞争越激烈、生态越拥挤", Int, initialNuclei, 0, 10000),
        DEF("frames", GRP_WORLD, "总模拟帧数；跑满该帧数后模拟结束", Int, maxFrames, 1, 1000000000),
        DEF("render_interval", GRP_WORLD, "浏览器画面快照间隔帧数；越小画面越流畅（前端负载越高），0 关闭", Int, renderInterval, 0, 10000),
        DEF("sample_interval", GRP_WORLD, "全场 CSV 快照采样间隔（导出 balls_*/nuclei_*.csv）；0 关闭", Int, sampleInterval, 0, 100000),
        DEF("trend_interval", GRP_WORLD, "核参数时间序列采样间隔（趋势 CSV）；仅命令行 --trend-csv 使用", Int, trendInterval, 1, 10000),
        DEF("max_fps", GRP_WORLD, "模拟帧率上限；0 = 不限速跑满 CPU，设 30/60 便于肉眼观察演化过程", Double, maxFps, 0, 1000),
        DEF("visualization", GRP_WORLD, "控制台字符渲染总开关（只影响 --console 模式，不影响启动器画面）", Bool, visualization, 0, 1),
        DEF("clear_screen", GRP_WORLD, "控制台每次渲染前是否清屏；黑屏闪烁时建议关闭", Bool, clearScreen, 0, 1),
        DEF("grid_cols", GRP_WORLD, "控制台字符网格列数（画面宽度，按字符计）", Int, gridCols, 20, 400),
        DEF("grid_rows", GRP_WORLD, "控制台字符网格行数（画面高度，按字符计）", Int, gridRows, 10, 200),
        DEF("lock_console", GRP_WORLD, "启动时锁定控制台窗口为合适大小（字符网格 + 统计行）", Bool, lockConsole, 0, 1),
        DEF("pause_on_exit", GRP_WORLD, "控制台模拟结束后等待按键（双击运行时防止窗口闪退）", Bool, pauseOnExit, 0, 1),

        DEF("max_nuclei", GRP_ENV, "环境承载力（核总数上限）；0 = 按面积自动估算。核数量受此约束，防止无限增长", Int, maxNuclei, 0, 100000),
        DEF("territory_per_nucleus", GRP_ENV, "承载力估算时每个核占用的面积；调小 = 同样的世界容纳更多核", Double, territoryPerNucleus, 100, 100000),
        DEF("absorb_radius", GRP_ENV, "核吸收小球的半径；调大核更容易吃到球（能量来源变宽松）", Double, absorbRadius, 1, 500),
        DEF("ball_spawn_probability", GRP_ENV, "每帧随机补充自由小球的概率；调大食物更充裕、生态更富饶", Double, ballSpawnProbability, 0, 1),
        DEF("base_ball_energy", GRP_ENV, "吸收单个小球获得的基础能量；调大核积累能量更快、更容易繁殖", Double, baseBallEnergy, 0.01, 100),
        DEF("grid_cell_size", GRP_ENV, "空间哈希网格单元大小；影响查询性能，60 附近为宜（太小内存大、太大查询慢）", Double, gridCellSize, 10, 500),

        DEF("ball_damping", GRP_BALL, "小球速度阻尼（每帧保留比例）；越小惯性越小、运动越'黏'", Double, ballDamping, 0.5, 1),
        DEF("radial_k", GRP_BALL, "轨道径向弹簧系数——偏离期望环绕半径时拉回的力度；调大球群轨道更紧、更规整", Double, radialK, 0, 1),
        DEF("tangential_k", GRP_BALL, "切向（环绕）驱动系数；调大环绕公转更快、轨道更圆", Double, tangentialK, 0, 10),
        DEF("near_repel_radius", GRP_BALL, "小球近核排斥触发半径；防止小球贴到核上，调大分层更明显", Double, nearRepelRadius, 1, 200),
        DEF("ball_ball_repel_radius", GRP_BALL, "小球间短程排斥半径；调大球与球保持距离、球群更分散", Double, ballBallRepelRadius, 1, 100),
        DEF("ball_ball_repel_k", GRP_BALL, "小球间短程排斥强度；与排斥半径配合决定球群疏密", Double, ballBallRepelK, 0, 10),
        DEF("ball_same_owner_attract_radius", GRP_BALL, "同核小球相互吸引半径——让同一核的球聚成球群；0 = 关闭", Double, ballSameOwnerAttractRadius, 0, 200),
        DEF("ball_same_owner_attract_k", GRP_BALL, "同核小球相互吸引强度；调大球群更凝聚", Double, ballSameOwnerAttractK, 0, 1),
        DEF("ball_max_speed_shield", GRP_BALL, "护盾球速度上限；决定护盾球能跟多快的核", Double, ballMaxSpeedShield, 1, 500),
        DEF("ball_max_speed_worker", GRP_BALL, "资源球速度上限；决定资源球能跟多快的核", Double, ballMaxSpeedWorker, 1, 500),
        DEF("ball_max_speed_scout", GRP_BALL, "侦察球速度上限；决定侦察球能跟多快的核", Double, ballMaxSpeedScout, 1, 500),
        DEF("ball_vel_range", GRP_BALL, "初始/补充小球速度随机范围（±该值）", Double, ballVelRange, 0, 50),
        DEF("bounce_restitution", GRP_BALL, "边界反弹恢复系数；1 = 完全弹性，0 = 贴墙停住", Double, bounceRestitution, 0, 1),
        DEF("follow_boost", GRP_BALL, "跟随加速倍率——归属球限速 = max(类型上限, 核速×该值)；0 = 关闭（核太快会甩掉球群）", Double, followBoost, 0, 10),
        DEF("follow_coupling", GRP_BALL, "归属核轨道弹簧加强倍数——让快核甩不掉自己的球；0 = 关闭", Double, followCoupling, 0, 100),
        DEF("detach_radius", GRP_BALL, "归属球距核超过该距离即脱附变自由；调大更难被甩掉", Double, detachRadius, 50, 2000),
        DEF("ball_loss_cost", GRP_BALL, "失去一个归属球（脱附/被抢）扣除的能量；调大'甩球'代价更高。注意：过大 + 球多会连锁破产", Double, ballLossCost, 0, 1000),

        DEF("nucleus_damping", GRP_NUC, "核速度阻尼（每帧保留比例）；越小转向越灵活、惯性越小", Double, nucleusDamping, 0.5, 1),
        DEF("attack_k", GRP_NUC, "攻击追逐加速度系数——追击比自己弱的目标；调大掠食者更凶悍", Double, attackK, 0, 10),
        DEF("avoid_k", GRP_NUC, "避让加速度系数——躲避比自己强的对手；调大弱者更会逃", Double, avoidK, 0, 10),
        DEF("combat_rate", GRP_NUC, "争斗能量转移速率；调大战斗更致命、输家更快死亡", Double, combatRate, 0, 10),
        DEF("influence_margin", GRP_NUC, "核力场查询半径的额外余量；影响小球感知核作用力的范围", Double, influenceMargin, 0, 500),
        DEF("forage_k", GRP_NUC, "核朝附近小球觅食的加速度系数；调大核觅食更积极", Double, forageK, 0, 10),
        DEF("forage_radius", GRP_NUC, "核觅食的小球查询半径；调大能发现更远的食物", Double, forageRadius, 10, 2000),
        DEF("wander_k", GRP_NUC, "随机游走（探索）加速度系数；调大核更爱到处跑、扩散更快", Double, wanderK, 0, 10),
        DEF("wander_turn_rate", GRP_NUC, "游走方向每帧最大转向角（弧度）；小 = 路径笔直，大 = 原地打转", Double, wanderTurnRate, 0, 3),
        DEF("basal_cost", GRP_NUC, "基础代谢：每帧固定能量流逝（静止也要扣）；调大生存压力更大、竞争更残酷", Double, basalCost, 0, 10),
        DEF("speed_cost_k", GRP_NUC, "速度代价系数；调大'跑得快'更耗能（速度需要付出代价）", Double, speedCostK, 0, 1),
        DEF("speed_cost_exponent", GRP_NUC, "速度代价指数（>1 为超线性增长）；越大高速惩罚越狠", Double, speedCostExponent, 1, 4),
        DEF("reproduction_cost_ratio", GRP_NUC, "繁殖代价——父核消耗 = 比值 × 繁殖阈值；调大繁殖更昂贵、更慎重", Double, reproductionCostRatio, 0, 2),
        DEF("child_energy_ratio", GRP_NUC, "子核初始能量 = 比值 × 繁殖阈值；调大子代出生就富、起步更高", Double, childEnergyRatio, 0, 1),
        DEF("nucleus_init_energy_ratio", GRP_NUC, "初始随机核能量 = 比值 × 繁殖阈值；调大开局更宽裕", Double, nucleusInitEnergyRatio, 0, 2),
        DEF("reproduction_offset", GRP_NUC, "子核出生位置相对父核的随机偏移（±）；调大子代离得更远", Double, reproductionOffset, 0, 500),
        DEF("nucleus_min_separation", GRP_NUC, "核最小间距——小于该距离施强斥力且跳过攻击（防止重叠与抖动）", Double, nucleusMinSeparation, 0, 200),
        DEF("nucleus_repel_k", GRP_NUC, "核贴脸斥力刚度；调大两核对峙时'顶得更硬'", Double, nucleusRepelK, 0, 100),
        DEF("newborn_grace_frames", GRP_NUC, "新核保护期帧数——期内不主动攻击、也不被攻击（防父杀子）", Int, newbornGraceFrames, 0, 10000),

        DEF_RANGE("affinity_init", GRP_GENE, "亲和力初始化范围（三种球各随机一值后归一化）——决定核天生更吸引哪类球", affinityInitMin, affinityInitMax, 0, 10),
        DEF_RANGE("orbit_radius_init", GRP_GENE, "期望环绕半径初始化范围——决定球群天生离核多远", orbitRadiusInitMin, orbitRadiusInitMax, 1, 1000),
        DEF_RANGE("absorb_preference_init", GRP_GENE, "吸收偏好初始化范围——决定天生吃哪类球收益更高", absorbPreferenceInitMin, absorbPreferenceInitMax, 0, 10),
        DEF_RANGE("repel_strength_init", GRP_GENE, "近距排斥强度初始化范围——小球贴核时的推开力度", repelStrengthInitMin, repelStrengthInitMax, 0, 50),
        DEF_RANGE("attack_range_init", GRP_GENE, "攻击距离初始化范围——决定天生好战程度（能打多远）", attackRangeInitMin, attackRangeInitMax, 1, 2000),
        DEF_RANGE("attack_strength_init", GRP_GENE, "攻击强度初始化范围——决定强弱势判断与吸能/追击速率", attackStrengthInitMin, attackStrengthInitMax, 0, 1000),
        DEF_RANGE("avoid_range_init", GRP_GENE, "避让距离初始化范围——多远开始躲避强者", avoidRangeInitMin, avoidRangeInitMax, 1, 2000),
        DEF_RANGE("avoid_strength_init", GRP_GENE, "避让强度初始化范围——躲强者的加速度大小", avoidStrengthInitMin, avoidStrengthInitMax, 0, 1000),
        DEF_RANGE("max_speed_init", GRP_GENE, "核最大速度初始化范围——影响追击/逃跑能力，快慢策略由此分化", maxSpeedInitMin, maxSpeedInitMax, 1, 2000),
        DEF_RANGE("energy_threshold_init", GRP_GENE, "繁殖阈值初始化范围——阈值越低越早繁殖、越高越稳（攒够再分家）", energyThresholdInitMin, energyThresholdInitMax, 1, 10000),
        DEF_RANGE("mutation_rate_init", GRP_GENE, "变异幅度初始化范围——调大进化更狂野、策略更多样（也更不稳定）", mutationRateInitMin, mutationRateInitMax, 0, 2),
        {"affinity_clamp_min", GRP_GENE, "亲和力变异裁剪下限（随后重新归一化）——防止变异把亲和力压到 0", ParamDef::Type::Double,
          offsetof(WorldConfig, ranges) + offsetof(ParamRanges, affinityClampMin), 0,
          0, 1, (double)kDefaults.ranges.affinityClampMin, 0.0},
        DEF_RANGE("orbit_radius_clamp", GRP_GENE, "环绕半径变异裁剪范围——进化中允许达到的轨道远近极限", orbitRadiusClampMin, orbitRadiusClampMax, 1, 5000),
        DEF_RANGE("absorb_preference_clamp", GRP_GENE, "吸收偏好变异裁剪范围——进化中吃球收益倍率的极限", absorbPreferenceClampMin, absorbPreferenceClampMax, 0, 20),
        DEF_RANGE("repel_strength_clamp", GRP_GENE, "排斥强度变异裁剪范围——进化中的推开力度极限", repelStrengthClampMin, repelStrengthClampMax, 0, 100),
        DEF_RANGE("attack_range_clamp", GRP_GENE, "攻击距离变异裁剪范围——进化中好战程度的极限", attackRangeClampMin, attackRangeClampMax, 1, 5000),
        DEF_RANGE("attack_strength_clamp", GRP_GENE, "攻击强度变异裁剪范围——进化中武力值的极限", attackStrengthClampMin, attackStrengthClampMax, 0, 2000),
        DEF_RANGE("avoid_range_clamp", GRP_GENE, "避让距离变异裁剪范围——进化中警觉范围的极限", avoidRangeClampMin, avoidRangeClampMax, 1, 5000),
        DEF_RANGE("avoid_strength_clamp", GRP_GENE, "避让强度变异裁剪范围——进化中逃跑能力的极限", avoidStrengthClampMin, avoidStrengthClampMax, 0, 2000),
        DEF_RANGE("max_speed_clamp", GRP_GENE, "核最大速度变异裁剪范围——进化中速度的极限", maxSpeedClampMin, maxSpeedClampMax, 1, 5000),
        DEF_RANGE("energy_threshold_clamp", GRP_GENE, "繁殖阈值变异裁剪范围——进化中繁殖策略的极限", energyThresholdClampMin, energyThresholdClampMax, 1, 100000),
        DEF_RANGE("mutation_rate_clamp", GRP_GENE, "变异幅度变异裁剪范围——进化中突变程度的极限（变异率本身也遗传）", mutationRateClampMin, mutationRateClampMax, 0, 2),
    };
    return table;
}

// 字段指针解引用：WorldConfig 内偏移（含 ranges 子结构）。
void* fieldPtr(WorldConfig& cfg, size_t off) { return reinterpret_cast<char*>(&cfg) + off; }
const void* fieldPtr(const WorldConfig& cfg, size_t off) {
    return reinterpret_cast<const char*>(&cfg) + off;
}

double readDouble(const WorldConfig& cfg, const ParamDef& d, size_t off) {
    switch (d.type) {
        case ParamDef::Type::Int: {
            int v;
            std::memcpy(&v, fieldPtr(cfg, off), sizeof(v));
            return (double)v;
        }
        case ParamDef::Type::Double: {
            double v;
            std::memcpy(&v, fieldPtr(cfg, off), sizeof(v));
            return v;
        }
        case ParamDef::Type::Bool: {
            bool v;
            std::memcpy(&v, fieldPtr(cfg, off), sizeof(v));
            return v ? 1.0 : 0.0;
        }
        default: return 0.0;
    }
}

void writeDouble(WorldConfig& cfg, const ParamDef& d, size_t off, double v) {
    if (d.type == ParamDef::Type::Int) {
        int iv = (int)v;
        std::memcpy(fieldPtr(cfg, off), &iv, sizeof(iv));
    } else if (d.type == ParamDef::Type::Double) {
        std::memcpy(fieldPtr(cfg, off), &v, sizeof(v));
    } else if (d.type == ParamDef::Type::Bool) {
        bool bv = v != 0.0;
        std::memcpy(fieldPtr(cfg, off), &bv, sizeof(bv));
    }
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}  // namespace

const std::vector<ParamDef>& ParamSchema::all() { return buildTable(); }

const ParamDef* ParamSchema::find(const std::string& key) {
    for (const ParamDef& d : all()) {
        if (key == d.key) return &d;
    }
    return nullptr;
}

std::string ParamSchema::valueString(const WorldConfig& cfg, const ParamDef& d) {
    char buf[64];
    if (d.type == ParamDef::Type::Pair) {
        std::snprintf(buf, sizeof(buf), "%g, %g", readDouble(cfg, d, d.offA),
                      readDouble(cfg, d, d.offB));
    } else if (d.type == ParamDef::Type::Bool) {
        return readDouble(cfg, d, d.offA) != 0.0 ? "1" : "0";
    } else if (d.type == ParamDef::Type::Int) {
        std::snprintf(buf, sizeof(buf), "%d", (int)readDouble(cfg, d, d.offA));
    } else {
        std::snprintf(buf, sizeof(buf), "%g", readDouble(cfg, d, d.offA));
    }
    return buf;
}

bool ParamSchema::applyLine(WorldConfig& cfg, const std::string& key, const std::string& rawValue) {
    const ParamDef* d = find(key);
    if (!d) return false;
    std::string val = trim(rawValue);
    // 去掉内联注释
    size_t hash = val.find('#');
    if (hash != std::string::npos) val = trim(val.substr(0, hash));
    if (val.empty()) return false;
    try {
        if (d->type == ParamDef::Type::Pair) {
            size_t comma = val.find(',');
            double lo, hi;
            if (comma == std::string::npos) {
                lo = hi = std::stod(val);
            } else {
                lo = std::stod(trim(val.substr(0, comma)));
                hi = std::stod(trim(val.substr(comma + 1)));
            }
            writeDouble(cfg, *d, d->offA, lo);
            writeDouble(cfg, *d, d->offB, hi);
        } else if (d->type == ParamDef::Type::Bool) {
            std::string v = val;
            for (char& c : v) c = (char)std::tolower((unsigned char)c);
            writeDouble(cfg, *d, d->offA,
                        (v == "on" || v == "true" || v == "1" || v == "yes") ? 1.0 : 0.0);
        } else {
            writeDouble(cfg, *d, d->offA, std::stod(val));
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool ParamSchema::writeEnvFile(const std::string& path, const WorldConfig& base,
                               const std::map<std::string, std::string>& overrides) {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "# 环境配置文件（由启动器生成，key = value，# 注释）\n";
    f << "# 程序启动时按 exe 同目录 → 当前目录 顺序自动查找。\n";
    const char* lastGroup = nullptr;
    for (const ParamDef& d : all()) {
        if (!lastGroup || std::strcmp(lastGroup, d.group) != 0) {
            lastGroup = d.group;
            f << "\n# ===== " << d.group << " =====\n";
        }
        std::string value;
        auto it = overrides.find(d.key);
        if (it != overrides.end()) {
            value = it->second;
        } else {
            value = valueString(base, d);
        }
        f << "# " << d.desc << "（范围 " << d.min << " ~ " << d.max << "）\n";
        f << d.key << " = " << value << "\n";
    }
    return true;
}

bool ParamSchema::coverageCheck() {
    // EnvConfig.cpp 能识别的全部键（必须与 EnvConfig::load 同步，本检查防漂移）。
    static const char* envKeys[] = {
        "width", "height", "seed", "balls", "nuclei", "frames",
        "render_interval", "sample_interval", "trend_interval", "max_fps",
        "visualization", "clear_screen", "grid_cols", "grid_rows",
        "lock_console", "pause_on_exit",
        "max_nuclei", "territory_per_nucleus", "absorb_radius",
        "ball_spawn_probability", "base_ball_energy", "grid_cell_size",
        "ball_damping", "radial_k", "tangential_k", "near_repel_radius",
        "ball_ball_repel_radius", "ball_ball_repel_k",
        "ball_same_owner_attract_radius", "ball_same_owner_attract_k",
        "nucleus_damping", "attack_k", "avoid_k", "combat_rate",
        "influence_margin", "forage_k", "forage_radius", "wander_k",
        "wander_turn_rate", "basal_cost", "speed_cost_k", "speed_cost_exponent",
        "reproduction_cost_ratio", "child_energy_ratio", "nucleus_init_energy_ratio",
        "reproduction_offset", "nucleus_min_separation", "nucleus_repel_k",
        "newborn_grace_frames",
        "ball_max_speed_shield", "ball_max_speed_worker", "ball_max_speed_scout",
        "ball_vel_range", "bounce_restitution", "follow_boost", "follow_coupling",
        "detach_radius", "ball_loss_cost",
        "affinity_init", "orbit_radius_init", "absorb_preference_init",
        "repel_strength_init", "attack_range_init", "attack_strength_init",
        "avoid_range_init", "avoid_strength_init", "max_speed_init",
        "energy_threshold_init", "mutation_rate_init",
        "affinity_clamp_min", "orbit_radius_clamp", "absorb_preference_clamp",
        "repel_strength_clamp", "attack_range_clamp", "attack_strength_clamp",
        "avoid_range_clamp", "avoid_strength_clamp", "max_speed_clamp",
        "energy_threshold_clamp", "mutation_rate_clamp",
    };
    bool ok = true;
    for (const char* k : envKeys) {
        if (!find(k)) {
            std::cerr << "[schema-check] 缺失键: " << k << "\n";
            ok = false;
        }
    }
    return ok;
}

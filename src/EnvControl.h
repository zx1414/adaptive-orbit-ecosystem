#pragma once
// 玩家环境调控的单一事实来源（v2.2.0）。
// 设计目标：
//   - 4 条环境轴 × 5 档快调；每条轴另有细调键（直接复用 ParamSchema 键）。
//   - 玩家干预消耗「影响力」，事件有费用/冷却，形成有代价的取舍。
//   - 本文件不依赖 World.h，避免循环包含；需要 WorldConfig 的函数在 .cpp 中实现。
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct WorldConfig;  // 前向声明（applyAxisLevel/applyFine/applyPresetConfig 使用）

enum class EnvAxis : int { Light = 0, Food = 1, Pressure = 2, Producer = 3 };
constexpr int kEnvAxisCount = 4;

enum class EnvEventKind : int { Harvest = 0, Winter = 1, Plague = 2, Bloom = 3 };
constexpr int kEnvEventCount = 4;

// 一档快调：axis 的等级 = 一组 schema 键值。
struct EnvAxisLevel {
    const char* label = "";
    std::map<std::string, double> values;  // ParamSchema 键 -> 目标值
};

struct EnvFineKnob {
    const char* key = "";
    const char* label = "";
    const char* unit = "";
};

struct EnvAxisDef {
    EnvAxis axis;
    const char* id;    // "light" / "food" / "pressure" / "producer"
    const char* name;  // 中文轴名
    const char* hint;  // 涌现作用说明（UI 直接显示）
    std::array<EnvAxisLevel, 5> levels;
    std::vector<EnvFineKnob> fine;
};

struct EnvEventDef {
    EnvEventKind kind;
    const char* id;    // "harvest" / "winter" / "plague" / "bloom"
    const char* name;
    const char* desc;
    double cost;
    int cooldownFrames;
};

struct EnvPresetDef {
    const char* id;
    const char* name;
    const char* desc;
    std::array<int, kEnvAxisCount> levels;  // Light, Food, Pressure, Producer
    bool cyanoOn;
    int cyanoTarget;  // 预设后的蓝细菌节点目标（小于现有数量时不强删）
};

// 跨帧、可存档的玩家环境状态。
struct EnvState {
    double influence = 50.0;                     // 当前影响力
    std::array<int, kEnvEventCount> cooldown{};  // 剩余冷却帧数，按 EnvEventKind 下标
    int interventions = 0;                       // 本局玩家干预计数
    int peakAlive = 0;                           // 本局存活核峰值（结算展示）
};

struct EnvApplyResult {
    int applied = 0;
    std::vector<std::pair<std::string, std::string>> rejected;  // key -> 中文原因
};

class EnvControl {
public:
    static constexpr double kInfluenceMax = 100.0;
    static constexpr double kInfluenceStart = 50.0;
    static constexpr double kInfluenceRegenPerFrame = 0.08;
    static constexpr double kQuickCost = 1.0;
    static constexpr double kFineCost = 3.0;
    static constexpr double kPresetCost = 5.0;
    static constexpr double kCyanoToggleCost = 10.0;

    static const std::vector<EnvAxisDef>& axes();
    static const std::vector<EnvEventDef>& events();
    static const std::vector<EnvPresetDef>& presets();

    static const EnvAxisDef* axis(const std::string& id);
    static const EnvEventDef* event(const std::string& id);
    static const EnvPresetDef* preset(const std::string& id);

    // 当前 config 精确匹配的档位；返回 -1 表示“自定义”。
    static int axisLevelOf(const WorldConfig& cfg, EnvAxis a);
    static bool applyAxisLevel(WorldConfig& cfg, EnvAxis a, int level, std::string& err);
    // 细调：key 必须存在于 ParamSchema；按 schema 的 min/max 钳制后写入。
    static bool applyFine(WorldConfig& cfg, const std::string& key,
                          const std::string& value, std::string& err);
    static bool applyPresetConfig(WorldConfig& cfg, const EnvPresetDef& p, std::string& err);

    // GET /env 的 JSON（含轴/档位/细调/事件/预设/影响力/蓝细菌状态）。
    static std::string toJson(const EnvState& st, const WorldConfig& cfg,
                              int cyanoNodes, int cyanoLinks, bool cyanoEnabled,
                              bool running, bool paused);
};

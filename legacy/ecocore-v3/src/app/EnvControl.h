// agentsim - app/EnvControl.h
// R1-C：实时环境调控的单一事实来源（四轴五档 + 细调 + 事件 + 预设 + 蓝细菌开关）。
//
// 【V2-SPEC-AMEND-1 已生效】影响力系统整体删除：
//   - 无 kInfluenceMax/kInfluenceStart/kInfluenceRegenPerFrame，
//     无 kQuickCost/kFineCost/kPresetCost/kCyanoToggleCost。
//   - 调控免费、无限、无冷却；换档/细调/事件/预设/开关全部恒成功（参数合法时）。
//   - EnvState 只留展示与幂等需要的字段；tickFrame() 保留为"帧推进"挂钩
//     （主代理倾向保留，接线层以后可在同一位置挂每帧逻辑）。
//
// 架构边界（任务卡 R1-C）：
//   - 本模块**不 include Config/Sim/Environment/Ledger**，不持有任何世界状态指针。
//     apply() 只产出 EnvChange（参数变更描述）与 EnvPendingEvent（事件效果描述），
//     由主代理把变更写到自己的 SimConfig、把事件效果走 ledger 执行。
//   - 不消耗 RNG（纯配置读写）→ 不影响同 seed 双跑确定性。
//   - 幂等所需的"当前档位"记录在 EnvState::axisLevels（控制面状态，与模拟面解耦；
//     初始全 2 = 各轴"标准"档，与 ParamSchema 默认值一致）。
#pragma once
#include <string>
#include <vector>

namespace agentsim::app {

enum class EnvAxis { Light = 0, Food = 1, Pressure = 2, Producer = 3 };
constexpr int kEnvAxisCount = 4;

enum class EnvEventKind { Harvest = 0, Winter = 1, Plague = 2, Bloom = 3 };
constexpr int kEnvEventCount = 4;

// 跨帧、可存档的调控面状态（修正 1：无影响力字段）。
struct EnvState {
    int interventions = 0;             // 玩家干预计数（幂等命中不计数），展示用
    int peakAlive = 0;                 // 本局存活核峰值（主代理每帧上报），展示用
    long long frames = 0;              // 已推进帧数（tickFrame 累加；暂停时不调即不推进）
    int axisLevels[kEnvAxisCount] = {2, 2, 2, 2};  // 四轴当前档位（0..4，初始=标准）
    bool cyanoOn = true;               // 蓝细菌网络总开关（初始=开）
};

// 一次干预要应用的参数变更（key 为 ParamSchema 键；cyano 开关的键是 cyano_enabled）。
struct EnvChange {
    std::string key;
    double value = 0.0;
};

// 事件效果描述：本模块**不直接改能量**——能量改动必须由主代理走 ledger
// （注入走 injectPrimary，扣减走 dissipate/transfer，保持审计恒等式）。
// 字段是对 spec 最小集 {perNucleus, onlyRich, richThreshold, cyanoNodesToAdd}
// 的超集扩展，多余字段供执行器精确结算：
//   skipBelow            每核能量低于它则跳过（winter=5，0 = 不跳过）
//   cyanoNodesIfDisabled 蓝细菌关闭时激活的节点数（bloom=150）
//   capPerEvent          单次事件总注入上限（harvest=2000，0 = 不封顶）
struct EnvEventEffect {
    double perNucleus = 0.0;
    bool onlyRich = false;
    double richThreshold = 0.0;
    double skipBelow = 0.0;
    int cyanoNodesToAdd = 0;
    int cyanoNodesIfDisabled = 0;
    double capPerEvent = 0.0;
};

// 一条事件命令的待执行描述：apply 成功后填充，由主代理读取并执行。
struct EnvPendingEvent {
    std::string id;            // "harvest" / "winter" / "plague" / "bloom"
    EnvEventEffect effect{};
};

// 一档快调：axis 的等级 = 一组 ParamSchema 键值。
struct EnvAxisLevel {
    std::string label;
    std::vector<std::pair<std::string, double>> values;  // 键 -> 目标值
};

// 细调滑杆（白名单来自各轴 fine 列表；是否暴露由 advanced 控制）。
struct EnvFineKnob {
    std::string key;
    std::string label;
    std::string unit;
    std::string desc;
    bool advanced = false;
};

struct EnvAxisDef {
    EnvAxis axis = EnvAxis::Light;
    std::string id;    // "light" / "food" / "pressure" / "producer"
    std::string name;  // 中文轴名
    std::string hint;  // 涌现作用说明（前端直接显示）
    std::vector<EnvAxisLevel> levels;  // 恒 5 档
    std::vector<EnvFineKnob> fine;
};

struct EnvEventDef {
    EnvEventKind kind = EnvEventKind::Harvest;
    std::string id;    // "harvest" / "winter" / "plague" / "bloom"
    std::string name;
    std::string desc;
    EnvEventEffect effect;  // 修正 1：无 cost、无冷却，效果描述即全部契约
};

struct EnvPresetDef {
    std::string id;
    std::string name;
    std::string desc;
    int levels[kEnvAxisCount] = {2, 2, 2, 2};  // Light, Food, Pressure, Producer
    bool cyanoOn = true;
    int cyanoTarget = 150;  // 蓝细菌节点目标（少于现有数量不强删）
};

class EnvControl {
public:
    static const std::vector<EnvAxisDef>& axes();
    static const std::vector<EnvEventDef>& events();
    static const std::vector<EnvPresetDef>& presets();

    static const EnvAxisDef* axis(const std::string& id);
    static const EnvEventDef* event(const std::string& id);
    static const EnvPresetDef* preset(const std::string& id);

    // key 是否在细调白名单（各轴 fine 列表的并集；运行中可热改的键才允许）。
    static bool isFineKey(const std::string& key);

    // 解析一条命令；成功返回 true（err 清空），失败返回 false（err 给中文原因，
    // 且 out / pendingEvent / st 全部不变）。
    // 命令形态：
    //   axis.light=3          四轴快调（0..4）；同档幂等成功且不计数
    //   fine.sun_base_level=1.35   细调单条（免费，计 1 次干预）；值按 schema 钳制
    //   event.harvest=1       事件（免费、无冷却）；效果写入 pendingEvent/lastEvent
    //   preset.balance=1      预设（免费）；四轴档位 + 蓝细菌开关
    //   cyano=on|off          蓝细菌开关（免费）；同态幂等成功且不计数
    // 事件**只产出描述不执行**：能量改动由主代理走 ledger。
    static bool apply(const std::string& key, const std::string& value,
                      EnvState& st, std::vector<EnvChange>& out, std::string& err,
                      EnvPendingEvent* pendingEvent = nullptr);

    // apply 的辅助读取口：最近一次成功的事件命令（没有则为空 id）。
    static const EnvPendingEvent& lastEvent();
    static void clearLastEvent();

    // 每帧推进（修正 1：无影响力可恢复；保留作帧计数挂钩）。
    // 暂停不恢复 = 暂停时不调用本函数。
    static void tickFrame(EnvState& st);

    // GET /env 的 JSON。修正 1：无 influence 字段，改放 interventions/peakAlive。
    // {"running":..,"paused":..,"interventions":N,"peakAlive":N,"frames":N,
    //  "cyanoOn":true,"cyano":{"nodes":N,"links":N},
    //  "axes":[{"id","name","hint","level","levels":[{"label","values":{}}],
    //           "fine":[{"key","label","unit","desc","advanced","type","min","max"}]}],
    //  "events":[{"id","name","desc","effect":{...}}],
    //  "presets":[{"id","name","desc","levels":[..],"cyanoOn":..,"cyanoTarget":..}]}
    static std::string toJson(const EnvState& st, bool running, bool paused,
                              int cyanoNodes, int cyanoLinks);
};

} // namespace agentsim::app

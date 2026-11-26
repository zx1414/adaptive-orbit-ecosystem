// agentsim - app/EnvControl.h
// R1-C：实时环境调控的单一事实来源（四轴五档 + 细调 + 环境事件 + 预设 + 蓝细菌开关）。
//
// 【V2-SPEC-AMEND-1 已生效】影响力系统整体删除：
//   - 无 kInfluenceMax/kInfluenceStart/kInfluenceRegenPerFrame，
//     无 kQuickCost/kFineCost/kPresetCost/kCyanoToggleCost。
//   - 调控免费、无限、无冷却；换档/细调/事件/预设/开关全部恒成功（参数合法时）。
//
// 【P0-5 玩家干预重做（2026-11-25）】普通玩法的事件从"直接改核的能量/直接批量造菌"
// 改成"有持续时间的环境变化"：
//   - 撤下 harvest（全体 +8 能量）、plague（富核 -12）、bloom（直接追加蓝细菌节点）
//     连同它们的 CLI / HTTP / RuntimeEnv / 前端入口；旧命令一律返回明确的
//     retired_event 错误码（HTTP 400），任何普通接口都没有让它们生效的路径。
//   - 只保留 winter：把光照基线压低一段时间，到期自动恢复。**触发只改环境状态**，
//     不碰任何核的能量，也不按余额挑选受罚者。
//   - 玩家基础值与事件临时偏移分开存：
//       EnvEffectState（本模块）只存"事件自己的临时偏移 + 到期帧"；
//       玩家的光照基础值由运行时（--serve 的 ServeBridge）单独持有；
//       生效值 = 玩家基础值 + 事件偏移，到期只撤偏移（玩家期间的改动继续有效）。
//   - 到期以**模拟帧**计（endTick），由主循环盖戳与撤销；不是"下次轮询才恢复"，
//     所以暂停时不会悄悄到期，也不会因为没人 GET /env 而永远不恢复。
//   - 玩家播种是独立且明确的**外部输入**（seed.cyano=N），不是自然生长，
//     也不是事件奖励；回执与 /env JSON 都带 external:true 标注。
//
// 架构边界（任务卡 R1-C）：
//   - 本模块**不 include Config/Sim/Environment/Ledger**，不持有任何世界状态指针。
//     apply() 只产出 EnvChange（参数变更描述）、EnvEffectRequest（事件请求）、
//     EnvSeedRequest（外部播种请求），由调用方执行。
//   - 不消耗 RNG（纯配置读写）→ 不影响同 seed 双跑确定性。
//
// 命令形态（POST /env 行式协议，每行 key=value）：
//   axis.light=3                四轴快调（0..4）；同档幂等成功且不计数
//   fine.sun_base_level=1.35    细调单条（免费，计 1 次干预）；值按 schema 钳制
//   event.winter=1              触发/续期寒冬（影响光照基线，到期自动恢复）
//   event.winter=0              提前撤销（只撤事件偏移，玩家基础值继续有效）
//   winter.duration=600         覆盖后续触发使用的持续帧数（1..100000）
//   seed.cyano=120              外部播种蓝细菌节点（显式外部输入，不是事件奖励）
//   preset.balance=1            预设（免费）；四轴档位 + 蓝细菌开关
//   cyano=on|off                蓝细菌开关（免费）；同态幂等成功且不计数
//   event.harvest / event.plague / event.bloom
//                               已撤下 → 明确错误（code=retired_event）
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace agentsim::app {

enum class EnvAxis { Light = 0, Food = 1, Pressure = 2, Producer = 3 };
constexpr int kEnvAxisCount = 4;

// 环境事件种类。P0-5 后普通玩法只剩 Winter。
enum class EnvEffectKind { Winter = 0 };
constexpr int kEnvEffectCount = 1;

// 一条事件命令解析出的"请求"：由调用方交给运行时执行（本模块不持有世界状态）。
struct EnvEffectRequest {
    bool cancel = false;           // true：请求提前撤销当前生效事件（只撤事件偏移）
    std::string id;                // "winter"
    double lightOffset = 0.0;      // 叠加到 sun_base_level 的偏移（负 = 变暗变冷）
    long long durationFrames = 0;  // 持续帧数（>0）
};

// 事件运行时状态：**只存事件自己的临时偏移**（玩家基础值由调用方单独保存）。
struct EnvEffectState {
    bool active = false;
    std::string id;                 // "winter"
    double lightOffset = 0.0;       // 生效值 = 玩家基础值 + 本偏移
    long long durationFrames = 0;   // 本次持续时间
    long long endTick = 0;          // 到期帧；0 = 尚未起算（等主循环第一个模拟帧盖戳）
};

// 事件快照（HTTP /env 与 POST /env 回执同形；只读展示）。
struct EnvEffectStatus {
    bool active = false;
    std::string id;
    double lightOffset = 0.0;
    long long durationFrames = 0;
    long long remainingFrames = 0;  // 触发瞬间 = durationFrames；到期 = 0
    long long endTick = 0;
    bool started = false;           // 是否已盖戳（false = 已触发、等下一模拟帧）
};

// 外部播种请求（显式外部输入；不是自然生长，也不是事件奖励）。
struct EnvSeedRequest {
    bool requested = false;
    std::string kind;   // "cyano"
    int count = 0;
};

// apply() 的附加输出（失败时 code 表明原因，供 HTTP 回执映射错误码）。
struct EnvCommandResult {
    std::string code;            // 成功时为空；失败时 "retired_event" / "unknown_command" …
    EnvEffectRequest effect;     // 非空 id 或 cancel 时调用方需执行
    bool effectRestarted = false;  // 事件在已生效状态下被续期
    EnvSeedRequest seed;
};

// 跨帧、可存档的调控面状态（修正 1：无影响力字段）。
struct EnvState {
    int interventions = 0;             // 玩家干预计数（幂等命中不计数），展示用
    int peakAlive = 0;                 // 本局存活核峰值（主代理每帧上报），展示用
    long long frames = 0;              // 已推进帧数（tickFrame 累加；暂停时不调即不推进）
    int axisLevels[kEnvAxisCount] = {2, 2, 2, 2};  // 四轴当前档位（0..4，初始=标准）
    bool cyanoOn = true;               // 蓝细菌网络总开关（初始=开）
    long long effectDurationOverride = 0;  // winter.duration 覆盖值（0 = 用表里默认）
    int seedsRequested = 0;            // 外部播种请求累计（节点数），展示用
};

// 一次干预要应用的参数变更（key 为 ParamSchema 键；cyano 开关的键是 cyano_enabled）。
struct EnvChange {
    std::string key;
    double value = 0.0;
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

// 环境事件定义：只描述"环境怎么变、变多久"，不含任何能量奖惩字段。
struct EnvEffectDef {
    EnvEffectKind kind = EnvEffectKind::Winter;
    std::string id;    // "winter"
    std::string name;  // 中文名
    std::string desc;  // 前端直接显示（含持续时间与幅度）
    double lightOffset = 0.0;      // 叠加到 sun_base_level
    long long durationFrames = 0;  // 默认持续帧数
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
    static const std::vector<EnvEffectDef>& effects();
    static const std::vector<EnvPresetDef>& presets();

    static const EnvAxisDef* axis(const std::string& id);
    static const EnvEffectDef* effect(const std::string& id);
    static const EnvPresetDef* preset(const std::string& id);

    // key 是否在细调白名单（各轴 fine 列表的并集；运行中可热改的键才允许）。
    static bool isFineKey(const std::string& key);

    // 解析一条命令；成功返回 true（err 清空），失败返回 false（err 给中文原因，
    // code 给机器可读错误码；out / cmd 的全部字段保持不变）。
    static bool apply(const std::string& key, const std::string& value,
                      EnvState& st, std::vector<EnvChange>& out, std::string& err,
                      EnvCommandResult* cmd = nullptr);

    // ---- 事件时钟（纯函数；主循环与单测共用，不碰世界状态、不消耗 RNG）----
    // beginEffect：按请求写入事件状态（endTick 归零 = 未起算，等主循环盖戳）。
    static void beginEffect(EnvEffectState& eff, const EnvEffectRequest& req);
    // clearEffect：只撤事件自己的偏移；返回原本是否生效。
    static bool clearEffect(EnvEffectState& eff);
    // stampEffectStart：首次被主循环观测到 → endTick = tick + durationFrames。
    static bool stampEffectStart(EnvEffectState& eff, long long tick);
    // expireEffect：tick >= endTick → 撤销（返回 true）。
    static bool expireEffect(EnvEffectState& eff, long long tick);
    // effectStatus：给 JSON / 回执用的只读快照（remainingFrames 随 tick 递减）。
    static EnvEffectStatus effectStatus(const EnvEffectState& eff, long long tick);
    // 生效值 = 玩家基础值 + 事件偏移（下限 0：光照基线永不为负）。
    // **只撤偏移**的语义靠调用方把玩家基础值单独存着，本函数不写任何状态。
    static double effectiveSunBase(double playerBase, const EnvEffectState& eff);

    // 每帧推进（修正 1：无影响力可恢复；保留作帧计数挂钩）。
    // 暂停不恢复 = 暂停时不调用本函数。
    static void tickFrame(EnvState& st);

    // GET /env 的 JSON。太阳值 = 生效值/玩家基础值/事件偏移三个字段并列，
    // 让"触发瞬间、后续模拟步、到期时刻"都能在同一接口读到当前环境状态。
    // tick = 当前模拟帧（事件剩余时长以它为基准；与 /env/event 的 tick 同源）。
    static std::string toJson(const EnvState& st, bool running, bool paused,
                              int cyanoNodes, int cyanoLinks,
                              const EnvEffectStatus& effect,
                              double sunBaseEffective, double sunBasePlayer,
                              long long tick = 0);
};

} // namespace agentsim::app

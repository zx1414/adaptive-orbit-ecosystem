// agentsim - app/EnvControl.cpp
// R1-C：四轴五档 / 细调 / 环境事件 / 预设 / 蓝细菌开关的数据表 + 命令解析 + GET /env JSON。
//
// 四轴档位数值：参照 v2.3.1 的档位结构，但换算到 agentsim 的小世界（1024x1024，
// R10 后）与"蓝细菌是唯一生产者"的经济学（V2-SPEC §E / GOAL-ECOLOGY）：
//   - food 轴不再有"人工撒食"档（Food 实体已删），档位改调自由球能量经济学。
//   - 数值只写在本表（前端从 GET /env 读取，不得复制常量）。
//
// 修正 1：无影响力——apply 全部免费；幂等（同档/同开关态）成功且不计干预数。
// 确定性红线：本文件任何函数都不消耗 RNG。
//
// 【P0-5 玩家干预重做】见 EnvControl.h 头部。摘要：
//   - 事件表只剩 winter（降低光照基线，有持续时间），没有 perNucleus/onlyRich/
//     cyanoNodesToAdd 一类能量或实体字段 —— 结构上就不可能再直接改核能量。
//   - harvest/plague/bloom 三个旧命令名在 apply() 里显式拒绝（retired_event），
//     不是"未知命令"而是"已撤下的旧事件"，回执里写明替代路径。
//   - 玩家播种走独立命令 seed.cyano=N，回执与 JSON 标 external:true。
#include "EnvControl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "ParamSchema.h"

namespace agentsim::app {
namespace {

// ==================== 四轴五档数据表 ====================

EnvAxisLevel mkLevel(const char* label, std::vector<std::pair<std::string, double>> v) {
    EnvAxisLevel l;
    l.label = label;
    l.values = std::move(v);
    return l;
}

EnvFineKnob mkKnob(const char* key, const char* label, const char* unit,
                   const char* desc, bool advanced = false) {
    EnvFineKnob k;
    k.key = key;
    k.label = label;
    k.unit = unit;
    k.desc = desc;
    k.advanced = advanced;
    return k;
}

std::vector<EnvAxisDef> buildAxes() {
    std::vector<EnvAxisDef> v;

    // ---- 光照：sun_base_level / sun_day_amplitude 二键组合 ----
    {
        EnvAxisDef a;
        a.axis = EnvAxis::Light;
        a.id = "light";
        a.name = "光照";
        a.hint = "光照决定生产者产能：极夜几乎停摆，灼热光合旺盛但昼夜波动更剧烈";
        a.levels = {
            mkLevel("极夜", {{"sun_base_level", 0.05}, {"sun_day_amplitude", 0.05}}),
            mkLevel("阴天", {{"sun_base_level", 0.4}, {"sun_day_amplitude", 0.2}}),
            mkLevel("标准", {{"sun_base_level", 1.0}, {"sun_day_amplitude", 0.5}}),
            mkLevel("晴朗", {{"sun_base_level", 1.8}, {"sun_day_amplitude", 0.7}}),
            mkLevel("灼热", {{"sun_base_level", 3.0}, {"sun_day_amplitude", 0.9}}),
        };
        a.fine = {
            mkKnob("sun_base_level", "基准强度", "", "整体光照强弱"),
            mkKnob("sun_day_amplitude", "昼夜振幅", "", "白天/黑夜差异"),
            mkKnob("sun_day_period_frames", "昼夜周期", "帧", "越小昼夜切换越快", true),
            mkKnob("sun_vertical_gradient", "垂直梯度", "", "上下光照差异", true),
            mkKnob("sun_noise_amplitude", "局部扰动", "", "光斑强弱", true),
            mkKnob("cyano_light_half_saturation", "半饱和光强", "", "光合对光的敏感度", true),
        };
        v.push_back(std::move(a));
    }

    // ---- 食物：自由球能量经济学（蓝细菌是唯一生产者，无人工撒食档）----
    {
        EnvAxisDef a;
        a.axis = EnvAxis::Food;
        a.id = "food";
        a.name = "食物";
        a.hint = "自由球能量决定捕食收益：贫瘠逼迫节食，泛滥让竞争从觅食转向繁殖";
        a.levels = {
            mkLevel("贫瘠", {{"ball_energy", 3.0}}),
            mkLevel("稀少", {{"ball_energy", 6.0}}),
            mkLevel("标准", {{"ball_energy", 10.0}}),
            mkLevel("丰饶", {{"ball_energy", 18.0}}),
            mkLevel("泛滥", {{"ball_energy", 30.0}}),
        };
        a.fine = {
            mkKnob("ball_energy", "单球能量", "", "吃掉一个自由球获得的能量"),
            mkKnob("ball_initial_energy", "新生球能量", "", "新产自由球的初始能量", true),
            mkKnob("ball_loss_cost", "丢球代价", "", "球脱附/被抢时对原主的扣能", true),
            mkKnob("detach_radius", "脱附半径", "", "球距核多远算丢", true),
        };
        v.push_back(std::move(a));
    }

    // ---- 压力：代谢/成本族 ----
    {
        EnvAxisDef a;
        a.axis = EnvAxis::Pressure;
        a.id = "pressure";
        a.name = "压力";
        a.hint = "代谢压力决定生存成本：安逸适合扩张，严酷筛选高效节能的个体";
        a.levels = {
            mkLevel("安逸", {{"basal_cost", 0.002}}),
            mkLevel("温和", {{"basal_cost", 0.006}}),
            mkLevel("标准", {{"basal_cost", 0.01}}),
            mkLevel("严酷", {{"basal_cost", 0.02}}),
            mkLevel("炼狱", {{"basal_cost", 0.04}}),
        };
        a.fine = {
            mkKnob("basal_cost", "基础代谢", "", "每帧固定能量消耗"),
            mkKnob("speed_cost_k", "速度代价", "", "跑得快的额外消耗"),
            mkKnob("speed_cost_exponent", "速度代价指数", "", "越大高速惩罚越狠", true),
            mkKnob("reproduction_cost_ratio", "繁殖成本", "", "父核繁殖时扣除比例", true),
            mkKnob("child_energy_ratio", "子代能量", "", "子核出生带走的能量比例", true),
            mkKnob("mutation_coefficient", "变异系数", "", "子代变异幅度倍数（进化速度）", true),
        };
        v.push_back(std::move(a));
    }

    // ---- 生产者：光合速率族 ----
    {
        EnvAxisDef a;
        a.axis = EnvAxis::Producer;
        a.id = "producer";
        a.name = "生产者";
        a.hint = "生产者规模决定第一营养级产能：繁盛时捕食者获利，爆发时节点竞争更激烈";
        a.levels = {
            mkLevel("停滞", {{"cyano_photosynthesis_rate", 0.15}}),
            mkLevel("稀疏", {{"cyano_photosynthesis_rate", 0.4}}),
            mkLevel("标准", {{"cyano_photosynthesis_rate", 1.0}}),
            mkLevel("繁盛", {{"cyano_photosynthesis_rate", 2.0}}),
            mkLevel("爆发", {{"cyano_photosynthesis_rate", 4.0}}),
        };
        a.fine = {
            mkKnob("cyano_photosynthesis_rate", "光合速率", "", "节点积攒能量的速度"),
            mkKnob("cyano_metabolism_rate", "代谢消耗", "", "节点每帧消耗能量"),
            mkKnob("cyano_reproduction_probability", "繁殖概率", "", "节点分裂出新节点概率", true),
            mkKnob("cyano_reproduction_offset", "子代距离", "", "子节点离亲节点多远", true),
            mkKnob("cyano_transfer_rate", "传输速率", "", "富节点补贴穷节点速度", true),
            mkKnob("cyano_predation_radius", "捕食半径", "", "核吞节点的判定半径", true),
            mkKnob("cyano_min_node_separation", "最小间距", "", "过近则取消繁殖", true),
        };
        v.push_back(std::move(a));
    }

    return v;
}

// ==================== 环境事件表（P0-5：只有 winter，且只改环境）====================
// 数值标定说明（写在这里 = 唯一权威，前端从 GET /env 读，不复制常量）：
//   - lightOffset = -0.35：在"标准"档（sun_base_level=1.0）下把基线压到 0.65，
//     相当于阴天档与标准档之间。刻意不压到 0：玩家已经调到极夜（0.05）时再压
//     0.35 会把光照彻底归零、让"寒冬"退化成"停光"，那就不是环境变化而是删除生产者。
//   - durationFrames = 600：默认昼夜周期（sun_day_period_frames schema 默认 1000，
//     实际 Config 默认 628）量级的"一个季节"。600 帧 ≈ 1~2 个昼夜周期，
//     足够让光合产出落差显现，又不会长到玩家等不到恢复。
//   - 两者都可在运行期覆盖：winter.duration=N 改后续触发的持续时间。
std::vector<EnvEffectDef> buildEffects() {
    std::vector<EnvEffectDef> v;
    EnvEffectDef winter;
    winter.kind = EnvEffectKind::Winter;
    winter.id = "winter";
    winter.name = "寒冬";
    winter.desc = "光照基线 -0.35，持续 600 模拟帧后自动恢复到玩家设定值（不直接改任何核的能量）";
    winter.lightOffset = -0.35;
    winter.durationFrames = 600;
    v.push_back(std::move(winter));
    return v;
}

std::vector<EnvPresetDef> buildPresets() {
    std::vector<EnvPresetDef> v;
    v.push_back({"balance", "平衡", "四轴全部标准 + 蓝细菌 150 节点：适合观察基线生态",
                 {2, 2, 2, 2}, true, 150});
    v.push_back({"bounty", "丰饶", "晴朗光照 + 食物丰饶 + 压力温和 + 生产者繁盛：种群快速扩张",
                 {3, 3, 1, 3}, true, 150});
    v.push_back({"harsh", "严酷", "阴天光照 + 食物稀少 + 压力严酷 + 生产者稀疏：生存筛选",
                 {1, 1, 3, 1}, true, 150});
    // 预设 id 从 "bloom" 改成 "surge"：P0-5 撤下的事件里也有一个 bloom（藻华），
    // 保留同名预设会让"旧事件是否还能绕过"的检查与玩家理解都含糊 ——
    // preset.surge 只是四轴档位组合（灼热光照 + 生产者爆发），与事件无关。
    v.push_back({"surge", "爆发", "灼热光照 + 压力严酷 + 生产者爆发 + 蓝细菌 400 节点",
                 {4, 2, 3, 4}, true, 400});
    return v;
}

const std::vector<EnvAxisDef>& axisTable() {
    static const std::vector<EnvAxisDef> table = buildAxes();
    return table;
}

// 数值 → ParamSchema 文本（%.17g：double 往返无损）。
std::string fmtNum(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

// 严格数值解析：整个字符串必须是一个有限数（拒绝 "3abc" / 空串 / inf）。
bool parseNum(const std::string& text, double& out) {
    std::string t;
    size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return false;
    size_t b = text.find_last_not_of(" \t\r\n");
    t = text.substr(a, b - a + 1);
    if (t.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == nullptr || static_cast<size_t>(end - t.c_str()) != t.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

// JSON 数字：%.10g 足够表内字面量；整数补 ".0"。
std::string envJsonNum(double v) {
    if (!std::isfinite(v)) return "0.0";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    std::string s(buf);
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    return s;
}

std::string envJsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// 细调值按 schema 钳制（越界收敛到 min/max，不拒绝——实时滑杆语义）。
double clampToSchema(const ParamDef& d, double v) {
    if (v < d.min) v = d.min;
    if (v > d.max) v = d.max;
    if (d.type == ParamDef::Type::Int) v = std::round(v);
    if (d.type == ParamDef::Type::Bool) v = v >= 0.5 ? 1.0 : 0.0;
    return v;
}

// 已撤下的事件命令 → 明确错误（不是"未知命令"：调用方需要知道这条命令曾经存在、
// 现在被规则改动移除，以及替代路径是什么）。
struct RetiredEvent {
    const char* id;
    const char* reason;
};
const RetiredEvent kRetiredEvents[] = {
    {"harvest", "「丰收」已撤下：普通玩法不再直接给核注入能量"},
    {"plague", "「瘟疫」已撤下：普通玩法不再按核的能量余额选择性扣减"},
    {"bloom", "「藻华」已撤下：普通玩法不再直接追加蓝细菌节点（生产靠光照生长与分裂）"},
};

const RetiredEvent* retiredEvent(const std::string& id) {
    for (const RetiredEvent& r : kRetiredEvents) {
        if (id == r.id) return &r;
    }
    return nullptr;
}

// 小写化（cyano 开关取值容错）。
std::string lowerAscii(const std::string& s) {
    std::string v;
    v.reserve(s.size());
    for (char c : s) {
        v.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }
    return v;
}

} // namespace

const std::vector<EnvAxisDef>& EnvControl::axes() { return axisTable(); }

const std::vector<EnvEffectDef>& EnvControl::effects() {
    static const std::vector<EnvEffectDef> table = buildEffects();
    return table;
}

const std::vector<EnvPresetDef>& EnvControl::presets() {
    static const std::vector<EnvPresetDef> table = buildPresets();
    return table;
}

const EnvAxisDef* EnvControl::axis(const std::string& id) {
    for (const EnvAxisDef& a : axes()) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

const EnvEffectDef* EnvControl::effect(const std::string& id) {
    for (const EnvEffectDef& e : effects()) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const EnvPresetDef* EnvControl::preset(const std::string& id) {
    for (const EnvPresetDef& p : presets()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

bool EnvControl::isFineKey(const std::string& key) {
    for (const EnvAxisDef& a : axes()) {
        for (const EnvFineKnob& k : a.fine) {
            if (k.key == key) return true;
        }
    }
    return false;
}

// ---------------- 事件时钟（纯函数）----------------

void EnvControl::beginEffect(EnvEffectState& eff, const EnvEffectRequest& req) {
    eff.active = true;
    eff.id = req.id;
    eff.lightOffset = req.lightOffset;
    eff.durationFrames = req.durationFrames > 0 ? req.durationFrames : 1;
    // endTick 归零 = 尚未起算。主循环在第一个模拟帧用 stampEffectStart 盖戳，
    // 保证"触发瞬间 → 后续模拟步 → 到期"三段都能被同一套帧号解释；
    // 暂停时主循环不推进，也就不会把持续时间耗在暂停里。
    eff.endTick = 0;
}

bool EnvControl::clearEffect(EnvEffectState& eff) {
    const bool was = eff.active;
    eff.active = false;
    eff.id.clear();
    eff.lightOffset = 0.0;
    eff.durationFrames = 0;
    eff.endTick = 0;
    return was;
}

bool EnvControl::stampEffectStart(EnvEffectState& eff, long long tick) {
    if (!eff.active) return false;
    if (eff.endTick > 0) return false;
    eff.endTick = tick + eff.durationFrames;
    return true;
}

bool EnvControl::expireEffect(EnvEffectState& eff, long long tick) {
    if (!eff.active) return false;
    if (eff.endTick <= 0) return false;  // 还没盖戳：不算到期
    if (tick < eff.endTick) return false;
    clearEffect(eff);
    return true;
}

EnvEffectStatus EnvControl::effectStatus(const EnvEffectState& eff, long long tick) {
    EnvEffectStatus s;
    s.active = eff.active;
    s.id = eff.id;
    s.lightOffset = eff.lightOffset;
    s.durationFrames = eff.durationFrames;
    s.endTick = eff.endTick;
    s.started = eff.active && eff.endTick > 0;
    if (!eff.active) {
        s.remainingFrames = 0;
    } else if (eff.endTick <= 0) {
        // 已触发但主循环还没盖戳：剩余 = 完整持续帧数（回执立刻可见）
        s.remainingFrames = eff.durationFrames;
    } else {
        const long long rem = eff.endTick - tick;
        s.remainingFrames = rem > 0 ? rem : 0;
    }
    return s;
}

// 生效值 = 玩家基础值 + 事件偏移（下限 0：光照基线永不为负）。
double EnvControl::effectiveSunBase(double playerBase, const EnvEffectState& eff) {
    const double v = playerBase + (eff.active ? eff.lightOffset : 0.0);
    return v > 0.0 ? v : 0.0;
}

// ---------------- 命令解析 ----------------

bool EnvControl::apply(const std::string& key, const std::string& value,
                       EnvState& st, std::vector<EnvChange>& out, std::string& err,
                       EnvCommandResult* cmd) {
    out.clear();
    err.clear();
    if (cmd != nullptr) *cmd = EnvCommandResult{};

    // ---- 蓝细菌开关：cyano=on/off（免费；同态幂等成功且不计数）----
    if (key == "cyano") {
        const std::string v = lowerAscii(value);
        const bool wantOn = (v == "on" || v == "true" || v == "1" || v == "yes");
        const bool wantOff = (v == "off" || v == "false" || v == "0" || v == "no");
        if (!wantOn && !wantOff) {
            err = "cyano 取值只能是 on/off: " + value;
            if (cmd != nullptr) cmd->code = "bad_value";
            return false;
        }
        if (st.cyanoOn == wantOn) {
            return true;  // 幂等：不计干预，不产出变更
        }
        st.cyanoOn = wantOn;
        ++st.interventions;
        out.push_back({"cyano_enabled", wantOn ? 1.0 : 0.0});
        return true;
    }

    // ---- 事件持续帧数覆盖：winter.duration=N（免费，仅改后续触发的时长）----
    if (key == "winter.duration") {
        double v = 0.0;
        if (!parseNum(value, v)) {
            err = "持续时间必须是数字: " + key + " = " + value;
            if (cmd != nullptr) cmd->code = "bad_value";
            return false;
        }
        if (v < 1.0 || v > 100000.0) {
            err = "持续时间越界（1..100000 帧）: " + value;
            if (cmd != nullptr) cmd->code = "out_of_range";
            return false;
        }
        const long long frames = static_cast<long long>(std::llround(v));
        if (st.effectDurationOverride == frames) {
            return true;  // 幂等
        }
        st.effectDurationOverride = frames;
        ++st.interventions;
        return true;
    }

    // ---- 事件：event.<id>=1（触发/续期）；event.<id>=0（提前撤销）----
    if (key.rfind("event.", 0) == 0) {
        const std::string id = key.substr(6);
        if (const RetiredEvent* r = retiredEvent(id)) {
            err = std::string(r->reason) + "（旧命令 event." + id +
                  " 不再生效；当前可用的环境事件只有 event.winter）";
            if (cmd != nullptr) cmd->code = "retired_event";
            return false;
        }
        const EnvEffectDef* e = effect(id);
        if (e == nullptr) {
            err = "未知事件: " + id + "（当前可用的环境事件只有 winter）";
            if (cmd != nullptr) cmd->code = "unknown_event";
            return false;
        }
        double v = 0.0;
        if (!parseNum(value, v)) {
            err = "事件取值必须是数字（1=触发，0=撤销）: " + key + " = " + value;
            if (cmd != nullptr) cmd->code = "bad_value";
            return false;
        }
        ++st.interventions;
        if (cmd == nullptr) return true;
        if (v == 0.0) {
            cmd->effect.cancel = true;
            cmd->effect.id = e->id;
            return true;
        }
        cmd->effect.cancel = false;
        cmd->effect.id = e->id;
        cmd->effect.lightOffset = e->lightOffset;
        cmd->effect.durationFrames = st.effectDurationOverride > 0
                                         ? st.effectDurationOverride
                                         : e->durationFrames;
        return true;
    }

    // ---- 外部播种：seed.cyano=N（显式外部输入，不是事件奖励，不是自然生长）----
    if (key.rfind("seed.", 0) == 0) {
        const std::string kind = key.substr(5);
        double v = 0.0;
        if (!parseNum(value, v)) {
            err = "播种数量必须是数字: " + key + " = " + value;
            if (cmd != nullptr) cmd->code = "bad_value";
            return false;
        }
        if (kind != "cyano") {
            err = "未知播种目标: " + kind + "（目前只支持 seed.cyano=<节点数>）";
            if (cmd != nullptr) cmd->code = "unknown_seed";
            return false;
        }
        if (v < 1.0 || v > 4096.0) {
            err = "播种数量越界（1..4096 个节点）: " + value;
            if (cmd != nullptr) cmd->code = "out_of_range";
            return false;
        }
        const int n = static_cast<int>(std::llround(v));
        ++st.interventions;
        st.seedsRequested += n;
        if (cmd != nullptr) {
            cmd->seed.requested = true;
            cmd->seed.kind = "cyano";
            cmd->seed.count = n;
        }
        return true;
    }

    // ---- 预设：preset.<id>=1（免费；四轴档位 + 蓝细菌开关）----
    if (key.rfind("preset.", 0) == 0) {
        const std::string id = key.substr(7);
        const EnvPresetDef* p = preset(id);
        if (p == nullptr) {
            err = "未知预设: " + id;
            if (cmd != nullptr) cmd->code = "unknown_preset";
            return false;
        }
        double v = 0.0;
        if (!parseNum(value, v)) {
            err = "预设取值必须是数字: " + key + " = " + value;
            if (cmd != nullptr) cmd->code = "bad_value";
            return false;
        }
        for (int i = 0; i < kEnvAxisCount; ++i) {
            const EnvAxisDef& a = axes()[static_cast<size_t>(i)];
            const int level = p->levels[i];
            if (level < 0 || level >= static_cast<int>(a.levels.size())) {
                err = "预设档位越界: " + p->id;
                if (cmd != nullptr) cmd->code = "out_of_range";
                return false;
            }
            st.axisLevels[i] = level;
            for (const auto& kv : a.levels[static_cast<size_t>(level)].values) {
                out.push_back({kv.first, kv.second});
            }
        }
        // 预设的蓝细菌开关：同态不重复产出变更；异态计入同一笔干预。
        if (st.cyanoOn != p->cyanoOn) {
            st.cyanoOn = p->cyanoOn;
            out.push_back({"cyano_enabled", p->cyanoOn ? 1.0 : 0.0});
        }
        ++st.interventions;
        return true;
    }

    // ---- 四轴快调：axis.<id>=<0..4>（免费；同档幂等成功且不计数）----
    if (key.rfind("axis.", 0) == 0) {
        const std::string id = key.substr(5);
        const EnvAxisDef* a = axis(id);
        if (a == nullptr) {
            err = "未知环境轴: " + id;
            if (cmd != nullptr) cmd->code = "unknown_axis";
            return false;
        }
        double levelF = 0.0;
        if (!parseNum(value, levelF)) {
            err = "档位必须是数字: " + key + " = " + value;
            if (cmd != nullptr) cmd->code = "bad_value";
            return false;
        }
        const int level = static_cast<int>(levelF);
        if (level < 0 || level >= static_cast<int>(a->levels.size())) {
            err = "档位越界（0..4）: " + std::to_string(level);
            if (cmd != nullptr) cmd->code = "out_of_range";
            return false;
        }
        const int axisIdx = static_cast<int>(a->axis);
        if (st.axisLevels[axisIdx] == level) {
            return true;  // 幂等：无变化不计干预
        }
        st.axisLevels[axisIdx] = level;
        ++st.interventions;
        for (const auto& kv : a->levels[static_cast<size_t>(level)].values) {
            out.push_back({kv.first, kv.second});
        }
        return true;
    }

    // ---- 细调：fine.<key>=<value>（免费；单条即可，每次计 1 次干预）----
    if (key.rfind("fine.", 0) == 0) {
        const std::string pkey = key.substr(5);
        if (!isFineKey(pkey)) {
            err = "该参数不支持实时细调: " + pkey;
            if (cmd != nullptr) cmd->code = "not_fine_key";
            return false;
        }
        const ParamDef* d = ParamSchema::find(pkey);
        if (d == nullptr) {
            err = "未知参数键: " + pkey;
            if (cmd != nullptr) cmd->code = "unknown_key";
            return false;
        }
        double v = 0.0;
        if (!parseNum(value, v)) {
            err = "数值非法: " + pkey + " = " + value;
            if (cmd != nullptr) cmd->code = "bad_value";
            return false;
        }
        ++st.interventions;
        out.push_back({pkey, clampToSchema(*d, v)});
        return true;
    }

    err = "未知命令: " + key;
    if (cmd != nullptr) cmd->code = "unknown_command";
    return false;
}

void EnvControl::tickFrame(EnvState& st) {
    // 修正 1：无影响力可恢复。保留帧计数挂钩（暂停时不调用即不推进）。
    ++st.frames;
}

std::string EnvControl::toJson(const EnvState& st, bool running, bool paused,
                               int cyanoNodes, int cyanoLinks,
                               const EnvEffectStatus& effect,
                               double sunBaseEffective, double sunBasePlayer,
                               long long tick) {
    std::string out;
    out.reserve(8192);
    out += "{\"running\":";
    out += running ? "true" : "false";
    out += ",\"paused\":";
    out += paused ? "true" : "false";
    out += ",\"interventions\":";
    out += std::to_string(st.interventions);
    out += ",\"peakAlive\":";
    out += std::to_string(st.peakAlive);
    out += ",\"frames\":";
    out += std::to_string(st.frames);
    // 当前模拟帧（A5 的三段时间戳都以它为基准；暂停时停住不动）。
    out += ",\"tick\":";
    out += std::to_string(tick);
    out += ",\"cyanoOn\":";
    out += st.cyanoOn ? "true" : "false";
    out += ",\"cyano\":{\"nodes\":";
    out += std::to_string(cyanoNodes);
    out += ",\"links\":";
    out += std::to_string(cyanoLinks);
    out += "}";

    // ---- 太阳基线三段并列（生效值 / 玩家基础值 / 事件偏移）----
    // 触发瞬间、后续模拟步、到期时刻都能从这一处读到当前环境状态。
    out += ",\"sunBaseEffective\":";
    out += envJsonNum(sunBaseEffective);
    out += ",\"sunBasePlayer\":";
    out += envJsonNum(sunBasePlayer);
    out += ",\"sunBaseEventOffset\":";
    out += envJsonNum(effect.active ? effect.lightOffset : 0.0);

    // ---- 当前环境事件（含剩余时长）----
    out += ",\"effect\":{\"active\":";
    out += effect.active ? "true" : "false";
    out += ",\"id\":\"";
    out += envJsonEscape(effect.id);
    out += "\",\"lightOffset\":";
    out += envJsonNum(effect.lightOffset);
    out += ",\"durationFrames\":";
    out += std::to_string(effect.durationFrames);
    out += ",\"remainingFrames\":";
    out += std::to_string(effect.remainingFrames);
    out += ",\"endTick\":";
    out += std::to_string(effect.endTick);
    out += ",\"started\":";
    out += effect.started ? "true" : "false";
    out += '}';

    // ---- 外部输入（播种不是自然生长，也不是事件奖励）----
    out += ",\"externalInputs\":{\"seedsRequested\":";
    out += std::to_string(st.seedsRequested);
    out += "}";

    // ---- axes ----
    out += ",\"axes\":[";
    for (size_t ai = 0; ai < axes().size(); ++ai) {
        const EnvAxisDef& a = axes()[ai];
        if (ai > 0) out += ',';
        out += "{\"id\":\"";
        out += envJsonEscape(a.id);
        out += "\",\"name\":\"";
        out += envJsonEscape(a.name);
        out += "\",\"hint\":\"";
        out += envJsonEscape(a.hint);
        out += "\",\"level\":";
        out += std::to_string(st.axisLevels[a.axis == EnvAxis::Light     ? 0
                                                   : a.axis == EnvAxis::Food ? 1
                                                   : a.axis == EnvAxis::Pressure ? 2
                                                                                 : 3]);
        out += ",\"levels\":[";
        for (size_t lv = 0; lv < a.levels.size(); ++lv) {
            if (lv > 0) out += ',';
            out += "{\"label\":\"";
            out += envJsonEscape(a.levels[lv].label);
            out += "\",\"values\":{";
            for (size_t vi = 0; vi < a.levels[lv].values.size(); ++vi) {
                if (vi > 0) out += ',';
                out += '"';
                out += envJsonEscape(a.levels[lv].values[vi].first);
                out += "\":";
                out += envJsonNum(a.levels[lv].values[vi].second);
            }
            out += "}}";
        }
        out += "],\"fine\":[";
        for (size_t fi = 0; fi < a.fine.size(); ++fi) {
            if (fi > 0) out += ',';
            const EnvFineKnob& k = a.fine[fi];
            const ParamDef* d = ParamSchema::find(k.key);
            out += "{\"key\":\"";
            out += envJsonEscape(k.key);
            out += "\",\"label\":\"";
            out += envJsonEscape(k.label);
            out += "\",\"unit\":\"";
            out += envJsonEscape(k.unit);
            out += "\",\"desc\":\"";
            out += envJsonEscape(k.desc);
            out += "\",\"advanced\":";
            out += k.advanced ? "true" : "false";
            out += ",\"type\":\"";
            out += (d == nullptr ? "double"
                                 : d->type == ParamDef::Type::Int ? "int"
                                   : d->type == ParamDef::Type::Bool ? "bool" : "double");
            out += "\",\"min\":";
            out += envJsonNum(d == nullptr ? 0.0 : d->min);
            out += ",\"max\":";
            out += envJsonNum(d == nullptr ? 0.0 : d->max);
            out += '}';
        }
        out += "]}";
    }
    out += "]";

    // ---- events（P0-5：环境事件，只有 winter；无任何能量/实体字段）----
    out += ",\"events\":[";
    for (size_t ei = 0; ei < effects().size(); ++ei) {
        const EnvEffectDef& e = effects()[ei];
        if (ei > 0) out += ',';
        out += "{\"id\":\"";
        out += envJsonEscape(e.id);
        out += "\",\"name\":\"";
        out += envJsonEscape(e.name);
        out += "\",\"desc\":\"";
        out += envJsonEscape(e.desc);
        out += "\",\"kind\":\"environment\",\"external\":false,\"effect\":{\"lightOffset\":";
        out += envJsonNum(e.lightOffset);
        out += ",\"durationFrames\":";
        out += std::to_string(e.durationFrames);
        out += "}}";
    }
    out += "]";

    // ---- 已撤下的旧命令（前端据此不渲染按钮，脚本可读到迁移说明）----
    out += ",\"retiredEvents\":[";
    for (size_t ri = 0; ri < sizeof(kRetiredEvents) / sizeof(kRetiredEvents[0]); ++ri) {
        if (ri > 0) out += ',';
        out += "{\"id\":\"";
        out += envJsonEscape(kRetiredEvents[ri].id);
        out += "\",\"reason\":\"";
        out += envJsonEscape(kRetiredEvents[ri].reason);
        out += "\"}";
    }
    out += "]";

    // ---- 外部输入说明（玩家播种）----
    out += ",\"externalInput\":{\"seed\":\"seed.cyano=<节点数>\",\"label\":\"玩家播种（外部输入）\""
           ",\"note\":\"播种是玩家显式投入的外部物质，不代表自然生长或事件奖励\"}";

    // ---- presets ----
    out += ",\"presets\":[";
    for (size_t pi = 0; pi < presets().size(); ++pi) {
        const EnvPresetDef& p = presets()[pi];
        if (pi > 0) out += ',';
        out += "{\"id\":\"";
        out += envJsonEscape(p.id);
        out += "\",\"name\":\"";
        out += envJsonEscape(p.name);
        out += "\",\"desc\":\"";
        out += envJsonEscape(p.desc);
        out += "\",\"levels\":[";
        for (int i = 0; i < kEnvAxisCount; ++i) {
            if (i > 0) out += ',';
            out += std::to_string(p.levels[i]);
        }
        out += "],\"cyanoOn\":";
        out += p.cyanoOn ? "true" : "false";
        out += ",\"cyanoTarget\":";
        out += std::to_string(p.cyanoTarget);
        out += '}';
    }
    out += "]}";
    return out;
}

} // namespace agentsim::app

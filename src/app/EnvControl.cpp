// agentsim - app/EnvControl.cpp
// R1-C：四轴五档 / 细调 / 事件 / 预设 / 蓝细菌开关的数据表 + 命令解析 + GET /env JSON。
//
// 四轴档位数值：参照 v2.3.1 的档位结构，但换算到 agentsim 的小世界（256x256）
// 与"蓝细菌是唯一生产者"的新经济学（V2-SPEC §E / GOAL-ECOLOGY）：
//   - food 轴不再有"人工撒食"档（ball_spawn_probability 默认 0），档位改调
//     自由球能量与蓝细菌产球经济，仍保持"贫瘠→泛滥"的五档语义。
//   - 数值只写在本表（前端从 GET /env 读取，不得复制常量）。
//
// 修正 1：无影响力——apply 全部免费；幂等（同档/同开关态）成功且不计干预数。
// 确定性红线：本文件任何函数都不消耗 RNG。
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
            mkLevel("贫瘠", {{"ball_energy", 3.0}, {"cyano_ball_spawn_probability", 0.005}}),
            mkLevel("稀少", {{"ball_energy", 6.0}, {"cyano_ball_spawn_probability", 0.01}}),
            mkLevel("标准", {{"ball_energy", 10.0}, {"cyano_ball_spawn_probability", 0.02}}),
            mkLevel("丰饶", {{"ball_energy", 18.0}, {"cyano_ball_spawn_probability", 0.05}}),
            mkLevel("泛滥", {{"ball_energy", 30.0}, {"cyano_ball_spawn_probability", 0.1}}),
        };
        a.fine = {
            mkKnob("ball_energy", "单球能量", "", "吃掉一个自由球获得的能量"),
            mkKnob("cyano_ball_spawn_probability", "产球概率", "", "蓝细菌每节点每帧产球概率"),
            mkKnob("cyano_ball_spawn_cost", "产球成本", "", "蓝细菌产球消耗的能量", true),
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

std::vector<EnvEventDef> buildEvents() {
    // 修正 1：免费、无冷却。effect 只是一份可执行的效果描述，能量走 ledger。
    std::vector<EnvEventDef> v;

    EnvEventDef harvest;
    harvest.kind = EnvEventKind::Harvest;
    harvest.id = "harvest";
    harvest.name = "丰收";
    harvest.desc = "全体存活核 +8 能量（单次总注入 <=2000）：快速回血、缓解饥荒";
    harvest.effect.perNucleus = 8.0;
    harvest.effect.capPerEvent = 2000.0;
    v.push_back(std::move(harvest));

    EnvEventDef winter;
    winter.kind = EnvEventKind::Winter;
    winter.id = "winter";
    winter.name = "寒冬";
    winter.desc = "全体存活核 -5 能量（能量 <5 的核跳过）：加速淘汰弱者";
    winter.effect.perNucleus = -5.0;
    winter.effect.skipBelow = 5.0;
    v.push_back(std::move(winter));

    EnvEventDef plague;
    plague.kind = EnvEventKind::Plague;
    plague.id = "plague";
    plague.name = "瘟疫";
    plague.desc = "能量 >100 的富核 -12：抑制垄断巨头";
    plague.effect.perNucleus = -12.0;
    plague.effect.onlyRich = true;
    plague.effect.richThreshold = 100.0;
    v.push_back(std::move(plague));

    EnvEventDef bloom;
    bloom.kind = EnvEventKind::Bloom;
    bloom.id = "bloom";
    bloom.name = "蓝细菌爆发";
    bloom.desc = "蓝细菌关闭则激活 150 节点；已开启则追加 120 节点";
    bloom.effect.cyanoNodesToAdd = 120;
    bloom.effect.cyanoNodesIfDisabled = 150;
    v.push_back(std::move(bloom));

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
    v.push_back({"bloom", "爆发", "灼热光照 + 压力严酷 + 生产者爆发 + 蓝细菌 400 节点",
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

} // namespace

const std::vector<EnvAxisDef>& EnvControl::axes() { return axisTable(); }

const std::vector<EnvEventDef>& EnvControl::events() {
    static const std::vector<EnvEventDef> table = buildEvents();
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

const EnvEventDef* EnvControl::event(const std::string& id) {
    for (const EnvEventDef& e : events()) {
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

const EnvPendingEvent& EnvControl::lastEvent() {
    // apply() 写入、lastEvent() 读取。线程约定与 HttpServer 一致：
    // HTTP 请求在单线程内同步处理，无并发访问。
    static EnvPendingEvent current;
    return current;
}

void EnvControl::clearLastEvent() {
    EnvPendingEvent& e = const_cast<EnvPendingEvent&>(lastEvent());
    e = EnvPendingEvent{};
}

bool EnvControl::apply(const std::string& key, const std::string& value,
                       EnvState& st, std::vector<EnvChange>& out, std::string& err,
                       EnvPendingEvent* pendingEvent) {
    out.clear();
    err.clear();

    // ---- 蓝细菌开关：cyano=on/off（免费；同态幂等成功且不计数）----
    if (key == "cyano") {
        std::string v;
        v.reserve(value.size());
        for (char c : value) {
            v.push_back(static_cast<char>(
                c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
        }
        const bool wantOn = (v == "on" || v == "true" || v == "1" || v == "yes");
        const bool wantOff = (v == "off" || v == "false" || v == "0" || v == "no");
        if (!wantOn && !wantOff) {
            err = "cyano 取值只能是 on/off: " + value;
            return false;
        }
        if (st.cyanoOn == wantOn) {
            return true;  // 幂等：不扣费（修正 1 后即不计干预），不产出变更
        }
        st.cyanoOn = wantOn;
        ++st.interventions;
        out.push_back({"cyano_enabled", wantOn ? 1.0 : 0.0});
        return true;
    }

    // ---- 事件：event.<id>=1（免费、无冷却；只产出效果描述）----
    if (key.rfind("event.", 0) == 0) {
        const std::string id = key.substr(6);
        const EnvEventDef* e = event(id);
        if (e == nullptr) {
            err = "未知事件: " + id;
            return false;
        }
        // 值不约束（前端恒发 1），但必须是合法数字，保持"参数错要报错"的契约。
        double v = 0.0;
        if (!parseNum(value, v)) {
            err = "事件取值必须是数字: " + key + " = " + value;
            return false;
        }
        ++st.interventions;
        EnvPendingEvent pending;
        pending.id = e->id;
        pending.effect = e->effect;
        // bloom：追加节点数取决于蓝细菌当前开关态。
        if (e->kind == EnvEventKind::Bloom) {
            pending.effect.cyanoNodesToAdd = st.cyanoOn ? e->effect.cyanoNodesToAdd
                                                        : e->effect.cyanoNodesIfDisabled;
            pending.effect.cyanoNodesIfDisabled = 0;
        }
        EnvPendingEvent& slot = const_cast<EnvPendingEvent&>(lastEvent());
        slot = pending;
        if (pendingEvent != nullptr) *pendingEvent = pending;
        return true;
    }

    // ---- 预设：preset.<id>=1（免费；四轴档位 + 蓝细菌开关）----
    if (key.rfind("preset.", 0) == 0) {
        const std::string id = key.substr(7);
        const EnvPresetDef* p = preset(id);
        if (p == nullptr) {
            err = "未知预设: " + id;
            return false;
        }
        double v = 0.0;
        if (!parseNum(value, v)) {
            err = "预设取值必须是数字: " + key + " = " + value;
            return false;
        }
        for (int i = 0; i < kEnvAxisCount; ++i) {
            const EnvAxisDef& a = axes()[static_cast<size_t>(i)];
            const int level = p->levels[i];
            if (level < 0 || level >= static_cast<int>(a.levels.size())) {
                err = "预设档位越界: " + p->id;
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
            return false;
        }
        double levelF = 0.0;
        if (!parseNum(value, levelF)) {
            err = "档位必须是数字: " + key + " = " + value;
            return false;
        }
        const int level = static_cast<int>(levelF);
        if (level < 0 || level >= static_cast<int>(a->levels.size())) {
            err = "档位越界（0..4）: " + std::to_string(level);
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
            return false;
        }
        const ParamDef* d = ParamSchema::find(pkey);
        if (d == nullptr) {
            err = "未知参数键: " + pkey;
            return false;
        }
        double v = 0.0;
        if (!parseNum(value, v)) {
            err = "数值非法: " + pkey + " = " + value;
            return false;
        }
        ++st.interventions;
        out.push_back({pkey, clampToSchema(*d, v)});
        return true;
    }

    err = "未知命令: " + key;
    return false;
}

void EnvControl::tickFrame(EnvState& st) {
    // 修正 1：无影响力可恢复。保留帧计数挂钩（暂停时不调用即不推进）。
    ++st.frames;
}

std::string EnvControl::toJson(const EnvState& st, bool running, bool paused,
                               int cyanoNodes, int cyanoLinks) {
    std::string out;
    out.reserve(8192);
    out += "{\"running\":";
    out += running ? "true" : "false";
    out += ",\"paused\":";
    out += paused ? "true" : "false";
    // 修正 1：无 influence；interventions/peakAlive/frames 直接顶上。
    out += ",\"interventions\":";
    out += std::to_string(st.interventions);
    out += ",\"peakAlive\":";
    out += std::to_string(st.peakAlive);
    out += ",\"frames\":";
    out += std::to_string(st.frames);
    out += ",\"cyanoOn\":";
    out += st.cyanoOn ? "true" : "false";
    out += ",\"cyano\":{\"nodes\":";
    out += std::to_string(cyanoNodes);
    out += ",\"links\":";
    out += std::to_string(cyanoLinks);
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

    // ---- events（修正 1：无 cost/cooldown；effect 描述替代）----
    out += ",\"events\":[";
    for (size_t ei = 0; ei < events().size(); ++ei) {
        const EnvEventDef& e = events()[ei];
        if (ei > 0) out += ',';
        out += "{\"id\":\"";
        out += envJsonEscape(e.id);
        out += "\",\"name\":\"";
        out += envJsonEscape(e.name);
        out += "\",\"desc\":\"";
        out += envJsonEscape(e.desc);
        out += "\",\"effect\":{\"perNucleus\":";
        out += envJsonNum(e.effect.perNucleus);
        out += ",\"onlyRich\":";
        out += e.effect.onlyRich ? "true" : "false";
        out += ",\"richThreshold\":";
        out += envJsonNum(e.effect.richThreshold);
        out += ",\"skipBelow\":";
        out += envJsonNum(e.effect.skipBelow);
        out += ",\"cyanoNodesToAdd\":";
        out += std::to_string(e.effect.cyanoNodesToAdd);
        out += ",\"cyanoNodesIfDisabled\":";
        out += std::to_string(e.effect.cyanoNodesIfDisabled);
        out += ",\"capPerEvent\":";
        out += envJsonNum(e.effect.capPerEvent);
        out += "}}";
    }
    out += "]";

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

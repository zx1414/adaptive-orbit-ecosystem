// 玩家环境调控（v2.2.0）的**唯一事实来源**实现。
//
// 设计约束：
//  * 本文件只放数据表 + 纯配置操作 + JSON，不持有任何世界状态；
//    World::applyEnvCommand / applyEnvEvent 负责影响力、事件效果与派生对象同步。
//  * 四轴五档的具体数值**只写在这里**（前端从 GET /env 读取，不得复制常量）。
//  * 细调复用 ParamSchema 的键与 min/max：值先钳制再写入，未知键直接拒绝。
//  * 不消耗任何 RNG（纯配置读写）→ 不影响模拟确定性红线。
#include "EnvControl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ParamSchema.h"
#include "World.h"

namespace {

// ==================== 四轴五档数据表 ====================

EnvAxisLevel makeLevel(const char* label, std::map<std::string, double> values) {
    EnvAxisLevel l;
    l.label = label;
    l.values = std::move(values);
    return l;
}

EnvFineKnob makeKnob(const char* key, const char* label, const char* unit) {
    EnvFineKnob k;
    k.key = key;
    k.label = label;
    k.unit = unit;
    return k;
}

std::vector<EnvAxisDef> buildAxes() {
    std::vector<EnvAxisDef> v;

    // ---- 光照 ----
    {
        EnvAxisDef a;
        a.axis = EnvAxis::Light;
        a.id = "light";
        a.name = "光照";
        a.hint = "光照决定生产者产能：极夜几乎停摆，灼热光合旺盛但昼夜波动更剧烈";
        a.levels = {makeLevel("极夜", {{"sun_base_level", 0.2}, {"sun_day_amplitude", 0.2}}),
                    makeLevel("阴天", {{"sun_base_level", 0.5}, {"sun_day_amplitude", 0.4}}),
                    makeLevel("标准", {{"sun_base_level", 1.0}, {"sun_day_amplitude", 0.7}}),
                    makeLevel("晴朗", {{"sun_base_level", 1.5}, {"sun_day_amplitude", 0.8}}),
                    makeLevel("灼热", {{"sun_base_level", 2.2}, {"sun_day_amplitude", 0.9}})};
        a.fine = {makeKnob("sun_base_level", "基准强度", ""),
                  makeKnob("sun_day_amplitude", "昼夜振幅", ""),
                  makeKnob("sun_day_period_frames", "昼夜周期", "帧")};
        v.push_back(std::move(a));
    }

    // ---- 食物 ----
    {
        EnvAxisDef a;
        a.axis = EnvAxis::Food;
        a.id = "food";
        a.name = "食物";
        a.hint = "食物决定能量注入速率：贫瘠逼迫节食，泛滥让竞争从觅食转向繁殖与争斗";
        a.levels = {makeLevel("贫瘠", {{"ball_spawn_probability", 0.0}, {"base_ball_energy", 0.5}}),
                    makeLevel("稀少", {{"ball_spawn_probability", 0.003}, {"base_ball_energy", 0.8}}),
                    makeLevel("标准", {{"ball_spawn_probability", 0.01}, {"base_ball_energy", 1.0}}),
                    makeLevel("丰饶", {{"ball_spawn_probability", 0.05}, {"base_ball_energy", 1.3}}),
                    makeLevel("泛滥", {{"ball_spawn_probability", 0.2}, {"base_ball_energy", 1.6}})};
        a.fine = {makeKnob("ball_spawn_probability", "补充概率", ""),
                  makeKnob("base_ball_energy", "单球能量", "")};
        v.push_back(std::move(a));
    }

    // ---- 压力 ----
    {
        EnvAxisDef a;
        a.axis = EnvAxis::Pressure;
        a.id = "pressure";
        a.name = "压力";
        a.hint = "代谢压力决定生存成本：安逸适合扩张，炼狱筛选高效节能的个体";
        a.levels = {makeLevel("安逸", {{"basal_cost", 0.002}}),
                    makeLevel("温和", {{"basal_cost", 0.006}}),
                    makeLevel("标准", {{"basal_cost", 0.01}}),
                    makeLevel("严酷", {{"basal_cost", 0.02}}),
                    makeLevel("炼狱", {{"basal_cost", 0.04}})};
        a.fine = {makeKnob("basal_cost", "基础代谢", ""),
                  makeKnob("speed_cost_k", "速度代价", "")};
        v.push_back(std::move(a));
    }

    // ---- 生产者 ----
    {
        EnvAxisDef a;
        a.axis = EnvAxis::Producer;
        a.id = "producer";
        a.name = "生产者";
        a.hint = "生产者规模决定捕食收益：繁盛时捕食者获利，爆发时节点间竞争与收割都更激烈";
        a.levels = {makeLevel("停滞", {{"cyano_photosynthesis_rate", 0.005}}),
                    makeLevel("稀疏", {{"cyano_photosynthesis_rate", 0.012}}),
                    makeLevel("标准", {{"cyano_photosynthesis_rate", 0.02}}),
                    makeLevel("繁盛", {{"cyano_photosynthesis_rate", 0.04}}),
                    makeLevel("爆发", {{"cyano_photosynthesis_rate", 0.08}})};
        a.fine = {makeKnob("cyano_photosynthesis_rate", "光合速率", ""),
                  makeKnob("cyano_metabolism_rate", "代谢消耗", ""),
                  makeKnob("cyano_predation_radius", "捕食半径", ""),
                  makeKnob("cyano_transfer_rate", "传输速率", "")};
        v.push_back(std::move(a));
    }

    return v;
}

std::vector<EnvEventDef> buildEvents() {
    // 手动事件只受「影响力」约束：可以连续触发，不再有冷却。
    return {
        {EnvEventKind::Harvest, "harvest", "丰收",
         "全体存活核 +8 能量（单次总注入 ≤2000）：快速回血、缓解饥荒", 15.0, 0},
        {EnvEventKind::Winter, "winter", "寒冬",
         "全体存活核 −5 能量（能量 <5 的核跳过）：加速淘汰弱者", 10.0, 0},
        {EnvEventKind::Plague, "plague", "瘟疫",
         "能量 >100 的富核 −12：抑制垄断巨头", 20.0, 0},
        {EnvEventKind::Bloom, "bloom", "蓝细菌爆发",
         "蓝细菌关闭则激活 150 节点；已开启则追加 120 节点", 25.0, 0},
    };
}

std::vector<EnvPresetDef> buildPresets() {
    return {
        {"balance", "平衡", "四轴全部标准 + 蓝细菌 150 节点：适合观察基线生态",
         {2, 2, 2, 2}, true, 150},
        {"bounty", "丰饶", "晴朗光照 + 食物丰饶 + 压力温和 + 生产者繁盛：种群快速扩张",
         {3, 3, 1, 3}, true, 150},
        {"harsh", "严酷", "阴天光照 + 食物稀少 + 压力严酷 + 生产者稀疏：生存筛选",
         {1, 1, 3, 1}, true, 150},
        {"bloom", "爆发", "灼热光照 + 压力严酷 + 生产者爆发 + 蓝细菌 400 节点：生产者主导的生态",
         {4, 2, 3, 4}, true, 400},
    };
}

// ==================== 配置读写小工具 ====================

// 数值 → ParamSchema 文本（%.17g：double 往返无损）。
std::string fmtNum(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 通过 ParamSchema 写一个标量键（Pair 区间键不支持单值写入）。
bool writeKey(WorldConfig& cfg, const std::string& key, double value, std::string& err) {
    const ParamDef* d = ParamSchema::find(key);
    if (!d) {
        err = "未知参数键: " + key;
        return false;
    }
    if (d->type == ParamDef::Type::Pair) {
        err = "区间参数不支持单值写入: " + key;
        return false;
    }
    if (!ParamSchema::applyLine(cfg, key, fmtNum(value))) {
        err = "参数写入失败: " + key;
        return false;
    }
    return true;
}

// 读取当前标量值：借 ParamSchema::valueString 的文本（%g）再转回 double。
// 只用于"当前档位"判定，axis 数据表的取值都是 6 位有效数字内可精确往返的十进制数。
bool readKey(const WorldConfig& cfg, const std::string& key, double& out) {
    const ParamDef* d = ParamSchema::find(key);
    if (!d || d->type == ParamDef::Type::Pair) return false;
    const std::string s = ParamSchema::valueString(cfg, *d);
    if (s.empty()) return false;
    out = std::strtod(s.c_str(), nullptr);
    return true;
}

// 细调白名单：只允许 axes() 各 fine 列表里**暴露给 UI** 的键。
// 为什么不是全部 ParamSchema 键：width/height（世界尺寸）、seed（随机流）、
// balls/nuclei（初始数量）、frames（结束帧）等在运行中热改会破坏存档语义与
// “同种子可复现”的前提；它们必须走参数页/读档，而不是实时细调。
bool isFineKey(const std::string& key) {
    for (const EnvAxisDef& a : EnvControl::axes()) {
        for (const EnvFineKnob& k : a.fine) {
            if (key == k.key) return true;
        }
    }
    return false;
}

bool nearlyEqual(double a, double b) {
    const double diff = std::fabs(a - b);
    const double scale = std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
    return diff <= 1e-12 * scale;
}

// 严格数值解析：整个字符串必须是一个数（拒绝 "3abc" / 空串）。
bool parseNum(const std::string& text, double& out) {
    const std::string t = trimStr(text);
    if (t.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == nullptr) return false;
    if (static_cast<size_t>(end - t.c_str()) != t.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

std::string jsonEscape(const std::string& s) {
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
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// JSON 数字：至少保留一个小数位（50 → 50.0），避免前端把整数当整数类型展示。
std::string jsonNum(double v) {
    if (!std::isfinite(v)) return "0.0";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    std::string s(buf);
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    return s;
}

}  // namespace

// ==================== 数据表访问 ====================

const std::vector<EnvAxisDef>& EnvControl::axes() {
    static const std::vector<EnvAxisDef> table = buildAxes();
    return table;
}

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
        if (id == a.id) return &a;
    }
    return nullptr;
}

const EnvEventDef* EnvControl::event(const std::string& id) {
    for (const EnvEventDef& e : events()) {
        if (id == e.id) return &e;
    }
    return nullptr;
}

const EnvPresetDef* EnvControl::preset(const std::string& id) {
    for (const EnvPresetDef& p : presets()) {
        if (id == p.id) return &p;
    }
    return nullptr;
}

// ==================== 档位 / 细调 / 预设 ====================

int EnvControl::axisLevelOf(const WorldConfig& cfg, EnvAxis a) {
    for (const EnvAxisDef& d : axes()) {
        if (d.axis != a) continue;
        for (int lv = 0; lv < 5; ++lv) {
            bool match = true;
            for (const auto& kv : d.levels[(size_t)lv].values) {
                double cur = 0.0;
                if (!readKey(cfg, kv.first, cur) || !nearlyEqual(cur, kv.second)) {
                    match = false;
                    break;
                }
            }
            if (match) return lv;
        }
        return -1;  // 自定义
    }
    return -1;
}

bool EnvControl::applyAxisLevel(WorldConfig& cfg, EnvAxis a, int level, std::string& err) {
    if (level < 0 || level >= 5) {
        err = "档位越界（0..4）";
        return false;
    }
    for (const EnvAxisDef& d : axes()) {
        if (d.axis != a) continue;
        for (const auto& kv : d.levels[(size_t)level].values) {
            if (!writeKey(cfg, kv.first, kv.second, err)) return false;
        }
        return true;
    }
    err = "未知环境轴";
    return false;
}

bool EnvControl::applyFine(WorldConfig& cfg, const std::string& key, const std::string& value,
                           std::string& err) {
    // 白名单：只允许 axes() 各 fine 列表暴露给 UI 的键（契约 v2.2.0 §3.1 的收窄版）。
    // 其他 ParamSchema 键（width/height/seed/balls/nuclei/frames …）一律拒绝，世界不变。
    if (!isFineKey(key)) {
        err = "该参数不支持实时细调: " + key;
        return false;
    }
    const ParamDef* d = ParamSchema::find(key);
    if (!d) {
        err = "未知参数键: " + key;
        return false;
    }
    if (d->type == ParamDef::Type::Pair) {
        err = "区间参数不支持细调: " + key;
        return false;
    }
    double v = 0.0;
    if (!parseNum(value, v)) {
        err = "数值非法: " + key;
        return false;
    }
    // 契约：值先按 schema 的 min/max 钳制（越界不拒绝，钳制后写入）。
    if (v < d->min) v = d->min;
    if (v > d->max) v = d->max;
    if (d->type == ParamDef::Type::Int) v = std::round(v);
    if (d->type == ParamDef::Type::Bool) v = (v >= 0.5) ? 1.0 : 0.0;
    return writeKey(cfg, key, v, err);
}

bool EnvControl::applyPresetConfig(WorldConfig& cfg, const EnvPresetDef& p, std::string& err) {
    for (int i = 0; i < kEnvAxisCount; ++i) {
        if (!applyAxisLevel(cfg, static_cast<EnvAxis>(i), p.levels[(size_t)i], err)) {
            return false;
        }
    }
    return true;
}

// ==================== GET /env JSON ====================

std::string EnvControl::toJson(const EnvState& st, const WorldConfig& cfg,
                               int cyanoNodes, int cyanoLinks, bool cyanoEnabled,
                               bool running, bool paused) {
    std::string out;
    out.reserve(4096);
    out += "{\"running\":";
    out += running ? "true" : "false";
    out += ",\"paused\":";
    out += paused ? "true" : "false";
    out += ",\"influence\":{\"cur\":" + jsonNum(st.influence) +
           ",\"max\":" + jsonNum(kInfluenceMax) +
           ",\"regenPerFrame\":" + jsonNum(kInfluenceRegenPerFrame) + "}";

    // ---- axes ----
    out += ",\"axes\":[";
    bool firstAxis = true;
    for (const EnvAxisDef& a : axes()) {
        if (!firstAxis) out += ',';
        firstAxis = false;
        out += "{\"id\":\"";
        out += a.id;
        out += "\",\"name\":\"";
        out += a.name;
        out += "\",\"hint\":\"";
        out += jsonEscape(a.hint);
        out += "\",\"level\":";
        out += std::to_string(axisLevelOf(cfg, a.axis));
        out += ",\"levels\":[";
        for (int lv = 0; lv < 5; ++lv) {
            if (lv > 0) out += ',';
            out += "{\"label\":\"";
            out += a.levels[(size_t)lv].label;
            out += "\",\"values\":{";
            bool firstVal = true;
            for (const auto& kv : a.levels[(size_t)lv].values) {
                if (!firstVal) out += ',';
                firstVal = false;
                out += '"';
                out += kv.first;
                out += "\":";
                out += jsonNum(kv.second);
            }
            out += "}}";
        }
        out += "],\"fine\":[";
        for (size_t i = 0; i < a.fine.size(); ++i) {
            if (i > 0) out += ',';
            const EnvFineKnob& k = a.fine[i];
            const ParamDef* d = ParamSchema::find(k.key);
            out += "{\"key\":\"";
            out += k.key;
            out += "\",\"label\":\"";
            out += k.label;
            out += "\",\"unit\":\"";
            out += k.unit;
            out += "\",\"min\":";
            out += jsonNum(d ? d->min : 0.0);
            out += ",\"max\":";
            out += jsonNum(d ? d->max : 0.0);
            out += ",\"cur\":\"";
            out += d ? jsonEscape(ParamSchema::valueString(cfg, *d)) : std::string("0");
            out += "\"}";
        }
        out += "]}";
    }
    out += "]";

    // ---- events ----
    out += ",\"events\":[";
    for (size_t i = 0; i < events().size(); ++i) {
        if (i > 0) out += ',';
        const EnvEventDef& e = events()[i];
        out += "{\"id\":\"";
        out += e.id;
        out += "\",\"name\":\"";
        out += e.name;
        out += "\",\"desc\":\"";
        out += jsonEscape(e.desc);
        out += "\",\"cost\":";
        out += jsonNum(e.cost);
        // 兼容字段：玩家手动事件无冷却，恒为 0。
        out += ",\"cooldownLeft\":0,\"cooldown\":0}";
    }
    out += "]";

    // ---- presets ----
    out += ",\"presets\":[";
    for (size_t i = 0; i < presets().size(); ++i) {
        if (i > 0) out += ',';
        const EnvPresetDef& p = presets()[i];
        out += "{\"id\":\"";
        out += p.id;
        out += "\",\"name\":\"";
        out += p.name;
        out += "\",\"desc\":\"";
        out += jsonEscape(p.desc);
        out += "\"}";
    }
    out += "]";

    // ---- cyano ----
    out += ",\"cyano\":{\"enabled\":";
    out += cyanoEnabled ? "true" : "false";
    out += ",\"nodes\":";
    out += std::to_string(cyanoNodes);
    out += ",\"links\":";
    out += std::to_string(cyanoLinks);
    out += "}";

    // ---- mods（由启动器/自动事件 mod 通过 setEnvModStatus 上报）----
    const EnvModStatus ms = envModStatus();
    out += ",\"mods\":{\"climateAuto\":";
    out += ms.climateAuto ? "true" : "false";
    out += ",\"nextClimateIn\":";
    out += std::to_string(ms.nextClimateIn);
    out += "}";

    // ---- stats（追加段，前端可选降级）：干预计数 / 存活峰值 ----
    out += ",\"stats\":{\"interventions\":";
    out += std::to_string(st.interventions);
    out += ",\"peakAlive\":";
    out += std::to_string(st.peakAlive);
    out += "}";

    // ---- costs（追加段，前端可选降级）：影响力价格表，前端不得复制常量 ----
    out += ",\"costs\":{\"quick\":";
    out += std::to_string((int)kQuickCost);
    out += ",\"fine\":";
    out += std::to_string((int)kFineCost);
    out += ",\"preset\":";
    out += std::to_string((int)kPresetCost);
    out += ",\"cyano\":";
    out += std::to_string((int)kCyanoToggleCost);
    out += "}}";
    return out;
}

// ==================== 环境 mod 状态（供 toJson 的 mods 段） ====================

namespace {
EnvModStatus gEnvModStatus;  // 启动器/自动事件 mod 上报，toJson 只读
}  // namespace

void setEnvModStatus(const EnvModStatus& st) {
    gEnvModStatus = st;
}

EnvModStatus envModStatus() {
    return gEnvModStatus;
}

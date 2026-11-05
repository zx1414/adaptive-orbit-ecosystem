// agentsim - tests/unit/test_envcontrol.cpp
// R1-C：EnvControl + ParamSchema 单测（V2-SPEC-AMEND-1 语义：无影响力）。
// 与 test_cli.cpp 同理：tools/build.ps1 的单测目标不链 src/app，
// 直接 #include 翻译单元（若日后构建脚本把 app 源加进单测链接集，
// 必须删除这些 #include，否则符号重复）。
#include "../src/app/EnvControl.cpp"
#include "../src/app/ParamSchema.cpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "test_framework.h"

namespace {

using agentsim::app::EnvAxis;
using agentsim::app::EnvAxisDef;
using agentsim::app::EnvChange;
using agentsim::app::EnvControl;
using agentsim::app::EnvEventDef;
using agentsim::app::EnvEventKind;
using agentsim::app::EnvPendingEvent;
using agentsim::app::EnvState;
using agentsim::app::ParamDef;
using agentsim::app::ParamSchema;

// 执行一条命令的小工具：返回成功与否，顺带清空 lastEvent。
bool runCmd(const std::string& key, const std::string& value, EnvState& st,
            std::vector<EnvChange>& out, std::string& err) {
    EnvControl::clearLastEvent();
    return EnvControl::apply(key, value, st, out, err);
}

int axisIndex(const std::string& id) {
    if (id == "light") return 0;
    if (id == "food") return 1;
    if (id == "pressure") return 2;
    if (id == "producer") return 3;
    return -1;
}

} // namespace

// ---------- ParamSchema：表完整性 ----------

TEST(param_schema_size_at_least_110) {
    CHECK(ParamSchema::all().size() >= 110u);
}

TEST(param_schema_keys_unique) {
    std::set<std::string> keys;
    for (const ParamDef& d : ParamSchema::all()) {
        CHECK(keys.insert(d.key).second);
    }
}

TEST(param_schema_min_default_max_ordered) {
    for (const ParamDef& d : ParamSchema::all()) {
        CHECK(d.min <= d.dflt);
        CHECK(d.dflt <= d.max);
    }
}

TEST(param_schema_find_roundtrip) {
    CHECK(ParamSchema::find("sun_base_level") != nullptr);
    CHECK(ParamSchema::find("cyano_photosynthesis_rate") != nullptr);
    CHECK(ParamSchema::find("ball_damping") != nullptr);
    CHECK(ParamSchema::find("nucleus_damping") != nullptr);
    CHECK(ParamSchema::find("basal_cost") != nullptr);
    CHECK(ParamSchema::find("no_such_key_at_all") == nullptr);
}

TEST(param_schema_validate_accepts_and_rejects) {
    std::string err;
    CHECK(ParamSchema::validate("sun_base_level", "1.35", err));
    CHECK(err.empty());
    CHECK(ParamSchema::validate("nuclei", "12", err));
    CHECK(ParamSchema::validate("regrow_food", "on", err));
    CHECK(ParamSchema::validate("regrow_food", "0", err));
    // 越界
    CHECK(!ParamSchema::validate("sun_base_level", "99", err));
    CHECK(!err.empty());
    // 非法数值
    CHECK(!ParamSchema::validate("sun_base_level", "1.3abc", err));
    CHECK(!ParamSchema::validate("nuclei", "12.5", err));
    CHECK(!ParamSchema::validate("nuclei", "", err));
    // 未知键
    CHECK(!ParamSchema::validate("no_such_key", "1", err));
    // 布尔非法
    CHECK(!ParamSchema::validate("regrow_food", "maybe", err));
}

TEST(param_schema_amend1_no_combat_keys) {
    // V2-SPEC-AMEND-1：战斗取消，不得出现 attack_/avoid_ 基因键。
    for (const ParamDef& d : ParamSchema::all()) {
        if (d.group == "genome") {
            CHECK(d.key.rfind("attack_", 0) != 0);
            CHECK(d.key.rfind("avoid_", 0) != 0);
        }
    }
}

TEST(param_schema_amend1_genome_keys_present) {
    static const char* const kGenome[] = {
        "affinity_resource", "affinity_scout", "affinity_shield",
        "orbit_radius_resource", "orbit_radius_scout", "orbit_radius_shield",
        "absorb_preference_resource", "absorb_preference_scout", "absorb_preference_shield",
        "repel_strength_resource", "repel_strength_scout", "repel_strength_shield",
        "nucleus_sight_radius", "nucleus_personal_space", "nucleus_repel_strength",
        "nucleus_cohesion", "force_range", "force_cost_k",
        "max_speed", "max_accel", "energy_threshold", "mutation_rate",
        "cyano_preference", "cyano_digestion",
    };
    for (const char* k : kGenome) {
        const ParamDef* d = ParamSchema::find(k);
        CHECK(d != nullptr);
        if (d != nullptr) CHECK(d->group == "genome");
    }
}

TEST(param_schema_tojson_shape) {
    const std::string json = ParamSchema::toJson();
    // 分组齐全 + count 字段
    CHECK(json.find("\"count\":") != std::string::npos);
    CHECK(json.find("\"id\":\"world\"") != std::string::npos);
    CHECK(json.find("\"id\":\"light\"") != std::string::npos);
    CHECK(json.find("\"id\":\"food\"") != std::string::npos);
    CHECK(json.find("\"id\":\"pressure\"") != std::string::npos);
    CHECK(json.find("\"id\":\"producer\"") != std::string::npos);
    CHECK(json.find("\"id\":\"cyano\"") != std::string::npos);
    CHECK(json.find("\"id\":\"genome\"") != std::string::npos);
    CHECK(json.find("\"id\":\"ball\"") != std::string::npos);
    CHECK(json.find("\"id\":\"nucleus\"") != std::string::npos);
    // 参数对象形态
    CHECK(json.find("\"key\":\"sun_base_level\"") != std::string::npos);
    CHECK(json.find("\"type\":\"double\"") != std::string::npos);
    CHECK(json.find("\"default\":") != std::string::npos);
    // 括号配对（朴素完整性）
    CHECK(std::count(json.begin(), json.end(), '{') ==
          std::count(json.begin(), json.end(), '}'));
    CHECK(std::count(json.begin(), json.end(), '[') ==
          std::count(json.begin(), json.end(), ']'));
}

// ---------- EnvControl：数据表 ----------

TEST(env_axes_four_by_five) {
    CHECK(EnvControl::axes().size() == 4u);
    static const char* const kIds[] = {"light", "food", "pressure", "producer"};
    int i = 0;
    for (const EnvAxisDef& a : EnvControl::axes()) {
        CHECK(a.id == kIds[i]);
        CHECK(a.levels.size() == 5u);
        CHECK(!a.fine.empty());
        ++i;
    }
}

TEST(env_events_four_free_no_cooldown) {
    CHECK(EnvControl::events().size() == 4u);
    const agentsim::app::EnvEventDef* harvest = EnvControl::event("harvest");
    const agentsim::app::EnvEventDef* winter = EnvControl::event("winter");
    const agentsim::app::EnvEventDef* plague = EnvControl::event("plague");
    const agentsim::app::EnvEventDef* bloom = EnvControl::event("bloom");
    CHECK(harvest != nullptr);
    CHECK(winter != nullptr);
    CHECK(plague != nullptr);
    CHECK(bloom != nullptr);
    if (harvest != nullptr) {
        CHECK_NEAR(harvest->effect.perNucleus, 8.0, 1e-12);
        CHECK_NEAR(harvest->effect.capPerEvent, 2000.0, 1e-12);
    }
    if (winter != nullptr) {
        CHECK_NEAR(winter->effect.perNucleus, -5.0, 1e-12);
        CHECK_NEAR(winter->effect.skipBelow, 5.0, 1e-12);
    }
    if (plague != nullptr) {
        CHECK_NEAR(plague->effect.perNucleus, -12.0, 1e-12);
        CHECK(plague->effect.onlyRich);
        CHECK_NEAR(plague->effect.richThreshold, 100.0, 1e-12);
    }
    if (bloom != nullptr) {
        CHECK(bloom->effect.cyanoNodesToAdd == 120);
        CHECK(bloom->effect.cyanoNodesIfDisabled == 150);
    }
}

TEST(env_presets_at_least_three) {
    CHECK(EnvControl::presets().size() >= 3u);
    CHECK(EnvControl::preset("balance") != nullptr);
    CHECK(EnvControl::preset("nope") == nullptr);
}

// ---------- EnvControl：快调 ----------

TEST(env_axis_all_levels_apply) {
    // 4 轴 × 5 档全部合法（换档永远成功）。
    for (const char* id : {"light", "food", "pressure", "producer"}) {
        for (int lv = 0; lv <= 4; ++lv) {
            EnvState st;
            std::vector<EnvChange> out;
            std::string err;
            CHECK(runCmd(std::string("axis.") + id, std::to_string(lv), st, out, err));
            CHECK(err.empty());
        }
    }
}

TEST(env_axis_changes_apply_and_count_intervention) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    CHECK(runCmd("axis.light", "3", st, out, err));
    CHECK(err.empty());
    CHECK(st.interventions == 1);
    CHECK(st.axisLevels[0] == 3);
    CHECK(!out.empty());
    bool hasSun = false;
    for (const EnvChange& c : out) {
        if (c.key == "sun_base_level") {
            hasSun = true;
            CHECK_NEAR(c.value, 1.8, 1e-12);
        }
    }
    CHECK(hasSun);
}

TEST(env_axis_same_level_idempotent_free) {
    // 同档幂等：成功、不产出变更、不计干预。
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    CHECK(runCmd("axis.pressure", "2", st, out, err));  // 初始就是 2
    CHECK(err.empty());
    CHECK(out.empty());
    CHECK(st.interventions == 0);
    // 换档再换回来也幂等于"回到初始档"的第二次调用计数规则：换过去 +1，回来 +1。
    CHECK(runCmd("axis.pressure", "4", st, out, err));
    CHECK(st.interventions == 1);
    CHECK(runCmd("axis.pressure", "4", st, out, err));
    CHECK(st.interventions == 1);  // 同档再调不再计数
    CHECK(runCmd("axis.pressure", "2", st, out, err));
    CHECK(st.interventions == 2);
}

TEST(env_axis_out_of_range_rejected) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    CHECK(!runCmd("axis.light", "5", st, out, err));
    CHECK(!err.empty());
    CHECK(!runCmd("axis.light", "-1", st, out, err));
    CHECK(!runCmd("axis.light", "abc", st, out, err));
    CHECK(st.interventions == 0);
    CHECK(st.axisLevels[0] == 2);  // 失败时状态不变
    CHECK(!runCmd("axis.bogus", "1", st, out, err));
}

// ---------- EnvControl：细调 ----------

TEST(env_fine_single_key_works_and_clamps) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    CHECK(runCmd("fine.sun_base_level", "1.35", st, out, err));
    CHECK(err.empty());
    CHECK(st.interventions == 1);
    CHECK(out.size() == 1u);
    CHECK(out[0].key == "sun_base_level");
    CHECK_NEAR(out[0].value, 1.35, 1e-12);
    // 越界值钳制进 schema 范围（不拒绝）。
    CHECK(runCmd("fine.sun_base_level", "999", st, out, err));
    CHECK(out.size() == 1u);
    CHECK_NEAR(out[0].value, 5.0, 1e-12);  // schema max
}

TEST(env_fine_whitelist_rejects_non_fine_keys) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    // width/seed/frames 等运行中不可热改的键必须在白名单外。
    CHECK(!runCmd("fine.width", "9999", st, out, err));
    CHECK(!runCmd("fine.seed", "7", st, out, err));
    CHECK(!runCmd("fine.nuclei", "3", st, out, err));
    CHECK(st.interventions == 0);
    CHECK(out.empty());
    CHECK(EnvControl::isFineKey("sun_base_level"));
    CHECK(EnvControl::isFineKey("mutation_coefficient"));
    CHECK(!EnvControl::isFineKey("width"));
}

// ---------- EnvControl：事件（免费、无冷却）----------

TEST(env_event_harvest_ten_times_in_a_row) {
    // 修正 1：事件免费无冷却，连续 10 次全部成功。
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    for (int i = 0; i < 10; ++i) {
        CHECK(runCmd("event.harvest", "1", st, out, err));
        CHECK(err.empty());
    }
    CHECK(st.interventions == 10);
    const EnvPendingEvent& ev = EnvControl::lastEvent();
    CHECK(ev.id == "harvest");
    CHECK_NEAR(ev.effect.perNucleus, 8.0, 1e-12);
    CHECK_NEAR(ev.effect.capPerEvent, 2000.0, 1e-12);
}

TEST(env_event_effect_descriptions) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;

    CHECK(runCmd("event.winter", "1", st, out, err));
    {
        const EnvPendingEvent& ev = EnvControl::lastEvent();
        CHECK(ev.id == "winter");
        CHECK_NEAR(ev.effect.perNucleus, -5.0, 1e-12);
        CHECK_NEAR(ev.effect.skipBelow, 5.0, 1e-12);
    }

    CHECK(runCmd("event.plague", "1", st, out, err));
    {
        const EnvPendingEvent& ev = EnvControl::lastEvent();
        CHECK(ev.id == "plague");
        CHECK(ev.effect.onlyRich);
        CHECK_NEAR(ev.effect.richThreshold, 100.0, 1e-12);
        CHECK_NEAR(ev.effect.perNucleus, -12.0, 1e-12);
    }

    // bloom：蓝细菌开→追加 120；关→激活 150。
    st.cyanoOn = true;
    CHECK(runCmd("event.bloom", "1", st, out, err));
    CHECK(EnvControl::lastEvent().effect.cyanoNodesToAdd == 120);
    st.cyanoOn = false;
    CHECK(runCmd("event.bloom", "1", st, out, err));
    CHECK(EnvControl::lastEvent().effect.cyanoNodesToAdd == 150);

    CHECK(!runCmd("event.meteor", "1", st, out, err));
    CHECK(!err.empty());
}

TEST(env_event_never_mutates_out_or_rejects_when_free) {
    // 事件只产出描述，不产出参数变更；无冷却即连续不同事件都成功。
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    const char* const seq[] = {"event.harvest", "event.winter", "event.plague", "event.bloom"};
    for (int round = 0; round < 4; ++round) {
        for (const char* k : seq) {
            out.clear();
            CHECK(runCmd(k, "1", st, out, err));
            CHECK(err.empty());
            CHECK(out.empty());  // 事件不产参数变更
        }
    }
    CHECK(st.interventions == 16);
}

// ---------- EnvControl：预设与蓝细菌开关 ----------

TEST(env_preset_applies_all_axes_and_cyano) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    // harsh 预设 cyanoOn=true，与初始态相同 → 按契约不重复产出 cyano_enabled。
    CHECK(runCmd("preset.harsh", "1", st, out, err));
    CHECK(err.empty());
    CHECK(st.interventions == 1);
    CHECK(st.axisLevels[0] == 1);  // light 阴天
    CHECK(st.axisLevels[1] == 1);  // food 稀少
    CHECK(st.axisLevels[2] == 3);  // pressure 严酷
    CHECK(st.axisLevels[3] == 1);  // producer 稀疏
    CHECK(st.cyanoOn);
    bool hasCyano = false;
    int axisKeyCount = 0;
    for (const EnvChange& c : out) {
        if (c.key == "cyano_enabled") {
            hasCyano = true;
        } else {
            ++axisKeyCount;
        }
    }
    CHECK(!hasCyano);            // 同态不产出变更（EnvControl.h 契约）
    CHECK(axisKeyCount == 6);    // light 2 + food 2 + pressure 1 + producer 1
    // 蓝细菌关闭时同一预设必须产出 cyano_enabled=1。
    EnvState st2;
    st2.cyanoOn = false;
    out.clear();
    CHECK(runCmd("preset.harsh", "1", st2, out, err));
    CHECK(st2.cyanoOn);
    hasCyano = false;
    for (const EnvChange& c : out) {
        if (c.key == "cyano_enabled") hasCyano = true;
    }
    CHECK(hasCyano);
    CHECK(!runCmd("preset.bogus", "1", st, out, err));
}

TEST(env_preset_keeps_current_level_reflected) {
    // 预设后快调档位必须与预设声明一致（幂等判定的依据被同步）。
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    CHECK(runCmd("preset.bounty", "1", st, out, err));
    // bounty = light 3 / food 3 / pressure 1 / producer 3
    out.clear();
    CHECK(runCmd("axis.light", "3", st, out, err));   // 已在 3：幂等
    CHECK(out.empty());
    CHECK(st.interventions == 1);  // 只有预设计了 1 次
    out.clear();
    CHECK(runCmd("axis.producer", "4", st, out, err));  // 3 → 4：换档
    CHECK(st.interventions == 2);
}

TEST(env_cyano_toggle_idempotent) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    CHECK(runCmd("cyano", "on", st, out, err));  // 初始就是 on
    CHECK(err.empty());
    CHECK(out.empty());
    CHECK(st.interventions == 0);
    CHECK(runCmd("cyano", "off", st, out, err));
    CHECK(st.interventions == 1);
    CHECK(!st.cyanoOn);
    CHECK(out.size() == 1u);
    CHECK(out[0].key == "cyano_enabled");
    CHECK_NEAR(out[0].value, 0.0, 1e-12);
    CHECK(runCmd("cyano", "off", st, out, err));  // 幂等
    CHECK(st.interventions == 1);
    CHECK(!runCmd("cyano", "maybe", st, out, err));
    CHECK(!err.empty());
}

TEST(env_unknown_command_rejected) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    CHECK(!runCmd("bogus", "1", st, out, err));
    CHECK(!err.empty());
    CHECK(st.interventions == 0);
    CHECK(!runCmd("axis", "1", st, out, err));
    CHECK(!runCmd("fine.", "1", st, out, err));
    CHECK(!runCmd("fine.nope", "1", st, out, err));
}

// ---------- EnvControl：tickFrame / toJson ----------

TEST(env_tick_frame_advances_frames_only) {
    // 修正 1：无影响力恢复；tickFrame 只推进帧计数。
    EnvState st;
    for (int i = 0; i < 10; ++i) EnvControl::tickFrame(st);
    CHECK(st.frames == 10);
    CHECK(st.interventions == 0);
}

TEST(env_tojson_shape_amend1) {
    EnvState st;
    st.interventions = 7;
    st.peakAlive = 42;
    st.axisLevels[0] = 3;
    st.cyanoOn = false;
    const std::string json = EnvControl::toJson(st, true, false, 64, 120);
    // 修正 1：无 influence；有 interventions/peakAlive。
    CHECK(json.find("influence") == std::string::npos);
    CHECK(json.find("\"interventions\":7") != std::string::npos);
    CHECK(json.find("\"peakAlive\":42") != std::string::npos);
    CHECK(json.find("\"frames\":0") != std::string::npos);
    CHECK(json.find("\"cyanoOn\":false") != std::string::npos);
    CHECK(json.find("\"running\":true") != std::string::npos);
    CHECK(json.find("\"paused\":false") != std::string::npos);
    CHECK(json.find("\"nodes\":64") != std::string::npos);
    CHECK(json.find("\"links\":120") != std::string::npos);
    CHECK(json.find("\"id\":\"light\"") != std::string::npos);
    CHECK(json.find("\"level\":3") != std::string::npos);
    CHECK(json.find("\"id\":\"harvest\"") != std::string::npos);
    CHECK(json.find("cooldown") == std::string::npos);
    CHECK(json.find("\"cost\"") == std::string::npos);
    CHECK(json.find("\"id\":\"balance\"") != std::string::npos);
    CHECK(std::count(json.begin(), json.end(), '{') ==
          std::count(json.begin(), json.end(), '}'));
    CHECK(std::count(json.begin(), json.end(), '[') ==
          std::count(json.begin(), json.end(), ']'));
}

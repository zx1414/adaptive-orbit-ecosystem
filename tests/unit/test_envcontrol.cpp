// agentsim - tests/unit/test_envcontrol.cpp
// R1-C：EnvControl + ParamSchema 单测（V2-SPEC-AMEND-1 语义：无影响力）。
// P0-5：事件语义整体重做 —— 撤下 harvest/plague/bloom（明确报错），winter 改成
// "有持续时间的环境变化"，并新增"三类时间戳 + 只撤事件偏移 + 暂停不耗时"的钉子。
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
using agentsim::app::EnvCommandResult;
using agentsim::app::EnvControl;
using agentsim::app::EnvEffectDef;
using agentsim::app::EnvEffectRequest;
using agentsim::app::EnvEffectState;
using agentsim::app::EnvEffectStatus;
using agentsim::app::EnvAxisLevel;
using agentsim::app::EnvFineKnob;
using agentsim::app::EnvState;
using agentsim::app::ParamDef;
using agentsim::app::ParamSchema;

// 执行一条命令的小工具：返回成功与否，顺带把回执带出来（错误码/事件请求/播种请求）。
bool runCmd(const std::string& key, const std::string& value, EnvState& st,
            std::vector<EnvChange>& out, std::string& err,
            EnvCommandResult* cmd = nullptr) {
    EnvCommandResult local;
    EnvCommandResult* slot = cmd != nullptr ? cmd : &local;
    return EnvControl::apply(key, value, st, out, err, slot);
}

// 事件时钟的单步推进辅助：模拟主循环一帧（盖戳 → 到期）。
void tickEffect(EnvEffectState& eff, long long tick) {
    EnvControl::stampEffectStart(eff, tick);
    EnvControl::expireEffect(eff, tick);
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

TEST(param_schema_size_matches_v2_plan) {
    // 规则 v2（P0/P2）删了 12 个键：113 - 12 = 101。钉具体数字而不是下限，
    // 这样"顺手塞一个键把数字补回去"会立刻被发现（见 ParamSchema.cpp 的注释）。
    CHECK(ParamSchema::all().size() == 101u);
    // 删掉的键必须真的查不到（不能只从 UI 隐藏、schema 里还留着）。
    static const char* const kRemoved[] = {
        "cyano_conversion_mode", "cyano_conversion_exponent",
        "cyano_conversion_speed_ref", "cyano_conversion_accel_ref",
        "cyano_conversion_w_speed", "cyano_conversion_w_accel",
        "cyano_conversion_min_rate", "cyano_conversion_max_rate",
        "ball_spawn_probability", "ball_spawn_cost",
        "ball_spawn_min_dist", "ball_spawn_max_dist",
        "migration_bonus", "migration_radius", "graze_refuge_energy",
    };
    for (const char* k : kRemoved) {
        CHECK(ParamSchema::find(k) == nullptr);
    }
    // 保留的同化率基率仍在（P0-1 的单一同化式用它）。
    CHECK(ParamSchema::find("cyano_conversion_rate") != nullptr);
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

TEST(env_effect_table_only_winter) {
    // P0-5：普通玩法的事件表只剩 winter（一个"环境变化"），且它只有环境字段。
    CHECK(EnvControl::effects().size() == 1u);
    const EnvEffectDef* winter = EnvControl::effect("winter");
    CHECK(winter != nullptr);
    if (winter != nullptr) {
        CHECK(winter->id == "winter");
        CHECK_NEAR(winter->lightOffset, -0.35, 1e-12);   // 幅度：压低光照基线
        CHECK(winter->durationFrames == 600);            // 持续：600 模拟帧
        CHECK(!winter->desc.empty());                    // 前端直接显示的文案
    }
    // 撤下的三个事件名不再是"已注册事件"。
    CHECK(EnvControl::effect("harvest") == nullptr);
    CHECK(EnvControl::effect("plague") == nullptr);
    CHECK(EnvControl::effect("bloom") == nullptr);
}

TEST(env_axis_levels_have_no_ball_spawn_keys) {
    // P2 提前收口：food 轴档位/细调里不得再有蓝细菌产球入口（那是"删了说明、
    // 实际还能调"的后门）。这里直接查表，防止以后有人顺手加回去。
    for (const EnvAxisDef& a : EnvControl::axes()) {
        for (const EnvAxisLevel& lv : a.levels) {
            for (const auto& kv : lv.values) {
                CHECK(kv.first.find("ball_spawn") == std::string::npos);
                CHECK(kv.first.find("cyano_ball_spawn") == std::string::npos);
            }
        }
        for (const EnvFineKnob& k : a.fine) {
            CHECK(k.key.find("ball_spawn") == std::string::npos);
            CHECK(k.key.find("cyano_ball_spawn") == std::string::npos);
        }
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

// ---------- EnvControl：事件（P0-5 重做）----------
// 三条验收要点（A5）在这里落成断言：
//   1. 触发只产出"环境偏移请求"，不产出任何参数变更、不碰能量（本模块没有能量口）；
//   2. 生效值 = 玩家基础值 + 事件偏移；到期只撤偏移，玩家基础值原样；
//   3. 时间戳三阶段（触发瞬间 / 后续模拟步 / 到期）都能从 status 读到；
//      暂停不推进 = 不调用 tickEffect 的一段时间不消耗持续帧数。

TEST(env_effect_winter_request_carries_environment_only) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    EnvCommandResult cmd;
    CHECK(runCmd("event.winter", "1", st, out, err, &cmd));
    CHECK(err.empty());
    CHECK(cmd.code.empty());
    CHECK(out.empty());                    // 不产出任何参数变更
    CHECK(cmd.effect.cancel == false);
    CHECK(cmd.effect.id == "winter");
    CHECK_NEAR(cmd.effect.lightOffset, -0.35, 1e-12);
    CHECK(cmd.effect.durationFrames == 600);
    CHECK(st.interventions == 1);
}

TEST(env_effect_retired_events_report_distinct_code) {
    // harvest / plague / bloom 必须是 retired_event（"曾经存在、现在撤下"），
    // 不能降级成 unknown_command（"你打错字了"）——两种解释对玩家不同。
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    for (const char* id : {"event.harvest", "event.plague", "event.bloom"}) {
        EnvCommandResult cmd;
        out.clear();
        CHECK(!runCmd(id, "1", st, out, err, &cmd));
        CHECK(cmd.code == "retired_event");
        CHECK(!err.empty());
        CHECK(out.empty());
        CHECK(cmd.effect.id.empty());       // 被拒即不产出任何请求（没有生效路径）
        CHECK(cmd.effect.cancel == false);
        CHECK(cmd.seed.requested == false);
    }
    // 真未知事件是另一个错误码。
    {
        EnvCommandResult cmd;
        CHECK(!runCmd("event.meteor", "1", st, out, err, &cmd));
        CHECK(cmd.code == "unknown_event");
    }
    // 撤下不等于"玩不了"：winter 仍然免费可用，连续触发不设冷却。
    for (int i = 0; i < 5; ++i) {
        CHECK(runCmd("event.winter", "1", st, out, err));
    }
    CHECK(st.interventions == 5);
}

TEST(env_effect_cancel_command) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    EnvCommandResult cmd;
    CHECK(runCmd("event.winter", "0", st, out, err, &cmd));   // =0 是撤销
    CHECK(cmd.effect.cancel);
    CHECK(cmd.effect.id == "winter");
    CHECK(out.empty());
    // 非法取值：明确报错，不静默当成 1。
    CHECK(!runCmd("event.winter", "abc", st, out, err));
    CHECK(!err.empty());
}

TEST(env_effect_duration_override) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    EnvCommandResult cmd;
    CHECK(runCmd("winter.duration", "1200", st, out, err));
    CHECK(st.effectDurationOverride == 1200);
    CHECK(runCmd("event.winter", "1", st, out, err, &cmd));
    CHECK(cmd.effect.durationFrames == 1200);   // 覆盖值生效
    // 越界与非法值明确报错。
    CHECK(!runCmd("winter.duration", "0", st, out, err));
    CHECK(!runCmd("winter.duration", "200000", st, out, err));
    CHECK(!runCmd("winter.duration", "nan", st, out, err));
    // 幂等：同值不重复计干预。
    const int before = st.interventions;
    CHECK(runCmd("winter.duration", "1200", st, out, err));
    CHECK(st.interventions == before);
}

TEST(env_effect_clock_three_stages) {
    // 触发瞬间 → 后续模拟步 → 到期。endTick 由主循环盖戳，触发瞬间还是 0。
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    EnvCommandResult cmd;
    CHECK(runCmd("event.winter", "1", st, out, err, &cmd));

    EnvEffectState eff;
    EnvControl::beginEffect(eff, cmd.effect);
    // --- 阶段 1：触发瞬间（还没被主循环观测到）---
    EnvEffectStatus s0 = EnvControl::effectStatus(eff, /*tick=*/0);
    CHECK(s0.active);
    CHECK(s0.id == "winter");
    CHECK(!s0.started);
    CHECK(s0.endTick == 0);
    CHECK(s0.remainingFrames == 600);          // 已触发：完整时长可见
    CHECK_NEAR(s0.lightOffset, -0.35, 1e-12);
    // --- 阶段 2：首个模拟帧盖戳，之后每帧递减 ---
    CHECK(EnvControl::stampEffectStart(eff, 100));
    CHECK(eff.endTick == 700);
    EnvEffectStatus s1 = EnvControl::effectStatus(eff, 100);
    CHECK(s1.started);
    CHECK(s1.remainingFrames == 600);
    EnvEffectStatus s2 = EnvControl::effectStatus(eff, 400);
    CHECK(s2.remainingFrames == 300);
    CHECK(s2.active);
    // --- 阶段 3：到期（tick == endTick 就撤）---
    CHECK(!EnvControl::expireEffect(eff, 699));   // 差一帧还不撤
    CHECK(eff.active);
    CHECK(EnvControl::expireEffect(eff, 700));
    CHECK(!eff.active);
    EnvEffectStatus s3 = EnvControl::effectStatus(eff, 700);
    CHECK(!s3.active);
    CHECK(s3.remainingFrames == 0);
    CHECK_NEAR(s3.lightOffset, 0.0, 1e-12);
}

TEST(env_effect_pause_does_not_consume_duration) {
    // 关键钉子（lead 点名）：暂停时主循环不推进 → 不调用 tickEffect →
    // 持续帧数不消耗。否则 A5 的"到期时刻"在真实客户端里不可复现。
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    EnvCommandResult cmd;
    CHECK(runCmd("event.winter", "1", st, out, err, &cmd));
    EnvEffectState eff;
    EnvControl::beginEffect(eff, cmd.effect);
    EnvControl::stampEffectStart(eff, 50);      // 起算于 tick 50，到期 650
    // 模拟"暂停 10000 帧"：这期间一次 tickEffect 都不调用 → 状态原样。
    const EnvEffectState frozen = eff;
    EnvEffectStatus s = EnvControl::effectStatus(eff, 50);
    CHECK(s.remainingFrames == 600);
    CHECK(eff.endTick == frozen.endTick);
    CHECK(eff.active == frozen.active);
    // 恢复后 tick 仍从 50 继续（暂停不推进模拟时间）→ 剩余仍是 600 起算。
    EnvControl::stampEffectStart(eff, 51);      // 幂等：已盖戳不再改
    CHECK(eff.endTick == 650);
    CHECK(EnvControl::effectStatus(eff, 51).remainingFrames == 599);
    // 到期判定用的是模拟 tick，不是墙钟。
    CHECK(!EnvControl::expireEffect(eff, 649));
    CHECK(EnvControl::expireEffect(eff, 650));
}

TEST(env_effect_only_offset_is_removed_player_base_kept) {
    // 生效值 = 玩家基础值 + 事件偏移；到期只撤偏移，玩家基础值（哪怕是
    // 事件期间改过的新值）继续有效。
    EnvEffectState eff;
    EnvEffectRequest req;
    req.id = "winter";
    req.lightOffset = -0.35;
    req.durationFrames = 600;
    EnvControl::beginEffect(eff, req);
    EnvControl::stampEffectStart(eff, 0);
    CHECK_NEAR(EnvControl::effectiveSunBase(1.0, eff), 0.65, 1e-12);
    // 玩家在事件期间把基线从 1.0 改成 2.0：生效值跟着走。
    CHECK_NEAR(EnvControl::effectiveSunBase(2.0, eff), 1.65, 1e-12);
    // 到期：只撤偏移 → 生效值 = 玩家保留的 2.0（不是回到 1.0）。
    CHECK(EnvControl::expireEffect(eff, 600));
    CHECK_NEAR(EnvControl::effectiveSunBase(2.0, eff), 2.0, 1e-12);
    // 下限夹到 0：极夜（0.05）+ 寒冬（-0.35）不能变成负光照。
    EnvControl::beginEffect(eff, req);
    CHECK_NEAR(EnvControl::effectiveSunBase(0.05, eff), 0.0, 1e-12);
    // 撤销命令同样只撤偏移。
    CHECK(EnvControl::clearEffect(eff));
    CHECK_NEAR(EnvControl::effectiveSunBase(0.9, eff), 0.9, 1e-12);
    CHECK(!EnvControl::clearEffect(eff));   // 已不在生效：返回 false
}

TEST(env_effect_renew_restarts_timer) {
    EnvEffectState eff;
    EnvEffectRequest req;
    req.id = "winter";
    req.lightOffset = -0.35;
    req.durationFrames = 600;
    EnvControl::beginEffect(eff, req);
    EnvControl::stampEffectStart(eff, 0);
    CHECK(EnvControl::effectStatus(eff, 300).remainingFrames == 300);
    // 续期：重新 beginEffect（endTick 归零）→ 下次盖戳按当前 tick 重算。
    EnvControl::beginEffect(eff, req);
    CHECK(eff.endTick == 0);
    CHECK(EnvControl::effectStatus(eff, 300).remainingFrames == 600);
    EnvControl::stampEffectStart(eff, 300);
    CHECK(eff.endTick == 900);
}

TEST(env_seed_is_external_input_not_event) {
    // 玩家播种：独立命令、独立请求类型、不产生参数变更、不占用 event.*。
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    EnvCommandResult cmd;
    CHECK(runCmd("seed.cyano", "120", st, out, err, &cmd));
    CHECK(err.empty());
    CHECK(out.empty());
    CHECK(cmd.seed.requested);
    CHECK(cmd.seed.kind == "cyano");
    CHECK(cmd.seed.count == 120);
    CHECK(cmd.effect.id.empty());       // 不是事件
    CHECK(st.seedsRequested == 120);
    // 越界 / 非法 / 未知目标：明确报错。
    CHECK(!runCmd("seed.cyano", "0", st, out, err));
    CHECK(!runCmd("seed.cyano", "9999", st, out, err));
    CHECK(!runCmd("seed.cyano", "abc", st, out, err));
    {
        EnvCommandResult bad;
        CHECK(!runCmd("seed.fish", "10", st, out, err, &bad));
        CHECK(bad.code == "unknown_seed");
    }
}

TEST(env_unknown_command_has_code) {
    EnvState st;
    std::vector<EnvChange> out;
    std::string err;
    EnvCommandResult cmd;
    CHECK(!runCmd("bogus", "1", st, out, err, &cmd));
    CHECK(cmd.code == "unknown_command");
    CHECK(!err.empty());
    CHECK(!runCmd("axis", "1", st, out, err));
    CHECK(!runCmd("fine.", "1", st, out, err));
    CHECK(!runCmd("fine.nope", "1", st, out, err));
    CHECK(st.interventions == 0);
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
    // light 2 + food 1 + pressure 1 + producer 1 = 5。
    // P2 提前收口后 food 轴只剩 ball_energy 一个键（产球概率/成本已删）。
    CHECK(axisKeyCount == 5);
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

// ---------- EnvControl：tickFrame / toJson ----------

TEST(env_tick_frame_advances_frames_only) {
    // 修正 1：无影响力恢复；tickFrame 只推进帧计数。
    EnvState st;
    for (int i = 0; i < 10; ++i) EnvControl::tickFrame(st);
    CHECK(st.frames == 10);
    CHECK(st.interventions == 0);
}

TEST(env_tojson_shape_v2) {
    EnvState st;
    st.interventions = 7;
    st.peakAlive = 42;
    st.axisLevels[0] = 3;
    st.cyanoOn = false;
    EnvEffectState eff;
    EnvEffectRequest req;
    req.id = "winter";
    req.lightOffset = -0.35;
    req.durationFrames = 600;
    EnvControl::beginEffect(eff, req);
    EnvControl::stampEffectStart(eff, 100);
    const EnvEffectStatus status = EnvControl::effectStatus(eff, 400);
    const std::string json = EnvControl::toJson(
        st, true, false, 64, 120, status,
        EnvControl::effectiveSunBase(1.0, eff), 1.0);
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
    CHECK(json.find("\"id\":\"balance\"") != std::string::npos);
    // P0-5：事件表只剩 winter；撤下的三个只出现在 retiredEvents（迁移说明）里，
    // 绝不出现在 events 数组里（否则前端会照旧渲染按钮）。
    const size_t evBegin = json.find("\"events\":[");
    const size_t evEnd = json.find(",\"retiredEvents\":[");
    CHECK(evBegin != std::string::npos);
    CHECK(evEnd != std::string::npos);
    CHECK(evBegin < evEnd);
    const std::string evSection =
        json.substr(evBegin, evEnd == std::string::npos ? std::string::npos : evEnd - evBegin);
    CHECK(evSection.find("\"id\":\"winter\"") != std::string::npos);
    CHECK(evSection.find("\"id\":\"harvest\"") == std::string::npos);
    CHECK(evSection.find("\"id\":\"plague\"") == std::string::npos);
    CHECK(evSection.find("\"id\":\"bloom\"") == std::string::npos);
    CHECK(evSection.find("\"kind\":\"environment\"") != std::string::npos);
    // 旧事件的迁移说明放在 retiredEvents 里（前端据此不渲染按钮）。
    CHECK(json.find("\"retiredEvents\"") != std::string::npos);
    CHECK(json.find("\"retiredEvents\":[{\"id\":\"harvest\"") != std::string::npos);
    // 三段光照并列 + 事件剩余时长。
    CHECK(json.find("\"sunBaseEffective\":0.65") != std::string::npos);
    CHECK(json.find("\"sunBasePlayer\":1.0") != std::string::npos);
    CHECK(json.find("\"sunBaseEventOffset\":-0.35") != std::string::npos);
    CHECK(json.find("\"remainingFrames\":300") != std::string::npos);
    CHECK(json.find("\"endTick\":700") != std::string::npos);
    CHECK(json.find("\"started\":true") != std::string::npos);
    // 外部输入标注（播种不是自然生长也不是事件奖励）。
    CHECK(json.find("\"externalInputs\"") != std::string::npos);
    CHECK(json.find("\"externalInput\"") != std::string::npos);
    // 旧事件的能量字段彻底消失（结构上不可能再直接改核能量）。
    CHECK(json.find("perNucleus") == std::string::npos);
    CHECK(json.find("onlyRich") == std::string::npos);
    CHECK(json.find("cyanoNodesToAdd") == std::string::npos);
    CHECK(json.find("capPerEvent") == std::string::npos);
    CHECK(json.find("cooldown") == std::string::npos);
    CHECK(json.find("\"cost\"") == std::string::npos);
    CHECK(std::count(json.begin(), json.end(), '{') ==
          std::count(json.begin(), json.end(), '}'));
    CHECK(std::count(json.begin(), json.end(), '[') ==
          std::count(json.begin(), json.end(), ']'));
}

TEST(env_tojson_inactive_effect_shape) {
    // 没有事件生效时：effect.active=false、remaining=0、事件偏移 0，
    // 生效值直接等于玩家基础值（到期后的正常状态）。
    EnvState st;
    EnvEffectState eff;
    const EnvEffectStatus status = EnvControl::effectStatus(eff, 1234);
    const std::string json = EnvControl::toJson(
        st, true, false, 0, 0, status, EnvControl::effectiveSunBase(1.8, eff), 1.8);
    CHECK(json.find("\"effect\":{\"active\":false") != std::string::npos);
    CHECK(json.find("\"remainingFrames\":0") != std::string::npos);
    CHECK(json.find("\"sunBaseEventOffset\":0.0") != std::string::npos);
    CHECK(json.find("\"sunBaseEffective\":1.8") != std::string::npos);
    CHECK(json.find("\"sunBasePlayer\":1.8") != std::string::npos);
}

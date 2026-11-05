// agentsim - app/RuntimeEnv.h
// R3-B【实时调控接线】核心：EnvControl 产出的 EnvChange{key,value} -> SimConfig 字段。
//
// 职责边界（任务卡 R3-B）：
//   - EnvControl（R1-C）只产出"参数变更描述"，不知道任何 Config 字段；
//   - 本模块是**唯一**知道 key -> 字段映射的地方（docs/PARAM-MAPPING.md 的执行者）；
//   - 不 include simulation/世界状态：调用方（main.cpp --serve 循环）拿着
//     SimConfig 来，改完交给 Simulation::applyConfig 下一帧生效。
//
// 映射实现：一张静态表（key -> 写入函数指针），一次构建、查表执行。
// 不写 113 个 if-else（PARAM-MAPPING.md 的硬要求）。
//
// key 分四类（详见 RuntimeEnv.cpp 顶部映射总账）：
//   1) 已接线：写入 cfg 对应字段，返回 true；
//   2) 明确拒绝（世界几何/种子/帧数/废弃键）：返回 false，**不**进 unwiredKeys
//      ——"拒绝"是设计意图，不是"没接线"；
//   3) cyano_enabled（蓝细菌总开关）：接进 RuntimeTuning.cyanoEnabled
//      （运行中暂停蓝细菌代谢属运行面开关，非世界构造参数）；
//   4) 未知键：返回 false，打一次 stderr 警告并记入 unwiredKeys（去重）。
//
// 线程约定：与 HttpServer 相同——所有调用发生在单线程（HTTP 线程内同步处理），
// unwired 集合无锁。确定性红线：不消耗任何 RNG。
//
// 【v231 精神：调参零门槛】本模块完全免费、无预算、无冷却
//（V2-SPEC-AMEND-1 修正 1 已删除影响力系统，RuntimeEnv 只是记账员不是守门员）。
#pragma once
#include <string>
#include <vector>

#include "EnvControl.h"   // EnvChange（本模块的输入契约）
#include "core/Config.h"  // SimConfig（本模块的输出契约）

namespace agentsim::app {

class RuntimeEnv {
public:
    // 应用一批变更；返回成功条数（applyOne 返回 true 的次数）。
    // 失败的条目（拒绝/未知）不影响同批其它条目——"一批里混有合法/非法 key，
    // 合法的仍生效"是验收用例。
    static int applyAll(SimConfig& cfg, const std::vector<EnvChange>& changes);

    // 单条变更。key 已接线 -> 写入并返回 true；拒绝名单/未知键 -> false。
    // 未知 key 首次出现时打 stderr 警告（每次进程生命周期每 key 恰一次）。
    static bool applyOne(SimConfig& cfg, const std::string& key, double value);

    // 诊断：出现过但未接线的 key（去重，按首次出现顺序）。
    // GET /env 可暴露；门禁与单测可查（验收：未知 key 返回 false 且进 unwiredKeys）。
    static std::vector<std::string> unwiredKeys();

    // 清空诊断集合（单测隔离用）。
    static void resetUnwired();

    // 供 --serve 启动横幅：已接线 key 数（自检"映射表没写漏一半"）。
    static int wiredKeyCount();
};

} // namespace agentsim::app

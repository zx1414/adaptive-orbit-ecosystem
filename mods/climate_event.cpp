#include "mod_api.h"
#include "World.h"

#include <iostream>
#include <random>
#include <string>

// ============================================================
// mod id:        climate_event
// 名称:          气候突变
// 机制:          每 kEventInterval(800) 帧用 api.rng 抽取一次全局环境事件，
//                三选一（丰收 40% / 寒冬 35% / 瘟疫 25%），然后调用
//                api.world.applyEnvEvent(kind, /*playerInitiated=*/false, err)。
// 为什么改为事件适配器（v2.2.0）:
//                环境调控把丰收/寒冬/瘟疫做成了玩家事件卡（World::applyEnvEvent）。
//                本 mod 不再自己遍历核改能量，而是复用同一实现：
//                  - 事件效果/上限/能量账目只有一处，玩家事件与自动事件不重复结算；
//                  - 自动事件 playerInitiated=false：不扣影响力、不受玩家冷却限制；
//                  - 能量变更统一经 EnergySystem（EnergyReason::BOOM），账目可对账。
// 唯一随机源:     api.rng（std::mt19937&）。事件类型每次触发只抽 1 次，
//                不逐核抽取，避免大量消耗共享 rng 序列；同种子可复现。
//                禁止 rand()/random_device/time()/自建引擎。
// touches:       BOOM
// incompatible:
// 依赖:          事件适配器（与玩家事件卡共用 World::applyEnvEvent）
// 【状态】纯无状态：事件类型由「帧号 + api.rng」决定，无跨帧变量，无 save/load 钩子。
// ============================================================

namespace {

// ---- 可调常数（全部编译期常量：无静态可变全局、无初始化副作用）----
constexpr int    kEventInterval = 800;    // 事件间隔（帧）
constexpr double kHarvestProb   = 0.40;   // 丰收概率
constexpr double kWinterProb    = 0.35;   // 寒冬概率
                                          // 瘟疫概率 = 1 - 0.40 - 0.35 = 0.25

const char* eventName(EnvEventKind e) {
    switch (e) {
        case EnvEventKind::Harvest: return "丰收";
        case EnvEventKind::Winter:  return "寒冬";
        case EnvEventKind::Plague:  return "瘟疫";
        default:                    return "未知";
    }
}

}  // anonymous namespace

void registerClimateEvent(ModAPI& api) {
    // 注册即向 GET /env 上报"自动气候已启用"（下一事件在 kEventInterval 帧后）。
    setEnvModStatus(EnvModStatus{true, kEventInterval});
    api.events.subscribe(EventType::FRAME_END, [&](const SimEvent& e) {
        if (api.world.finished()) return;
        // 每帧更新 "距下一次自动事件还有多少帧"，供前端 mods.nextClimateIn 显示。
        // 本 mod 是唯一知道 kEventInterval 的地方，状态由本 mod 自报，避免启动器复制常量。
        setEnvModStatus(EnvModStatus{true, kEventInterval - (e.frame % kEventInterval)});
        if (e.frame <= 0) return;
        if (e.frame % kEventInterval != 0) return;

        // 【每次事件只抽一次 rng】随机源唯一且强制：api.rng。
        std::uniform_real_distribution<double> pick(0.0, 1.0);
        const double r = pick(api.rng);
        EnvEventKind kind;
        if (r < kHarvestProb) {
            kind = EnvEventKind::Harvest;
        } else if (r < kHarvestProb + kWinterProb) {
            kind = EnvEventKind::Winter;
        } else {
            kind = EnvEventKind::Plague;  // 0.40+0.35=0.75 → 剩余 25%
        }

        // 复用玩家事件卡的同一实现：自动事件不扣影响力、不吃玩家冷却。
        // 能量只经 EnergySystem 变更（World 内部走 EnergyReason::BOOM）。
        std::string err;
        const bool ok = api.world.applyEnvEvent(kind, /*playerInitiated=*/false, err);

        std::cout << "[boom] frame " << e.frame << " 事件=" << eventName(kind)
                  << " 存活核=" << api.world.aliveNucleusCount();
        if (ok) {
            std::cout << " 结果=已生效\n";
        } else {
            std::cout << " 结果=跳过(" << (err.empty() ? "未生效" : err) << ")\n";
        }
    });
}

// 纯无状态：不提供 saveClimateEvent / loadClimateEvent。

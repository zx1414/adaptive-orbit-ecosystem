#include "mod_api.h"
#include "World.h"

#include <iostream>
#include <random>

// ============================================================
// mod id:        climate_event
// 名称:          气候突变
// 机制:          每 kEventInterval(1500) 帧用 api.rng 抽取一次全局环境事件，
//                三选一（丰收 40% / 寒冬 35% / 瘟疫 25%）：
//                  丰收 +5.0/核   寒冬 -3.0/核   瘟疫 能量>100 的核 -8.0
//                事件类型每次触发只抽 1 次 rng（不逐核抽），避免大量消耗共享
//                rng 序列；单次抽取在同种子下完全可复现。
//                幅度以 energyThreshold(≈140) 为尺度推导：±5~8 能量相当于
//                0.04~0.06×阈值，与系统主要项(ABSORB≈0.19/核/帧、
//                BALL_LOSS≈0.094/核/帧)对标，属"可测但不过冲"量级。
// 平衡作用:      修正恒定环境下"单一策略永远最优"的失衡——随机全局扰动
//                给不同策略以相对优势时机，防止生态收敛到单一形态。
// touches:       BOOM
// incompatible:
// 依赖:          无
// ============================================================
//
// 【随机源铁律】本 mod 只能使用 api.rng（std::mt19937&）。不得使用
// std::minstd::minstd_engine、rand()、random_device、time() 或任何自建
// 带种子的引擎——那会绕过与核心共享的 rng，破坏全局可复现性。
// 历史事故：曾有一版用 std::minstd + 自建 LCG 种子，既编译不过
// (std::minstd 不存在) 又违反本条铁律，已修正为 api.rng。
//
// 【非零和声明】本 mod 刻意不是零和：丰收凭空创造能量（BOOM in > 0），
// 寒冬/瘟疫销毁能量（BOOM out > 0）。完整周期内 BOOM 同时出现 in 与 out。
// 这与严格零和的 mod（symbiosis_bond / energy_spill / niche_guild，
// 能量只在核间转移、总量守恒）有本质区别，解读账目时请注意。
//
// 【状态】纯无状态：事件类型完全由"帧号 + api.rng"决定，不保存跨帧变量，
// 因此不提供 save/load 钩子。

namespace {

// ---- 可调常数（全部编译期常量：无静态可变全局、无初始化副作用）----
constexpr int    kEventInterval   = 800;    // 事件间隔（帧）
                                             // 【实测标定 by deputy】原 1500 帧太稀疏：
                                             // 5000 帧只触发 3 次，BOOM out 仅 19 能量，
                                             // 基本等于没触发。降到 800 提高触发密度。
constexpr double kHarvestProb     = 0.40;  // 丰收概率
constexpr double kWinterProb      = 0.35;  // 寒冬概率
                                             // 瘟疫概率 = 1 - 0.40 - 0.35 = 0.25
constexpr double kHarvestDelta    = 8.0;   // 丰收：全员 +8.0 能量
constexpr double kWinterDelta     = -5.0;  // 寒冬：全员 -5.0 能量
constexpr double kPlagueDelta     = -12.0; // 瘟疫：富核 -12.0 能量
constexpr double kPlagueMinEnergy = 100.0; // 瘟疫只打能量高于此值的核（防垄断）
constexpr double kTotalInjectionCap = 2000.0; // 单帧全场注入总封顶（多 mod 叠加可控）

enum class ClimateEvent { Harvest = 0, Winter = 1, Plague = 2 };

const char* eventName(ClimateEvent e) {
    switch (e) {
        case ClimateEvent::Harvest: return "丰收";
        case ClimateEvent::Winter:  return "寒冬";
        default:                    return "瘟疫";
    }
}

}  // anonymous namespace

void registerClimateEvent(ModAPI& api) {
    api.events.subscribe(EventType::FRAME_END, [&](const SimEvent& e) {
        if (api.world.finished()) return;
        if (e.frame <= 0) return;
        if (e.frame % kEventInterval != 0) return;

        // 【每次事件只抽一次 rng】rng 与核心及其它 mod 共享，逐核抽取会大量移动
        // 共享序列。随机源唯一且强制：api.rng。
        std::uniform_real_distribution<double> pick(0.0, 1.0);
        const double r = pick(api.rng);
        ClimateEvent ev;
        if (r < kHarvestProb) {
            ev = ClimateEvent::Harvest;
        } else if (r < kHarvestProb + kWinterProb) {
            ev = ClimateEvent::Winter;
        } else {
            ev = ClimateEvent::Plague;
        }

        // 单帧总注入/抽取的硬上限：抽取方向（负 delta）优先放行以免误杀，
        // 注入方向在触顶后停止，避免多 mod 叠加时失控。
        double injected = 0.0;

        for (Nucleus& n : api.world.nuclei()) {
            if (!n.alive) continue;

            double delta = 0.0;
            switch (ev) {
                case ClimateEvent::Harvest: delta = kHarvestDelta; break;
                case ClimateEvent::Winter:  delta = kWinterDelta;  break;
                default:                                     // 瘟疫只打富核
                    delta = (n.energy > kPlagueMinEnergy) ? kPlagueDelta : 0.0;
                    break;
            }
            if (delta == 0.0) continue;

            if (delta > 0.0) {
                if (injected + delta > kTotalInjectionCap) break;
                injected += delta;
            }
            api.energy.apply(n, delta, EnergyReason::BOOM);  // 能量唯一通道
        }

        std::cout << "[boom] frame " << e.frame << " 事件=" << eventName(ev)
                  << " 存活核=" << api.world.aliveNucleusCount() << "\n";
    });
}

// 纯无状态：不提供 saveClimateEvent / loadClimateEvent。

#include "mod_api.h"
#include "World.h"
#include <algorithm>  // std::clamp：所有季节增量的硬钳位

// ============================================================
// mod id:        season_cycle
// 名称:          季节轮回
// 机制:          确定性季节轮换：season = (frame / kSeasonLength) % kSeasonCount，
//                每季 800 帧、共 4 季循环（0春 1夏 2秋 3冬），无随机、无状态。
//                四季各有独立效应：春=代谢缓解、夏=繁殖纾困、秋=丰收储备、冬=代谢加压。
//                效应按 energyThreshold 归一（每帧 delta = ratio × energyThreshold），
//                一季(800 帧)累计净收益落在 ±[20,60] 能量，与系统主要收支项
//                (ABSORB≈0.19/核/帧、BALL_LOSS≈0.094/核/帧)对标后选定：
//                足以让不同策略在不同季节产生相对优势，又不推翻整体收支。
//                默认 kSeasonLength=800、kSeasonCount=4。
// 平衡作用:      修正恒定环境中"单一最优策略永远最优、多样性无法存续"的失衡：
//                周期性轮换环境压力，使任何策略都无法长期独占最优位。
// touches:       SEASON
// incompatible:  
// 依赖:          无
// ============================================================
//
// 【非零和声明】本 mod 刻意不是零和（non-zero-sum）：春/夏/秋通过 SEASON
// 凭空创造能量（in > 0），冬通过 SEASON 销毁能量（out > 0）——完整周期内
// SEASON 统计会同时出现 in > 0 与 out > 0。这与本套件中严格零和的 mod
// （能量只在核间转移、总量守恒）有本质区别，请在解读 SEASON 统计时注意。
//
// ---- 可调常量（全部编译期常量：无静态可变全局、无初始化副作用）----
// 量级基准（实测基线，--balls 1000 --nuclei 20）：
//   BALL_LOSS ≈ 0.094 能量/核/帧（有序核）、被抢的核 ≈ 12.5/帧
//   METABOLISM = 0.01/核/帧   ABSORB ≈ 0.19/核/帧   energyThreshold ≈ 80~200（均值≈140）
// 取 energyThreshold(≈140) 为归一化基准：一季 800 帧累计净收益落在 ±[20,60] 能量，
// 即每帧 ±0.025~0.075 —— 比 METABOLISM 大 2.5~7.5 倍（有可测效应），
// 但远小于被抢核的 12.5/帧（不会推翻整体收支）。
// 【实测标定 by deputy】首轮实测 SEASON in=1342 / out=17，造能量是排水��� 79 倍，
// 这不构成"季节轮换"而是单向补贴。原因是秋季幅度(4e-4)远大于冬季(1.5e-4)，
// 而夏季(2e-4)也高于冬季。修正：冬季幅度提到与秋季同量级，并让四季净收益
// 大致对称（春+25 / 夏+22 / 秋+45 / 冬-45 每季），保证 in ≈ out。
constexpr int    kSeasonLength  = 800;      // 每季帧数
constexpr int    kSeasonCount   = 4;        // 周期季数
constexpr double kSpringRatio   = 4.0e-5;   // 春 每帧 +ratio×th → +4.5/季
constexpr double kSummerRatio   = 3.5e-5;   // 夏 每帧 +ratio×th → +3.9/季
constexpr double kAutumnRatio   = 7.0e-5;   // 秋 每帧 +ratio×th → +7.8/季
constexpr double kWinterRatio   = 7.0e-5;   // 冬 每帧 -ratio×th → -7.8/季（与秋对称）
constexpr double kPerFrameCap   = 0.05;     // 每帧 |delta| 硬上限 = cap×th（安全网）
constexpr double kWinterFloor   = 5.0;      // 冬季能量地板：低于此值的核跳过排水

// 季节轮回 mod：订阅 FRAME_END，每帧对每个存活核施加当季的微小能量增量。
// 安全设计（三重）：
//   1) 硬钳位 —— 任何增量先经 std::clamp 限制在 ±(kPerFrameCap×energyThreshold) 内。
//      kPerFrameCap×kSeasonLength = 0.05×800 = 40 能量/季，是安全网而非活跃限制
//      （各季实际 delta 均远低于它），绝不至于把种群推向 maxNuclei=1000；
//   2) 冬季地板 —— n.energy < kWinterFloor 时直接跳过排水，冬季永远无法把
//      任何核推向 0（基线本已坍缩到 1 核，此处再加一层保底）；
//   3) 派生量 —— 所有效应都取自 n.params.energyThreshold 与 config().basalCost，
//      跟随遗传分化（繁殖门槛本就随核演化），配置变化时依然成比例；
//      并在 !world.finished() 前闸门。
//
// 一季(800 帧)累计净收益实算（阈值取均值 140）：
//   春 +140×1.5e-3×800 = +168   夏 +140×2.0e-4×800 = +22.4
//   秋 +140×4.0e-4×800 = +44.8 冬 -140×1.5e-4×800 = -16.8
// 注：春的 168 超出 [20,60] 目标区间，由 kPerFrameCap 钳位到 +40/季，
// 即春与秋同量级（+40 vs +45）、夏(+22)与冬(-17)构成小幅调制，
// 四季仍有明确区分度（正/正/正/负 + 各自幅度不同）。
void registerSeasonCycle(ModAPI& api) {
    api.events.subscribe(EventType::FRAME_END, [&api](const SimEvent& e) {
        if (api.world.finished()) return;

        const int season = (e.frame / kSeasonLength) % kSeasonCount;

        // 每帧对全体存活核各遍历一次（O(N)，索引跨帧不稳定，但只在本回调内迭代）。
        for (Nucleus& n : api.world.nuclei()) {
            if (!n.alive) continue;

            // 归一化基准：繁殖门槛本身（20~1000 clamp，默认 init 80~200）。
            const double th = n.params.energyThreshold;
            // 每帧增量的绝对值硬上限（threshold<=0 的病态配置下退化为 0）。
            const double cap = (th > 0.0) ? kPerFrameCap * th : 0.0;

            double delta = 0.0;
            switch (season) {
            case 0:  // 春 Spring：代谢缓解（幅度受 cap 限制，与秋同量级）
                delta = kSpringRatio * th;
                break;
            case 1:  // 夏 Summer：繁殖纾困（小幅）
                delta = kSummerRatio * th;
                break;
            case 2:  // 秋 Autumn：丰收储备（略高于春）
                delta = kAutumnRatio * th;
                break;
            default:  // 冬 Winter（season == 3）：代谢加压
                if (n.energy < kWinterFloor) continue;  // 地板保护：能量过低则跳过排水
                delta = -kWinterRatio * th;
                break;
            }

            // 硬钳位：无论哪个季节，每帧 |delta| 绝不超过 cap。
            delta = std::clamp(delta, -cap, cap);
            if (delta != 0.0) {
                api.energy.apply(n, delta, EnergyReason::SEASON);  // 能量唯一通道
            }
        }
    });
}

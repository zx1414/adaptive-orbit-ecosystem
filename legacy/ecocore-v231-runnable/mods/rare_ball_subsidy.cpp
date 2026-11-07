#include "mod_api.h"
#include "World.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <unordered_map>
#include <vector>

// ============================================================
// mod id:        rare_ball_subsidy
// 名称:          稀有球型保育
// 机制:          每帧统计全场球型分布得稀缺度倍率（该型占比越低倍率越高，
//                上限 2.0）。对每个核，在其 absorbRadius 内统计**归属于该核**
//                的各类球；FRAME_END 与 FRAME_START 的差分 = 本帧被该核吸收的
//                各型球数。对其中"稀有偏好型"（absorbPreference[t] > 1.2）的
//                球给予补贴：每颗 kSubsidyPerBall(0.35) × 稀缺度倍率。
//                单核单帧补贴封顶 kMaxSubsidyPerFrame(3.0)。
// 平衡作用:      修正三类球完美均匀生成(World.cpp spawnFreeBall 用
//                uniform_int_distribution(0,2))导致的无稀缺/无生态位分化——
//                把"专一吸收高价值球型"变成正反馈，驱动球型生态位分化。
// touches:       SUBSIDY
// incompatible:
// 依赖:          无
// ============================================================
//
// 【非零和声明】本 mod 刻意不是零和：补贴是从虚空注入的正反馈（SUBSIDY 只会有
// in > 0），目的是把"吸收稀有球型"变成值得追求的行为。这与严格零和的 mod
// （symbiosis_bond / energy_spill / niche_guard，能量只在核间转移、总量守恒）
// 有本质区别，解读账目时请注意。
//
// 【关键正确性约束】核心 World::absorbBalls (World.cpp:303) 只吸收
// **已归属**（ownerId >= 0）的球；自由球（ownerId == -1）永远不会被吸收。
// 所以本 mod 的计数口径必须是"归属于该核的球"，绝不能只数自由球——
// 否则测的是"球漂进漂出范围"，与吸收无关，会给幻影吸收发补贴。
//
// 【成本】每帧 O(B + N + C)，其中 B=球数、N=核数、C=总球型数；
// 绝无 O(N×B)：全局归属类型表只遍历一次球表，各核只查自己的桶。

namespace {

// ---- 可调常数 ----
constexpr double kRareThreshold      = 1.2;   // absorbPreference 超过此值 = 稀有偏好型
constexpr double kSubsidyPerBall     = 0.35;  // 每吸收一颗稀有球的基础补贴
constexpr double kMaxSubsidyPerFrame = 3.0;   // 单核单帧补贴上限（防刷）
constexpr double kMaxScarcityMult    = 2.0;   // 稀缺度倍率上限
constexpr int    kPruneInterval      = 120;   // 每 N 帧清理一次死核追踪项
constexpr std::size_t kMaxTracked     = 4096; // 追踪表硬上限（防内存无界）

// 每核的跨帧快照：上帧归属于该核的各型球数。
struct NucleusSnapshot {
    std::array<int, 3> ownedByType{{0, 0, 0}};
    int lastSeenFrame = 0;
};

// 全局稀缺度：某型球在全场（含归属）中的占比 -> 倍率。
// 均匀分布时每型占 1/3 -> 倍率 1.0；越稀缺倍率越高，封顶 kMaxScarcityMult。
double scarcityMultiplier(std::array<int, 3> const& globalTypeCount) {
    int total = 0;
    for (int t = 0; t < 3; ++t) total += globalTypeCount[static_cast<std::size_t>(t)];
    if (total <= 0) return 1.0;
    const double ratio = static_cast<double>(globalTypeCount[0]) / static_cast<double>(total);
    // ratio == 1/3 (均匀) -> 1.0；ratio -> 0 -> kMaxScarcityMult；ratio 偏高 -> 仍为 1.0
    double mult = 1.0 + (kMaxScarcityMult - 1.0) * (1.0 - ratio * 3.0);
    if (mult < 1.0) mult = 1.0;
    if (mult > kMaxScarcityMult) mult = kMaxScarcityMult;
    return mult;
}

// 跨帧快照表：key = 稳定 Nucleus::id（绝不用数组下标，下标每帧 swap-pop 变化）。
// 【生命周期铁律】必须是 static —— EventBus 里的 std::function 比 World 活得更久
// （见 mod_api.h:21-22）。若用 registerXxx 的局部变量 + [&] 捕获，函数返回后
// 变量即销毁，回调访问悬垂引用 → 必然 0xC0000005 访问违例。
// 正确对照组：ball_loss_relief.cpp 的 `static ReliefState state;`。
struct SubsidyState {
    std::unordered_map<int, NucleusSnapshot> snapshots;
    SubsidyState() { snapshots.reserve(kMaxTracked); }
};

}  // anonymous namespace

void registerRareBallSubsidy(ModAPI& api) {
    // 函数内 static：registerXxx 每进程调用一次，值初始化，无初始化副作用。
    static SubsidyState state;

    api.events.subscribe(EventType::FRAME_START, [&](const SimEvent& e) {
        const double absorbRadius = api.world.config().absorbRadius;
        const double r2 = absorbRadius * absorbRadius;
        const std::size_t nCount = api.world.nuclei().size();
        const std::size_t bCount = api.world.balls().size();

        // 本帧归属球的类型分布（供稀缺度倍率使用）。声明在**回调体内**，
        // 每帧重建，不跨帧持有——因此不涉及捕获生命周期问题。
        std::array<int, 3> globalTypeCount{{0, 0, 0}};

        // ---- 第 1 遍（O(N+B)）：一次性建立 下标 -> 各型球数 的桶表 ----
        // 桶表 key 是 balls 下标；只有归属于活核的球才计入（与核心吸收口径一致）。
        std::vector<std::array<int, 3>> ownedTypeByIndex(bCount);
        for (std::size_t i = 0; i < bCount; ++i) {
            const Ball& b = api.world.balls()[i];
            if (b.ownerId < 0 || static_cast<std::size_t>(b.ownerId) >= nCount) continue;
            const Nucleus& owner = api.world.nuclei()[static_cast<std::size_t>(b.ownerId)];
            if (!owner.alive) continue;  // 死核的球不会被吸收
            const int t = static_cast<int>(b.type);
            if (t < 0 || t >= 3) continue;
            ++ownedTypeByIndex[i][static_cast<std::size_t>(t)];
            ++globalTypeCount[static_cast<std::size_t>(t)];
        }

        // 全场稀缺度倍率：本帧一次性计算，三个类型共用。
        const double sharedScarcity = scarcityMultiplier(globalTypeCount);
        std::array<double, 3> scarcityByType{{sharedScarcity, sharedScarcity, sharedScarcity}};
        (void)sharedScarcity;

        // ---- 第 2 遍（O(B)）：按 absorbRadius 累计到所属核的稳定 id 上 ----
        // 只有"距离其归属核 <= absorbRadius"的球才可能被吸收，与核心判据一致。
        std::unordered_map<int, std::array<int, 3>> byId;
        byId.reserve(nCount + 8);
        for (std::size_t i = 0; i < bCount; ++i) {
            const Ball& b = api.world.balls()[i];
            if (b.ownerId < 0 || static_cast<std::size_t>(b.ownerId) >= nCount) continue;
            const Nucleus& owner = api.world.nuclei()[static_cast<std::size_t>(b.ownerId)];
            if (!owner.alive) continue;
            const double dx = b.pos.x - owner.pos.x;
            const double dy = b.pos.y - owner.pos.y;
            if (dx * dx + dy * dy > r2) continue;
            const int t = static_cast<int>(b.type);
            if (t < 0 || t >= 3) continue;
            byId[owner.id][static_cast<std::size_t>(t)] +=
                ownedTypeByIndex[i][static_cast<std::size_t>(t)];
        }

        // ---- 第 3 遍（O(N)）：差分出本帧吸收量并发放补贴 ----
        // 非 const 重载：World.h:104-106 说明该重载就是为 energy.apply 提供的。
        for (Nucleus& n : api.world.nuclei()) {
            if (!n.alive) continue;

            std::array<int, 3> curr{{0, 0, 0}};
            auto it = byId.find(n.id);
            if (it != byId.end()) curr = it->second;

            auto sit = state.snapshots.find(n.id);
            if (sit != state.snapshots.end()) {
                double frameSubsidy = 0.0;
                for (int t = 0; t < 3; ++t) {
                    const std::size_t ti = static_cast<std::size_t>(t);
                    const int absorbed = sit->second.ownedByType[ti] - curr[ti];
                    if (absorbed <= 0) continue;
                    // 稀有判定：用核自身遗传偏好，天然跟随基因分化，不硬编码球型。
                    if (n.params.absorbPreference[ti] <= kRareThreshold) continue;
                    frameSubsidy += static_cast<double>(absorbed) *
                                    kSubsidyPerBall * scarcityByType[ti];
                    if (frameSubsidy >= kMaxSubsidyPerFrame) {
                        frameSubsidy = kMaxSubsidyPerFrame;
                        break;
                    }
                }
                if (frameSubsidy > 0.0) {
                    api.energy.apply(n, frameSubsidy, EnergyReason::SUBSIDY);
                }
                sit->second.ownedByType = curr;
                sit->second.lastSeenFrame = e.frame;
            } else {
                if (state.snapshots.size() >= kMaxTracked) state.snapshots.clear();  // 硬上限
                NucleusSnapshot ns;
                ns.ownedByType = curr;
                ns.lastSeenFrame = e.frame;
                state.snapshots.emplace(n.id, ns);  // 首帧只建基线，不发放
            }
        }

        // 定期清理死核 id（防无界增长）。
        if (e.frame > 0 && (e.frame % kPruneInterval) == 0) {
            for (auto it = state.snapshots.begin(); it != state.snapshots.end();) {
                if (e.frame - it->second.lastSeenFrame > kPruneInterval) {
                    it = state.snapshots.erase(it);
                } else {
                    ++it;
                }
            }
        }
    });
}

// ============================================================
// mod id:        niche_guild
// 名称:          生态位行会
// 机制:          3行会(盾/斥/资)按基因评分分派，同行会邻居互惠
//                半径600，盈余2%流转，单核最多3伙伴，严格零和
//                全局每帧预算：alive * 10.0，耗尽即停止，防抖动失控
// 平衡作用:      修正单一捕食者策略占优，奖励生态位分化
// touches:       GUILD
// incompatible:  
// 依赖:          无
// ============================================================

#include "mod_api.h"
#include "World.h"
#include "Nucleus.h"
#include "EventBus.h"
#include "EnergySystem.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace {

// 调优常量（按需求默认值）
constexpr double kGuildRadius = 600.0;   // E[邻居数]=π·600²/4e6·20≈5.65
constexpr double kSurplusRate = 0.02;
constexpr double kSurplusFloor = 0.6;  // 能量 > 0.6×阈值 即视为有盈余（见下方实测标定）
constexpr int kMaxPartners = 3;
constexpr double kCellSize = 600.0;  // >= kGuildRadius，保证只需检查 3x3 邻域
// 全局每帧转移预算：每个活核允许的最大转移总量（防能量抖动失控）
// 取 12.0：alive=1000 时 budget≈1138，远低于观测到的 1.69M 抖动量级
constexpr double kGlobalBallsPerSqrt = 12.0; // 全局每帧预算：sqrt(alive) × 本值 × ballLossCost

// 三行会：固定顺序用于确定性破 tie
enum class Guild : uint8_t { Shield = 0, Scout = 1, Resource = 2, Count = 3 };

// 归一化到 [0,1]：用 ParamRanges 的 clamp 区间（config().ranges 可读，只读合规）。
// 【量纲修正】三个评分原本跨 2 个数量级（repelStrength≈1.75 / attackRange≈110 /
// absorbPreference≈1.0），attackRange 恒定最大 → 每个核都进 Scout 行会，行会分派退化。
// 先 min-max 归一化再比较，三个行会才真正可比。
inline double normalize(double v, double lo, double hi) {
    if (hi <= lo) return 0.0;
    const double t = (v - lo) / (hi - lo);
    return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
}

// 根据归一化基因评分分配行会（argmax + 固定顺序破 tie）
inline Guild assignGuild(const NucleusParams& p, const ParamRanges& r) {
    const double shieldScore =
        (normalize(p.repelStrength[0], r.repelStrengthClampMin, r.repelStrengthClampMax) +
         normalize(p.repelStrength[1], r.repelStrengthClampMin, r.repelStrengthClampMax) +
         normalize(p.repelStrength[2], r.repelStrengthClampMin, r.repelStrengthClampMax)) / 3.0;
    const double scoutScore = normalize(p.attackRange, r.attackRangeClampMin, r.attackRangeClampMax);
    const double resourceScore =
        (normalize(p.absorbPreference[0], r.absorbPreferenceClampMin, r.absorbPreferenceClampMax) +
         normalize(p.absorbPreference[1], r.absorbPreferenceClampMin, r.absorbPreferenceClampMax) +
         normalize(p.absorbPreference[2], r.absorbPreferenceClampMin, r.absorbPreferenceClampMax)) / 3.0;

    double bestScore = shieldScore;
    Guild bestGuild = Guild::Shield;

    if (scoutScore > bestScore) {
        bestScore = scoutScore;
        bestGuild = Guild::Scout;
    }
    if (resourceScore > bestScore) {
        bestScore = resourceScore;
        bestGuild = Guild::Resource;
    }
    // 相等时保持先出现的（固定顺序：Shield -> Scout -> Resource）
    return bestGuild;
}

// 粗网格：用于 O(N) 邻域查找
struct CoarseGrid {
    int cols = 0, rows = 0;
    double cellSize = kCellSize;
    double invCellSize = 1.0 / kCellSize;
    std::vector<std::vector<int>> cells;  // 存 nucleus 数组索引（仅本帧有效）

    void init(int c, int r) {
        cols = c; rows = r;
        cells.assign(cols * rows, {});
    }
    inline int idx(int cx, int cy) const { return cy * cols + cx; }
    inline void add(int cx, int cy, int nucleusIdx) {
        if (cx >= 0 && cx < cols && cy >= 0 && cy < rows) {
            cells[idx(cx, cy)].push_back(nucleusIdx);
        }
    }
    inline const std::vector<int>& get(int cx, int cy) const {
        static const std::vector<int> empty;
        if (cx >= 0 && cx < cols && cy >= 0 && cy < rows) return cells[idx(cx, cy)];
        return empty;
    }
    void clear() { for (auto& v : cells) v.clear(); }
};

} // anonymous namespace

void registerNicheGuild(ModAPI& api) {
    // 每帧处理：FRAME_START 后、FRAME_END 前都可。我们用 FRAME_START。
    api.events.subscribe(EventType::FRAME_START, [&](const SimEvent&) {
        const auto& cfg = api.world.config();
        const ParamRanges& ranges = cfg.ranges;
        // 非 const 重载：World.h:104-106 说明该重载就是为 energy.apply 提供的。
        auto& nuclei = api.world.nuclei();

        // 1) 为每个活核计算行会
        // 由于 nuclei 数组每帧 swap-pop，本帧的索引只能本帧用
        // 我们先建一个临时数组：index -> guild
        std::vector<Guild> guildOf(nuclei.size(), Guild::Shield);
        for (size_t i = 0; i < nuclei.size(); ++i) {
            if (nuclei[i].alive) {
                guildOf[i] = assignGuild(nuclei[i].params, ranges);
            }
        }

        // 2) 构建粗网格
        CoarseGrid grid;
        grid.init(
            static_cast<int>(std::ceil(cfg.width * grid.invCellSize)),
            static_cast<int>(std::ceil(cfg.height * grid.invCellSize))
        );
        for (size_t i = 0; i < nuclei.size(); ++i) {
            if (!nuclei[i].alive) continue;
            const int cx = static_cast<int>(nuclei[i].pos.x * grid.invCellSize);
            const int cy = static_cast<int>(nuclei[i].pos.y * grid.invCellSize);
            grid.add(cx, cy, static_cast<int>(i));
        }

        // 3) 同行会互惠：严格零和
        // 每个 giver 最多给 kMaxPartners 个 receiver
        // 每个 receiver 最多收 kMaxPartners 个 giver
        // x = giver_surplus * kSurplusRate（giver_surplus = energy - energyThreshold）
        // apply(giver, -x, GUILD); apply(receiver, +x, GUILD) —— 同一个 x，保证零和
        const double rSq = kGuildRadius * kGuildRadius;

        // 【全局每帧转移预算】budget = sqrt(alive) × kGlobalBallsPerSqrt × ballLossCost
        //
        // 【实测标定 by deputy — 首版线性预算无效，必须改成子线性】
        // 首版取 budget = alive × 10.0，实测 `relief+guild` 仍然爆炸到
        // maxNuclei=1000（survivors_mean=334）。根因：**线性于 alive 的预算正是
        // 爆炸的正反馈项本身**——核越多→可搬运量越多→核越有精力繁殖→核更多。
        // 注意这与零和性无关：单笔转移确实 in==out，但"把能量持续搬向繁殖闸门
        // 下方"是一个**种群放大器**，线性预算让它无刹车。
        //
        // 修正为 sqrt 缩放，并以「球当量」定标（乘 ballLossCost），使预算与配置解耦：
        //   alive=1    -> 6 球当量（单核索要上限 3 伙伴 × 0.02 盈余，够用）
        //   alive=20   -> 26.8 球当量
        //   alive=1000 -> 189 球当量（约为潜在的 1/30，正反馈被截断）
        // 低 N 段（1~3 核，已验证真正起作用的地方）几乎不受限，高 N 段被压平。
        int alive = 0;
        for (size_t i = 0; i < nuclei.size(); ++i) if (nuclei[i].alive) ++alive;
        const double lossCost = cfg.ballLossCost;
        const double budget = (alive == 0 || lossCost <= 0.0)
                                  ? 0.0
                                  : std::sqrt(static_cast<double>(alive)) *
                                        kGlobalBallsPerSqrt * lossCost;
        double moved = 0.0;

        // 记录每个核本帧已收到的伙伴数，用于上限
        // （giveCount 原本存在但只写不读，已删除；recvCount 的上限已约束总转移次数）
        std::vector<int> recvCount(nuclei.size(), 0);

        for (size_t i = 0; i < nuclei.size(); ++i) {
            const Nucleus& giver = nuclei[i];
            if (!giver.alive) continue;
            const Guild gGuild = guildOf[i];

            // 【实测标定 by deputy】基线平均核能量仅 34~78，而 energyThreshold
            // init 范围 80~200（均值≈140）——核几乎从不停留在阈值之上（达到阈值
            // 就立刻繁殖并被扣掉 0.6×阈值），所以"surplus = energy - threshold"
            // 恒为负 → GUILD 永不触发。改为"高于阈值的 60% 即可参与互惠"。
            const double giverSurplus =
                giver.energy - giver.params.energyThreshold * kSurplusFloor;
            if (giverSurplus <= 0.0) continue;  // 无盈余则不给

            const int gcx = static_cast<int>(giver.pos.x * grid.invCellSize);
            const int gcy = static_cast<int>(giver.pos.y * grid.invCellSize);

            int partnersFound = 0;
            for (int dy = -1; dy <= 1 && partnersFound < kMaxPartners; ++dy) {
                for (int dx = -1; dx <= 1 && partnersFound < kMaxPartners; ++dx) {
                    const auto& cell = grid.get(gcx + dx, gcy + dy);
                    for (int j : cell) {
                        if (partnersFound >= kMaxPartners) break;
                        if (j == static_cast<int>(i)) continue;
                        if (!nuclei[j].alive) continue;
                        if (guildOf[j] != gGuild) continue;
                        if (recvCount[j] >= kMaxPartners) continue;  // receiver 已满

                        const Vec2 diff = nuclei[j].pos - giver.pos;
                        if (diff.lengthSq() > rSq) continue;

                        // 计算转移量（仅用 giver 的盈余，不读 receiver 的能量）
                        const double x = giverSurplus * kSurplusRate;
                        if (x <= 0.0) continue;

                        // 全局预算检查：若剩余预算不足，本帧停止转移
                        if (moved + x > budget) {
                            // 预算耗尽，跳出所有循环
                            goto guild_transfer_done;
                        }

                        // 严格零和：同一个 x 正负各一次
                        api.energy.apply(nuclei[i], -x, EnergyReason::GUILD);
                        api.energy.apply(nuclei[j], +x, EnergyReason::GUILD);
                        moved += x;

                        ++recvCount[j];
                        ++partnersFound;
                    }
                }
            }
        }
        guild_transfer_done: ;
    });
}

// 可选：跨帧状态持久化（当前无跨帧状态，留空并注释）
// void saveNicheGuild(std::ostream&) {}
// void loadNicheGuild(std::istream&) {}
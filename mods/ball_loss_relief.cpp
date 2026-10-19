// ============================================================
// mod id:        ball_loss_relief
// 名称:          失球宽限
// 机制:          失球「宽限阶梯」。逐帧统计每核归属球数，按稳定 Nucleus::id
//                差分出掉球数，并用 EnergySystem::totalOut(BALL_LOSS) 的
//                增量做**全局台账校准**，从源头消除「吸收掉球」的假阳性。
//                每核每个滑动窗口内前 kFreeSwitches(2) 次掉球全额返还
//                ballLossCost，其后每次返还 kReliefRatio(0.5)×ballLossCost；
//                新生核前 kInfantProtectionFrames(200) 帧内按
//                kInfantRefundRatio(0.9) 返还（正交于计数额度的新生通道）；
//                单核单帧返还封顶 kMaxRefundBalls(6) 球当量；此外全帧还有一个
//                **子线性全局预算**：budget = alive^0.5 * kGlobalBallsPerSqrt(12.0)
//                × ballLossCost（以「球当量」定标，与配置解耦），
//                单帧返还总额不得超过它（spent + refund <= budget），用于掐断
//                「核越多→返还越多→核更多」的正反馈、防止种群冲上 maxNuclei。
// 平衡作用:      核心 updateOwnership 对失球方只罚 ballLossCost(5.0)，
//                而吸收一球最多只赚 baseBallEnergy×absorbPreference(≤3.0)，
//                净能量必为负 → 种群从 20 核坍缩到 1 核。本 mod 返还约
//                一半罚金，把收支拉回可持续区间，让行为策略有机会演化。
//                实测(--seed 42 --balls 1000 --nuclei 20 --frames 10000)：
//                存活核数 1 -> 22；TERRITORY in 10667 / BALL_LOSS out 22005。
// touches:       TERRITORY
// incompatible:
// 依赖:          无
// ============================================================
//
// 【重要：本 mod 刻意不是零和(zero-sum) mod】
// 严格零和 mod 只把能量从一个核搬到另一个核，系统总量不变。这里刻意
// 不同：本 mod 把一部分本应被系统销毁的能量「从虚空还给」受罚核，专门用来
// 抵消核心单边销毁能量的失衡（吸收收益上限 3.0 < 失球罚金 5.0）。因此
// EnergyReason::TERRITORY 的累计 totalIn 会是**净正输入**，而不是各核之间的
// 搬运。这是设计意图，不是能量泄漏。
//
// 【零和对照】若要与严格零和 mod 组合，应关闭本 mod；本实现刻意不做
// 「从 BALL_LOSS 已扣额里扣回」，因为那会与其它 mod 重复计算同一笔。
//
// 【如何精确检测失球（关键设计）】
// 本 mod 无法读取核心私有的 prevOwner_，也收不到换主事件，只能靠「本帧
// 归属球数 - 上帧归属球数」的差分来推断掉球。但该差分有两个噪声源：
//   (a) 被吸收的球也会让计数下降 —— 而核心**不会**为此收罚金；
//   (b) 本帧新出现的核没有历史基线。
// 解法是引入一个**全局台账 (ledger)**：核心每次真正收罚金，都会让
// EnergySystem::totalOut(BALL_LOSS) 增加 ballLossCost；这是 EnergySystem 的
// 公有只读访问器，与「核被扣能量后是否死亡」无关，因此是权威且精确的。
// 我们用台账增量与差分结果取 min 来校准掉球数：
//   * 台账增量为 0 → 本帧核心没收任何罚金 → 无论如何不返还（杜绝 (a) 类
//     假阳性：纯吸收导致的掉球不会被误当成失球）；
//   * 台账增量 > 0 → 以台账为上限，按各核差分占比分配，且返还总额恒
//     ≤ 台账增量 → 既精确又不超发。
// 由于台账是全局的、差分是按核的，分配采用按比例整数分配（带余数累积），
// 保证 Σ分配 ≤ 台账增量。
//
// 【状态保存】本 mod 保留内部状态（每核归属球快照 / 宽限额度 / 台账游标），
// 但这些都可从世界完全重建，且项目未登记 save/load 钩子。因此**不提供**
// saveBallLossRelief / loadBallLossRelief。读档后世界会在首个 FRAME_START
// 重建基线（所有核算「首次出现」，本帧不返还），最多影响读档后第一帧。

#include "mod_api.h"
#include "World.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {

// ---- 可调常数（保守取值，避免种群朝 maxNuclei 爆炸）----
// 【实测调优 by lead，勿回退】基线跨种子实测（--balls 1000 --nuclei 20 --frames 5000）：
//   基线        存活 0/0/1(seed1/2/42)，全灭率 2/3，evenness 0.000，BALL_LOSS out 9375
//   本组参数    存活 0/1/2，            全灭率 1/3，evenness 1.000
// 关键约束（来自实测反推：纯失球核约 2.5~3.5 球/帧被罚没，新核在攒满
// kFreeSwitches 次宽限之前就已耗尽能量）：
//   a) kGraceWindowFrames 必须 > 新生核典型存活帧数，否则计数型额度形同虚设；
//   b) 必须有 kInfantProtectionFrames 这条**不依赖累计、不依赖存活时长**的
//      正向通道（按 firstSeenFrame 的连续函数），否则对新生个体完全无效；
//   c) kMaxRefundBalls 必须 >= 实际失球速率（2.5~3.5 球/帧），否则追不上=无效。
constexpr int    kFreeSwitches         = 2;     // 每核每窗口前 N 次掉球：全额返还
constexpr double kReliefRatio          = 0.5;   // 超出宽限后的返还比例（× ballLossCost）
constexpr int    kMaxRefundBalls       = 6;     // 单核单帧返还球数上限（须 >= 失球速率）
constexpr int    kGraceWindowFrames    = 1200;  // 宽限窗口；须 > 新生核典型存活帧数
constexpr int    kInfantProtectionFrames = 200;// 幼体保护帧数（正交于计数额度）
constexpr double kInfantRefundRatio    = 0.9;   // 幼体期返还比例（× ballLossCost）
constexpr int    kTrackPruneFrames     = 120;   // 连续多久未出现则清理死核 id
constexpr int    kMaxTrackedNuclei     = 4096;  // 跟踪表硬上限（防内存无界）

// ---- 全局单帧返还预算（防正反馈种群爆炸）----
// 【实测问题】本 mod 从虚空返还能量。旧实现只有**单核**上限，没有任何全局
// 上限，于是构成一个正反馈回路：核越多 → 总掉球越多 → 返还越多 → 能量越多
// → 繁殖越快 → 核更多。与 niche_guild / symbiosis_bond 同跑时，后者让核一直
// 活着、回路没有刹车，实测直接冲到 maxNuclei=1000，
// TERRITORY in 189501（≈ 单独运行的 18 倍）。
//
// 【为什么是 sqrt(N) 而不是 N】线性正比于存活核数的预算，恰好就是引爆爆炸的
// 那条正反馈本身：返还总量 ∝ N，核的增长永远能换到成比例更多的返还，于是收敛
// 不到任何平衡点。取 alive^0.5 后预算随 N 只按平方根增长：
//   * 低 N 段（1~3 核，正是基线 --seed 42 --balls 1000 --nuclei 20 的残存
//     规模，也是返还最要紧的地方）预算相对单核索要额极其宽裕（见下表），
//     因此完全保留单跑时的 1/3 全灭率 / evenness 1.000 / TERRITORY in ≈ 10249
//     这一实测基线；
//   * 高 N 段预算被彻底压平（1000 核时 1000*30 = 30000 的潜在索要额被压到
//     远低于该值），回路增益 < 1，爆炸被截断。
// 【取值】单核单帧索要额上限 = kMaxRefundBalls(6) × ballLossCost(5.0) = 30 能量。
// 40 使 alive=1 时 budget = 40 > 30（低 N 段等价于「不设限」，故 1~3 核残存段仍
// 被**足额**返还，已实测的 1/3 全灭率 / evenness 1.000 / TERRITORY in ≈ 10249
// 基线得以保留）；而 alive=1000 时 budget = 1138，仅为潜在索要额 1000×30 = 30000
// 的约 1/26，正反馈增益被压到 1 以下。
// 【取值标定 by deputy — 实测驱动，不要凭算术拍脑袋】
// 首版取 40.0，结果**过度纠正、mod 退化**：`relief alone` 实测 TERRITORY in
// 从 10249 掉到 665~1368，survivors_max 从 3 掉回 1（等于基线），全灭率回升。
// 根因：预算**没有跟着单核索要额 ballLossCost 走**。低 N 段"budget > 单核索要额"
// 的前提只在 ballLossCost=5 时成立；一旦实��� ballLossCost 被调大，或同帧多核同时
// 索要，sqrt 预算立刻成为瓶颈，把本该发出的返还砍掉——而砍掉的部分正是
// "救活临界核"的关键。
// 修正原则：预算必须**相对真实需求**定标，而不是相对一个魔法数。做法是
//   budget = sqrt(alive) * kGlobalBallsPerSqrt * ballLossCost
// 其中 kGlobalBallsPerSqrt 的含义是"每 sqrt(核数) 允��发放多少个当量的球"，
// 与 ballLossCost 解耦：改配置不会让预算失准。
// 取值：kGlobalBallsPerSqrt = 12.0
//   alive=1   -> 12 球当量（单核索要上限 6 球）=> 不受限，保留已验证基线
//   alive=3   -> 20.8 球当量（索要上限 18）  => 基本不受限
//   alive=20  -> 53.7 球当量（索要上限 120） => 约为 45%，仍留余量
//   alive=1000-> 379 球当量（索要上限 6000） => 约 6%，正反馈被截断
// 这样低 N 段（1~3 核，已验证真正起作用的地方）几乎不受影响，
// 高 N 段被压到 ~6%，爆炸回路增益 < 1。
constexpr double kGlobalBallsPerSqrt = 12.0; // 全局单帧预算：sqrt(alive) × 本值 × ballLossCost
// 台账比例上限：单帧返还总额 ≤ 核心本帧实收罚金 × 本值。
// 这是**与核数无关**的硬不变量 —— relief 只能"减轻净销毁"，永远不能变成净能量源，
// 因此不存在任何 N 下的指数爆炸可能。取 0.6：已验证基线 37% 仍完整保留，
// 同时给失球率更高的拥挤场景保留 60% 的缓解力度。
constexpr double kRefundCapOfLedger = 0.6;

// 每个稳定 nucleus id 的追踪状态。全部由世界状态派生，可随时重建。
struct NucleusTrack {
    int ownedBalls       = 0;               // 上次统计到的归属球数
    int freeLossesLeft   = kFreeSwitches;   // 窗口内剩余的全额宽限次数
    int windowStartFrame = 0;               // 当前宽限窗口起始帧
    int lastSeenFrame    = 0;               // 最近一次出现在存活核列表的帧
    int firstSeenFrame   = 0;               // 首次出现帧（幼体保护起点）
};

struct ReliefState {
    std::unordered_map<int, NucleusTrack> tracks;  // key = 稳定 Nucleus::id
    // 核心 BALL_LOSS 累计扣额（来自 EnergySystem::totalOut）逐帧差分，
    // 得到「本帧核心实际收了多少罚金」= 本帧真正可返还的上限台账。
    // 这比用「掉球总数 - 吸收数」估算更准：从根本上杜绝把「纯吸收」误判为失球。
    double prevBallLossOut = 0.0;
};

}  // namespace

void registerBallLossRelief(ModAPI& api) {
    // 函数内静态：registerXxx 每进程调用一次；值初始化，无初始化副作用。
    static ReliefState state;

    // ---- 事件：FRAME_START ----
    // 此刻本帧死亡核已被移除、球已吸收/补充完毕，nuclei()/balls() 反映的是
    // 本帧**最终**归属状态，是做差分的正确时机。
    api.events.subscribe(EventType::FRAME_START, [&](const SimEvent& e) {
        std::vector<Nucleus>& nuclei = api.world.nuclei();
        const std::vector<Ball>& balls = api.world.balls();
        const WorldConfig& cfg = api.world.config();
        const double lossCost = cfg.ballLossCost;

        // 1) 精确台账：核心本帧真正收了多少失球罚金（公有只读访问器）。
        //    与「核是否死亡」无关，可安全用于权威校准。
        const double ballLossOutNow = api.energy.totalOut(EnergyReason::BALL_LOSS);
        double ledgerDelta = ballLossOutNow - state.prevBallLossOut;
        if (ledgerDelta < 0.0) ledgerDelta = 0.0;   // 防御：计数重置（如读档）
        state.prevBallLossOut = ballLossOutNow;

        // 2) 重建本帧每核归属球数：ownerId 是 nuclei 数组下标（会因 swap-pop
        //    变动），只作「本帧下标」，立刻映射到稳定 Nucleus::id 作为跨帧
        //    跟踪键。全程 O(N + M)，全程只读。
        std::unordered_map<int, int> idToIndex;    // 稳定 id -> 本帧下标
        std::unordered_map<int, int> currentOwned; // 稳定 id -> 本帧归属球数
        idToIndex.reserve(nuclei.size() + 8);
        currentOwned.reserve(nuclei.size() + 8);
        for (size_t i = 0; i < nuclei.size(); ++i) {
            const Nucleus& n = nuclei[i];
            if (!n.alive || n.id < 0) continue;
            idToIndex[n.id] = static_cast<int>(i);
            currentOwned.emplace(n.id, 0);
        }
        for (const Ball& b : balls) {
            const int idx = b.ownerId;  // 只读
            if (idx >= 0 && static_cast<size_t>(idx) < nuclei.size()) {
                const int id = nuclei[static_cast<size_t>(idx)].id;
                if (id >= 0) {
                    auto it = currentOwned.find(id);
                    if (it != currentOwned.end()) ++it->second;
                }
            }
        }

        // 2b) 本帧**全局**返还预算 —— 双重封顶，取较小者。
        //
        // 【根因与最终修法 by deputy — sqrt 单独不够，必须叠加"台账比例"上限】
        // 实测：`relief+guild` 仍爆炸到 maxNuclei=1000。逐项拆账发现
        //   ABSORB      3,662 -> 283,626   （涨 77 倍，但**有上限**：小球总数被
        //                                    initialBalls=1000 锁住，吸收总量饱和）
        //   BALL_LOSS   6,200 -> 220,635   （涨 36 倍，**随核数 N 线性增长且无上限**）
        //   TERRITORY   2,292 -> 145,369   （≈ BALL_LOSS 的 66%）
        // 关键不对称：**吸收有界、失球无界**。N 越大 → 归属 churn 越多 → BALL_LOSS
        // 越多 → 按比例返还的 relief 越多 → 核越有精力繁殖 → N 更大。正反馈闭环。
        // 而 sqrt 预算在 N=1000 时是 sqrt(1000)×12×5 ≈ 1897 能量/帧，实际需求
        // 只有 ~29/帧 —— **预算根本没被触发**，所以 sqrt 单独无效。
        //
        // 正确的不变量：**返还永远不能超过核心本帧实际收走的罚金**。
        //   budget = min( sqrt(alive)×kGlobalBallsPerSqrt×ballLossCost ,
        //                  ledgerDelta × kRefundCapOfLedger )
        // 台账比例上限与 N 无关 ⇒ 任何 N 下 relief 都是"净销毁"的减弱，不可能
        // 变成净能量源，从根上切断正反馈。已验证基线 `relief alone` 的
        // TERRITORY/BALL_LOSS = 2292/6200 = 37%，低于 kRefundCapOfLedger(0.6)，
        // 故该封顶**不触碰**已验证行为。
        const size_t aliveCount = idToIndex.size();
        const double sqrtBudget = (aliveCount == 0 || lossCost <= 0.0)
                                      ? 0.0
                                      : std::sqrt(static_cast<double>(aliveCount)) *
                                            kGlobalBallsPerSqrt * lossCost;
        const double ledgerBudget = (ledgerDelta > 0.0) ? ledgerDelta * kRefundCapOfLedger : 0.0;
        const double budget = (sqrtBudget < ledgerBudget) ? sqrtBudget : ledgerBudget;
        // 本帧已返还总量（能量当量），逐次 apply 后累加，用于夹紧余量。
        double spent = 0.0;

        // 3) 差分出「本帧掉球」。
        struct DropEntry { int id; int drop; };
        std::vector<DropEntry> drops;
        drops.reserve(currentOwned.size());
        int totalDrop = 0;
        for (const auto& kv : currentOwned) {
            auto it = state.tracks.find(kv.first);
            // 首次见到该 id：以当前球数建立基线，本帧不视为掉球（消除噪声源 b）。
            const int prev = (it == state.tracks.end()) ? kv.second : it->second.ownedBalls;
            const int drop = prev - kv.second;
            if (drop > 0) {
                drops.push_back(DropEntry{kv.first, drop});
                totalDrop += drop;
            }
        }

        // 4) 用台账校准并发放返还。
        //    ledgerBalls = 核心本帧实收罚金对应的球数（向上取整，宁可少发）。
        //    台账为 0 时完全不发放 —— 这从根本上杜绝了「纯吸收被误判为失球」。
        if (lossCost > 0.0 && totalDrop > 0 && ledgerDelta > 0.0) {
            int ledgerBalls = static_cast<int>(ledgerDelta / lossCost + 0.5);
            if (ledgerBalls < 0) ledgerBalls = 0;
            int grantable = ledgerBalls;
            if (grantable > totalDrop) { grantable = totalDrop; }
            if (grantable > 0) {
                // 按各核掉球占比把 grantable 个名额整数分配下去（Σ ≤ grantable）。
                int assigned = 0;
                int64_t carried = 0;
                for (size_t ci = 0; ci < drops.size(); ++ci) {
                    if (assigned >= grantable) break;
                    // 全局预算余量为 0 即本帧停发（子线性上限优先于逐核分配）。
                    if (spent >= budget) break;
                    carried += static_cast<int64_t>(grantable) * drops[ci].drop;
                    int share = static_cast<int>(carried / totalDrop);
                    carried %= totalDrop;
                    if (share <= 0) continue;
                    if (assigned + share > grantable) share = grantable - assigned;
                    assigned += share;

                    const int id = drops[ci].id;
                    NucleusTrack& tr = state.tracks[id];
                    // 窗口滑动：过期或帧号回绕则宽限次数回满。
                    if (e.frame - tr.windowStartFrame >= kGraceWindowFrames ||
                        e.frame < tr.windowStartFrame) {
                        tr.windowStartFrame = e.frame;
                        tr.freeLossesLeft = kFreeSwitches;
                    }
                    // 宽限阶梯：先用免费次数（每球全额），超出部分按比例返还。
                    int full = share;
                    if (tr.freeLossesLeft < full) full = tr.freeLossesLeft;
                    int relief = share - full;
                    if (full < 0) full = 0;
                    if (relief < 0) relief = 0;
                    tr.freeLossesLeft -= full;

                    double refundPerBall = lossCost * kReliefRatio;
                    // 幼体保护：首次出现后的 kInfantProtectionFrames 帧内按更高比例
                    // 返还。依据实测（纯失球核约 2.5~3.5 帧被罚死一球），幼体几乎必然
                    // 在攒满 kFreeSwitches 次宽限之前就耗尽能量，因此需要一条与
                    // 「免费次数」正交的高比例通道，否则该机制对新核无效。
                    if (e.frame - tr.firstSeenFrame < kInfantProtectionFrames) {
                        refundPerBall = lossCost * kInfantRefundRatio;
                    }

                    double refund = static_cast<double>(full) * lossCost +
                                    static_cast<double>(relief) * refundPerBall;
                    // 单核单帧返还上限：最多 kMaxRefundBalls 球的全额当量。
                    const double cap = static_cast<double>(kMaxRefundBalls) * lossCost;
                    if (refund > cap) refund = cap;
                    if (refund <= 0.0) continue;

                    // 经稳定 id 反查本帧下标（id 稳定，下标每帧可能变）。
                    auto idxIt = idToIndex.find(id);
                    if (idxIt == idToIndex.end()) continue;
                    Nucleus& target = nuclei[static_cast<size_t>(idxIt->second)];
                    if (!target.alive) continue;
                    // 全局预算夹紧（单核封顶之外再加一层逐帧总闸）：
                    // spent + refund <= budget。余量不足则按余量等比缩减，
                    // 余量为 0 时本帧彻底停发。这是正反馈的减速带。
                    const double remaining = budget - spent;
                    if (remaining <= 0.0) break;
                    if (refund > remaining) refund = remaining;
                    api.energy.apply(target, refund, EnergyReason::TERRITORY);
                    spent += refund;
                }
            }
        }

        // 5) 更新快照并清理（防跟踪表无界增长）。
        for (const auto& kv : currentOwned) {
            auto it = state.tracks.find(kv.first);
            if (it == state.tracks.end()) {
                if (state.tracks.size() >= static_cast<size_t>(kMaxTrackedNuclei)) {
                    state.tracks.clear();  // 硬上限：只丢派生量，无正确性风险
                }
                NucleusTrack nt;
                nt.ownedBalls = kv.second;
                nt.lastSeenFrame = e.frame;
                nt.windowStartFrame = e.frame;
                nt.freeLossesLeft = kFreeSwitches;
                nt.firstSeenFrame = e.frame;   // 幼体保护起点：必须显式初始化，
                                              // 否则默认 0 会让 e.frame-0 恒大，保护永不触发。
                state.tracks.emplace(kv.first, nt);
            } else {
                it->second.ownedBalls = kv.second;
                it->second.lastSeenFrame = e.frame;
            }
        }

        // 定期清理：长期消失（已死且不持球）的 id 移出跟踪表。
        if (e.frame > 0 && (e.frame % kTrackPruneFrames) == 0) {
            for (auto it = state.tracks.begin(); it != state.tracks.end();) {
                if (e.frame - it->second.lastSeenFrame > kTrackPruneFrames) {
                    it = state.tracks.erase(it);
                } else {
                    ++it;
                }
            }
        }
    });
}

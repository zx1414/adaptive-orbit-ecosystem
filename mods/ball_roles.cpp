// ============================================================
// mod id:        ball_roles
// 名称:          小球角色分工
// 机制:          计划1 经济层——让「养球」与「吃球」成为两种真实策略。
//                1) 资源球（Worker）持续产出：每帧按持有资源球数为主人产出
//                   能量，产出 = kWorkerYield × W × kWorkerHalfSat/(kWorkerHalfSat+W)
//                   （半饱和曲线：收益递减，单核产出硬上界 = kWorkerYield×kWorkerHalfSat）；
//                2) 护盾球（Shield）失球抵挡：本帧真正被核心收走罚金的失球，
//                   按各核「min(掉球数, 护盾数)」占比分摊返还名额，每个被抵挡
//                   的失球返还 ballLossCost × kShieldRefundRatio(0.6)。
//                   失球检测复用 ball_loss_relief 已验证的**台账法**：
//                   EnergySystem::totalOut(BALL_LOSS) 逐帧差分为权威上限，
//                   杜绝「吸收掉球」假阳性（纯吸收不返还）。
//                双重封顶防种群爆炸（同 relief 先例）：
//                   budget = min( sqrt(alive)×kGlobalBallsPerSqrt×ballLossCost ,
//                                 ledgerDelta×kRefundCapOfLedger )
// 平衡作用:      现状三类球只有速度差异，吃球是唯一策略。本 mod 给两种球
//                独立的持有收益：Worker 变成可放牧的「产能资产」，Shield 变成
//                「失球保险」——核必须权衡吃掉（一次性）与养着（持续性）。
// touches:       BALL_ROLES_WORKER,BALL_ROLES_SHIELD
// incompatible:  ball_loss_relief
// 依赖:          无
// ============================================================
//
// 【与 ball_loss_relief 的冲突（重要）】两者都从 BALL_LOSS 罚金流里返还能量：
// relief 返还 ≤60%，本 mod 再返还 ≤60%，叠加可突破 100% → 净销毁变净能量源。
// 启动器会按 incompatible 给出提示；请勿同时启用。
//
// 【本 mod 刻意不是零和 mod】与 relief 的 TERRITORY 同理：
//  * BALL_ROLES_WORKER 是「从虚空产出」（与蓝细菌光合同类）；
//  * BALL_ROLES_SHIELD 是「把本应被销毁的罚金还回来」，且受台账比例硬封顶
//    （≤ 本帧实收罚金 × 0.6），任何核数下都只是「净销毁的减弱」，不可能
//    变成净能量源，不存在随 N 的正反馈爆炸。
//
// 【资源球产出的反退化设计】半饱和曲线 W×kHalf/(kHalf+W) 保证：
//  * 收益递减：囤 200 个资源球不比囤 40 个多赚多少 → 抑制无脑囤球；
//  * 单核上界：产出 ≤ kWorkerYield×kWorkerHalfSat(0.04)/帧，与基础代谢
//    basalCost(0.01) 同量级——纯蹲守无法自给自足到失控，仍需觅食/战斗；
//  * 典型量级：W=10 时 ≈0.013/帧，与基线吸收收入（全场 0.32/帧 ÷ 核数）同量级。
//
// 【性能】每帧 O(N + B)：一趟归属统计 + 两趟核级分配，无 O(N×B)，零随机消耗
// （不触碰 api.rng，同 seed 逐字节可复现）。
//
// 【状态保存】内部状态（每核归属球快照 / 台账游标）全部可从世界重建，且未登记
// save/load 钩子（同 relief 先例）：读档后首个 FRAME_START 重建基线（所有核
// 算「首次出现」，本帧不返还），最多影响读档后第一帧。

#include "mod_api.h"
#include "World.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {

// ---- 资源球（Worker）持续产出 ----
// 【常数说明】解析估算起步（未实测标定）：0.002 为保守值——20 核典型持有量下
// 全场注入 ≈0.2~0.3/帧，不高于基线吸收收入 0.32/帧（MOD_SUITE.md 实测）。
// 方向验证后再上调找上界；若种群冲 maxNuclei，先降本值。
constexpr double kWorkerYield   = 0.002;  // 每资源球基础产出（能量/帧）
constexpr double kWorkerHalfSat = 20.0;   // 半饱和持有量（球）：越大则产出曲线越平缓、上界越高

// ---- 护盾球（Shield）失球抵挡 ----
constexpr double kShieldRefundRatio = 0.6;  // 每个被抵挡失球的返还比例（× ballLossCost）
constexpr int    kMaxCoverBalls     = 6;    // 单核单帧抵挡球数上限（实测失球速率 2.5~3.5/帧，须 >= 它）

// ---- 全局双封顶（复用 ball_loss_relief 已验证取值，勿单独改动）----
constexpr double kGlobalBallsPerSqrt = 12.0;  // sqrt(alive) × 本值 × ballLossCost
constexpr double kRefundCapOfLedger  = 0.6;   // 返还总额 ≤ 核心本帧实收罚金 × 本值

// ---- 跟踪表维护（同 relief 先例）----
constexpr int kTrackPruneFrames = 120;   // 连续多久未出现则清理死核 id
constexpr int kMaxTrackedNuclei = 4096;  // 跟踪表硬上限（防内存无界）

// 每个稳定 nucleus id 的追踪状态。全部由世界状态派生，可随时重建。
struct NucleusTrack {
    int ownedBalls    = 0;  // 上一帧归属球总数（失球差分基线）
    int lastSeenFrame = 0;  // 最近一次出现在存活核列表的帧
};

struct BallRolesState {
    std::unordered_map<int, NucleusTrack> tracks;  // key = 稳定 Nucleus::id
    // 核心 BALL_LOSS 累计扣额逐帧差分游标（台账）。
    double prevBallLossOut = 0.0;
};

}  // namespace

void registerBallRoles(ModAPI& api) {
    // 函数内静态：registerXxx 每进程调用一次；值初始化，无初始化副作用。
    static BallRolesState state;

    // ---- 事件：FRAME_START ----
    // 与 ball_loss_relief 同一时机：本帧死亡核已移除、球已吸收/补充完毕，
    // nuclei()/balls() 反映本帧最终归属状态，差分与统计都在正确时刻。
    api.events.subscribe(EventType::FRAME_START, [&](const SimEvent& e) {
        std::vector<Nucleus>& nuclei = api.world.nuclei();
        const std::vector<Ball>& balls = api.world.balls();
        const WorldConfig& cfg = api.world.config();
        const double lossCost = cfg.ballLossCost;

        // 1) 权威台账：核心本帧真正收了多少失球罚金（公有只读访问器）。
        const double ballLossOutNow = api.energy.totalOut(EnergyReason::BALL_LOSS);
        double ledgerDelta = ballLossOutNow - state.prevBallLossOut;
        if (ledgerDelta < 0.0) ledgerDelta = 0.0;  // 防御：计数重置（如读档）
        state.prevBallLossOut = ballLossOutNow;

        // 2) 稳定 id 索引 + 本帧归属统计（总数 / Worker / Shield），全程只读 O(N+B)。
        std::unordered_map<int, int> idToIndex;   // 稳定 id -> 本帧下标
        std::unordered_map<int, int> ownedTotal;  // 稳定 id -> 归属球总数
        std::unordered_map<int, int> ownedWorker; // 稳定 id -> 归属资源球数
        std::unordered_map<int, int> ownedShield; // 稳定 id -> 归属护盾球数
        idToIndex.reserve(nuclei.size() + 8);
        ownedTotal.reserve(nuclei.size() + 8);
        ownedWorker.reserve(nuclei.size() + 8);
        ownedShield.reserve(nuclei.size() + 8);
        for (size_t i = 0; i < nuclei.size(); ++i) {
            const Nucleus& n = nuclei[i];
            if (!n.alive || n.id < 0) continue;
            idToIndex[n.id] = static_cast<int>(i);
            ownedTotal.emplace(n.id, 0);
            ownedWorker.emplace(n.id, 0);
            ownedShield.emplace(n.id, 0);
        }
        for (const Ball& b : balls) {
            const int idx = b.ownerId;  // 只读：ownerId 是 nuclei 数组下标
            if (idx < 0 || static_cast<size_t>(idx) >= nuclei.size()) continue;
            const int id = nuclei[static_cast<size_t>(idx)].id;
            if (id < 0) continue;
            auto it = ownedTotal.find(id);
            if (it == ownedTotal.end()) continue;
            ++it->second;
            if (b.type == BallType::Worker) {
                ++ownedWorker.find(id)->second;
            } else if (b.type == BallType::Shield) {
                ++ownedShield.find(id)->second;
            }
        }

        // 3) 资源球持续产出：半饱和曲线，收益递减 + 单核硬上界。
        //    （kWorkerYield 调成 0 时由下方 yield<=0 跳过，不设编译期守卫以免 C4127。）
        for (const auto& kv : ownedWorker) {
            const int w = kv.second;
            if (w <= 0) continue;
            auto idxIt = idToIndex.find(kv.first);
            if (idxIt == idToIndex.end()) continue;
            Nucleus& target = nuclei[static_cast<size_t>(idxIt->second)];
            if (!target.alive) continue;
            const double sat = kWorkerHalfSat / (kWorkerHalfSat + static_cast<double>(w));
            const double yield =
                kWorkerYield * static_cast<double>(w) * sat;
            if (yield <= 0.0) continue;
            api.energy.apply(target, yield, EnergyReason::BALL_ROLES_WORKER);
        }

        // 4) 护盾球失球抵挡：差分掉球 → min(掉球, 护盾) 为抵挡名额 → 台账校准分配。
        if (lossCost > 0.0 && ledgerDelta > 0.0) {
            struct CoverEntry { int id; int covered; };
            std::vector<CoverEntry> covers;
            covers.reserve(ownedTotal.size());
            int totalCovered = 0;
            for (const auto& kv : ownedTotal) {
                const int id = kv.first;
                auto it = state.tracks.find(id);
                // 首次见到的 id：以当前球数建立基线，本帧不算掉球（噪声源 b）。
                const int prev = (it == state.tracks.end()) ? kv.second : it->second.ownedBalls;
                const int drop = prev - kv.second;
                if (drop <= 0) continue;
                int covered = ownedShield[id];
                if (covered > drop) covered = drop;            // 一盾抵挡一球
                if (covered > kMaxCoverBalls) covered = kMaxCoverBalls;  // 单核单帧上限
                if (covered > 0) {
                    covers.push_back(CoverEntry{id, covered});
                    totalCovered += covered;
                }
            }

            if (totalCovered > 0) {
                // 台账校准：核心本帧实收罚金对应的球数（四舍五入，宁可少算）。
                int ledgerBalls = static_cast<int>(ledgerDelta / lossCost + 0.5);
                if (ledgerBalls < 0) ledgerBalls = 0;
                int grantable = (ledgerBalls < totalCovered) ? ledgerBalls : totalCovered;
                if (grantable > 0) {
                    // 双重封顶（同 relief）：sqrt 预算 + 台账比例上限，取较小者。
                    const size_t aliveCount = idToIndex.size();
                    const double sqrtBudget =
                        (aliveCount == 0)
                            ? 0.0
                            : std::sqrt(static_cast<double>(aliveCount)) *
                                  kGlobalBallsPerSqrt * lossCost;
                    const double ledgerBudget = ledgerDelta * kRefundCapOfLedger;
                    const double budget = (sqrtBudget < ledgerBudget) ? sqrtBudget : ledgerBudget;

                    // 按抵挡名额占比整数分配（带余数累积，Σ分配 ≤ grantable）。
                    int assigned = 0;
                    int64_t carried = 0;
                    double spent = 0.0;
                    const double refundPerBall = lossCost * kShieldRefundRatio;
                    for (size_t ci = 0; ci < covers.size(); ++ci) {
                        if (assigned >= grantable) break;
                        if (spent >= budget) break;
                        carried += static_cast<int64_t>(grantable) * covers[ci].covered;
                        int share = static_cast<int>(carried / totalCovered);
                        carried %= totalCovered;
                        if (share <= 0) continue;
                        if (assigned + share > grantable) share = grantable - assigned;
                        assigned += share;

                        auto idxIt = idToIndex.find(covers[ci].id);
                        if (idxIt == idToIndex.end()) continue;
                        Nucleus& target = nuclei[static_cast<size_t>(idxIt->second)];
                        if (!target.alive) continue;
                        double refund = static_cast<double>(share) * refundPerBall;
                        const double remaining = budget - spent;
                        if (remaining <= 0.0) break;
                        if (refund > remaining) refund = remaining;
                        if (refund <= 0.0) continue;
                        api.energy.apply(target, refund, EnergyReason::BALL_ROLES_SHIELD);
                        spent += refund;
                    }
                }
            }
        }

        // 5) 更新快照并清理（防跟踪表无界增长）。
        for (const auto& kv : ownedTotal) {
            auto it = state.tracks.find(kv.first);
            if (it == state.tracks.end()) {
                if (state.tracks.size() >= static_cast<size_t>(kMaxTrackedNuclei)) {
                    state.tracks.clear();  // 硬上限：只丢派生量，无正确性风险
                }
                NucleusTrack nt;
                nt.ownedBalls = kv.second;
                nt.lastSeenFrame = e.frame;
                state.tracks.emplace(kv.first, nt);
            } else {
                it->second.ownedBalls = kv.second;
                it->second.lastSeenFrame = e.frame;
            }
        }
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

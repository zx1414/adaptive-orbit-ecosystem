// ============================================================
// mod id:        lucky_lottery
// 名称:          幸运抽奖
// 机制:          每 kLotteryPeriod(300) 帧（e.frame % 300 == 0 且 frame > 0）
//                开一期彩票：所有存活核各买 kTicket(2.0) 注金（energy.apply
//                负值），全部注金一次性发给用 api.rng 均匀抽出的一个幸运核
//                （std::uniform_int_distribution<size_t>(0, aliveCount-1)，
//                从「当前存活核列表」抽）。严格零和：奖金 = Σ注金 =
//                aliveCount × kTicket，一分不留；存活核 <2 则本期跳过。
// 平衡作用:      基线能量完全由吸收/争斗决定，富者恒富、种群易锁死在单一
//                策略。本 mod 注入期望为零、方差巨大的均匀随机重分配
//                （中奖 +2×(N-1)、未中 -2），为自然选择提供「运气」漂变源，
//                让低吸收策略的谱系也有概率靠奖金续命并继续演化。
// touches:       LUCKY_LOTTERY
// incompatible:
// 依赖:          alive>=2（存活核不足 2 时整期跳过，不消耗任何随机数）
// ============================================================
//
// 【零和说明（最重要）】每期台账恒等式：out = Σ(-kTicket) =
// aliveCount × kTicket = 奖金 = in。不注入、不销毁一分能量，与
// niche_guild / symbiosis_bond / energy_spill 同属严格零和 mod。
//
// 【为什么「先发奖金、后收注金」】
//   若先收注金，注金阶段 energy ≤ kTicket(2.0) 的核会被 apply 打死
//   （energy ≤ 0 → alive=false）；若它恰好中签，EnergySystem::apply 对
//   死核是 no-op，奖金将无处可发 → 台账 in < out，零和被破坏。
//   因此本实现把顺序安排为：
//     1) 从当前存活核列表均匀抽签（此刻列表成员全部存活）；
//     2) 立即把奖金 apply 给幸运核（抽签与发奖之间无任何能量变动，
//        中奖核必然存活，奖金必达，且 net +(N-1)×kTicket > 0）；
//     3) 再向全部存活核（含中奖核自己）收取注金。
//   能量加法可交换：各核净变化与「先买后付」逐核一致（中奖核
//   +(N-1)×kTicket，其余各 -kTicket），台账 in/out 恒相等，仅消除了
//   「中奖核恰被注金打死、奖金无主」的边界情况。
//
// 【随机源】唯一随机源 api.rng（std::mt19937&，禁止 rand()/random_device/
// time()）。每期只消耗 1 次抽签；存活核 <2 的帧不开奖、不消耗 rng，
// 因此同种子下 rng 序列完全可复现。
//
// 【状态保存】无任何跨帧状态：每期开奖只依赖当帧存活核列表与 api.rng，
// 不维护 map/游标/快照，因此不需要（也不提供）saveLuckyLottery /
// loadLuckyLottery；读档后行为与连续运行完全一致。

#include "mod_api.h"
#include "World.h"

#include <random>
#include <vector>

namespace {

// ---- 可调常数 ----
constexpr int    kLotteryPeriod = 300;  // 开奖周期（帧）
constexpr double kTicket        = 2.0;  // 每核每期注金（能量）

}  // namespace

void registerLuckyLottery(ModAPI& api) {
    // 函数内无需 static：本 mod 无任何跨帧状态，注册函数本身无副作用。
    api.events.subscribe(EventType::FRAME_END, [&api](const SimEvent& e) {
        if (e.frame <= 0) return;                    // frame > 0 才开第一期
        if (e.frame % kLotteryPeriod != 0) return;   // 每 kLotteryPeriod 帧一期
        if (api.world.finished()) return;            // 终局帧不再开奖（同 climate_event）

        // 当前存活核列表（FRAME_END 时死亡核已被 swap-pop 移除，再按
        // alive 防御一遍；列表顺序即 nuclei() 顺序，仅作抽签样本）。
        std::vector<Nucleus*> alive;
        alive.reserve(api.world.nuclei().size());
        for (Nucleus& n : api.world.nuclei()) {
            if (n.alive) alive.push_back(&n);
        }
        const size_t aliveCount = alive.size();
        if (aliveCount < 2) return;   // 存活核 <2：本期跳过（不消耗 rng）

        // 唯一一次 rng 消耗：均匀抽幸运核（MOD_SPEC：随机只走 api.rng）。
        std::uniform_int_distribution<size_t> pick(0, aliveCount - 1);
        Nucleus& winner = *alive[pick(api.rng)];

        // 1) 先发奖金：距抽签之间无任何能量变动，中奖核必然存活。
        //    奖金 = Σ注金 = aliveCount × kTicket，一分不留。
        const double pot = static_cast<double>(aliveCount) * kTicket;
        api.energy.apply(winner, pot, EnergyReason::LUCKY_LOTTERY);

        // 2) 后收注金：所有存活核（含中奖核）各付 kTicket。
        for (Nucleus* n : alive) {
            api.energy.apply(*n, -kTicket, EnergyReason::LUCKY_LOTTERY);
        }
    });
}

// 无跨帧状态：不提供 saveLuckyLottery / loadLuckyLottery（理由见文件头）。
//
// 【预期可观测效果】单独启用后跑 --energy-summary：
//   * LUCKY_LOTTERY: in == out（|in - out| < 0.01），且均 > 0；
//   * 每期 out = 该期开奖时的存活核数 × 2.0；5000 帧共开奖 16 期
//     （frame = 300, 600, ..., 4800）；
//   * 存活核数与基线相比随机涨落（期望为零的随机漂变，无方向性）。

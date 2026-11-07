// ============================================================
// mod id:        energy_spill
// 名称:          能量溢出再分配
// 机制:          富核判定 energy > 阈值 × kRichFactor(2.0)；每 kTickPeriod(5) 帧在
//                FRAME_END 取溢出 surplus = energy − energyThreshold，按 kSpillRate(0.05)
//                算出 x，零和转给 kSpillRadius(300) 内至多 kMaxRecipients(3) 个贫核
//                （energy < 自身阈值），单次 x < kMinSpill(1.0) 时不转（避免尘埃转移）。
// 平衡作用:      修正「富核能量远超繁殖阈值后白白浪费、贫核却在阈值下饿死」的两极失衡：
//                把浪费的过剩能量严格零和地托举给濒危核，缓解基线种群 20 → 1 的崩溃，
//                并让邻近互补（富者反哺、贫者受助）成为可被自然选择利用的收益。
// touches:       SPILL
// incompatible:  
// 依赖:          无
// ============================================================

#include "mod_api.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// ---- 可调常数（见机制表）----
// 【实测标定 by deputy】基线实测（--balls 1000 --nuclei 20，种子 1/2/42）：
//   平均核能量仅 34~78，而 energyThreshold init 范围 80~200（均值≈140）。
//   原 kRichFactor=2.0 → 富核门槛 2.0×140=280，基线几乎无人达标 → SPILL 从不出现。
//   原 kSpillRadius=300 在 2000×2000 世界、20 核时
//     E[邻居数] = π·300²/4e6·20 ≈ 1.41 —— 期望仅 1.4 个邻居，富核也难找到受体。
// 修正：门槛降到 1.35/0.65，半径放大到 600（E[邻居数]≈11.3），并给受体加接收上限。
// 【实测标定 by deputy】基线实测：唯一幸存核 energy=130.03 / threshold=148.8
// = **0.874×阈值**，且平均核能量仅 34~78。原 kRichFactor=2.0（需 >2×阈值≈280）
// 基线根本达不到 → SPILL 一次都没触发。
// 实测分布显示核几乎总在阈值附近波动（刚繁殖完 0.5×，攒够就繁殖掉回 0.5×），
// 所以"远高于阈值"的富核极少。修正：门槛贴到实际分布上——
//   富核 = 1.05×（正在攒能量、即将繁殖的临界态）
//   贫核 = 0.95×（被繁殖/代谢压到阈值下、最需要补给的态）
// 两个门槛仅差 0.1×阈值，正好卡在核能量真实波动的区间内，保证两侧都有样本。
constexpr double kRichFactor    = 1.05;   // 富核判定：energy > threshold * kRichFactor
constexpr double kSpillBase     = 1.0;    // 溢出基准：surplus = energy - kSpillBase×threshold
constexpr double kMinSurplus     = 3.0;    // 富核最小盈余（能量），低于则不参与（避免 dust）
constexpr double kSpillRate     = 0.05;   // 每次转移 x = surplus * kSpillRate
constexpr double kMinSpill      = 1.0;    // 单次转移下限，低于则跳过（避免尘埃转移）
constexpr double kSpillRadius   = 600.0;  // 受体搜索半径（E[邻居数]≈11.3）
constexpr double kPoorFactor    = 0.95;   // 贫核判定 + 接收上限：补到该阈值即停手
constexpr int    kTickPeriod    = 5;      // 每 N 帧执行一次（摊薄成本）
constexpr int    kMaxRecipients = 3;      // 每个富核每 tick 最多救助的受体数

// 粗均匀网格边长：必须 >= 200，且 >= kSpillRadius 才能保证 3x3 邻域完整覆盖
// 搜索半径（点落在两格外则距离必然 > 格宽 >= 半径）。
constexpr double kCellSize = kSpillRadius;  // 300.0

struct Cand {
    double d2;  // 距离平方（免开方）
    int idx;    // 本帧数组下标——仅本次调用内有效，绝不跨帧保存
};

// 无跨帧状态：网格与候选表每个 tick 都按当前帧核列表重建，下标不持久化，
// 因此省略 saveEnergySpill()/loadEnergySpill()（没有需要持久化的内容）。

// 单次溢出转移 tick：O(N) 建网格 + 每富核仅扫 3x3 邻域。
void spillTick(ModAPI& api) {
    std::vector<Nucleus>& ns = api.world.nuclei();  // 每帧取新的可变引用
    const int nCount = static_cast<int>(ns.size());
    if (nCount == 0) return;

    const WorldConfig& cfg = api.world.config();
    const int cols = std::max(1, static_cast<int>(std::ceil(cfg.width / kCellSize)));
    const int rows = std::max(1, static_cast<int>(std::ceil(cfg.height / kCellSize)));
    const double r2 = kSpillRadius * kSpillRadius;

    // 格子下标：钳位到界内，防御越界坐标。
    auto cellOf = [](double v, int n) {
        int c = static_cast<int>(v / kCellSize);
        if (c < 0) c = 0;
        if (c >= n) c = n - 1;
        return c;
    };

    // 1) 构建粗均匀网格（O(N)，每 tick 重建，索引仅本帧有效）。
    std::vector<std::vector<int>> cells(
        static_cast<size_t>(cols) * static_cast<size_t>(rows));
    for (int i = 0; i < nCount; ++i) {
        if (!ns[static_cast<size_t>(i)].alive) continue;
        const int gx = cellOf(ns[static_cast<size_t>(i)].pos.x, cols);
        const int gy = cellOf(ns[static_cast<size_t>(i)].pos.y, rows);
        cells[static_cast<size_t>(gy * cols + gx)].push_back(i);
    }

    std::vector<Cand> cand;
    cand.reserve(32);

    // 2) 逐核找富核做捐赠者。
    for (int i = 0; i < nCount; ++i) {
        Nucleus& donor = ns[static_cast<size_t>(i)];
        if (!donor.alive) continue;
        if (donor.energy <= donor.params.energyThreshold * kRichFactor) continue;
        // 最小盈余门槛：避免 surplus 只有零点几能量的核反复进入循环却转不出
        // 任何有效金额（dust）。
        if (donor.energy - donor.params.energyThreshold * kSpillBase < kMinSurplus) continue;

        const int cx = cellOf(donor.pos.x, cols);
        const int cy = cellOf(donor.pos.y, rows);

        // 3) 只扫 3x3 邻域：半径内的点必在同格或相邻格（kCellSize >= 半径）。
        cand.clear();
        for (int gy = cy - 1; gy <= cy + 1; ++gy) {
            if (gy < 0 || gy >= rows) continue;
            for (int gx = cx - 1; gx <= cx + 1; ++gx) {
                if (gx < 0 || gx >= cols) continue;
                const std::vector<int>& bucket = cells[static_cast<size_t>(gy * cols + gx)];
                for (int j : bucket) {
                    if (j == i) continue;  // 不能给自己
                    Nucleus& rec = ns[static_cast<size_t>(j)];
                    if (!rec.alive) continue;
                    if (rec.energy >= rec.params.energyThreshold * kPoorFactor) continue;  // 贫核且未达接收上限
                    const double dx = rec.pos.x - donor.pos.x;
                    const double dy = rec.pos.y - donor.pos.y;
                    const double d2 = dx * dx + dy * dy;
                    if (d2 > r2) continue;
                    cand.push_back(Cand{d2, j});
                }
            }
        }
        if (cand.empty()) continue;

        // 4) 近者优先：只把最近的 kMaxRecipients 个排到前面（近似 O(m log k)）。
        if (cand.size() > static_cast<size_t>(kMaxRecipients)) {
            std::partial_sort(cand.begin(), cand.begin() + kMaxRecipients, cand.end(),
                              [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });
        }

        // 5) 对至多 kMaxRecipients 个受体执行严格零和转移。
        int given = 0;
        for (const Cand& c : cand) {
            if (given >= kMaxRecipients) break;

            Nucleus& rec = ns[static_cast<size_t>(c.idx)];
            if (!donor.alive) break;       // 防御：apply 对死核是 no-op，会破坏零和
            if (!rec.alive) continue;
            if (rec.energy >= rec.params.energyThreshold) continue;  // 可能已被喂饱，跳过

            // 每次转移前按 donor 当前能量重算 surplus：先扣的转移自动把后续 x
            // 重新钳位，保证 x 永不超过 donor 实际剩余溢出量。
            // 【口径统一】donor 门槛与转移量必须用同一个基准，否则会出现
            // 「kRichFactor 判定通过、但 surplus<=0 而 break」的空转：
            // kRichFactor=1.35 的门槛是 1.35×threshold，而下面 surplus 用的是
            // 1.0×threshold —— 两个基准不一致会让富核"够格但没溢出"而永不转移。
            // 这里统一用 kSpillBase（= 1.0×threshold）作为溢出基准，
            // 与 select donor 的判定 (energy > threshold*kRichFactor) 组合后
            // 保证进入循环的 donor 一定有正 surplus。
            const double surplus = donor.energy - donor.params.energyThreshold * kSpillBase;
            if (surplus <= 0.0) break;     // 溢出耗尽，本捐赠者本 tick 结束
            double x = surplus * kSpillRate;
            if (x > surplus) x = surplus;  // 防御性钳位：绝不超过实际溢出
            if (x < kMinSpill) break;      // 太小不值得，且后续只会更小

            // ---- 严格零和：同一个 x，恰好两次 apply，返回值一律丢弃 ----
            // 事前不变式保证两侧都生效：donor 扣减后能量 = 阈值 + 0.95×surplus > 0
            // （donor 不会死），rec 只增不减，故 SPILL 总流入 == 总流出，逐比特一致。
            api.energy.apply(donor, -x, EnergyReason::SPILL);
            api.energy.apply(rec, +x, EnergyReason::SPILL);
            ++given;
        }
    }
}

}  // namespace

// 确定性机制：全程未使用随机数（未触碰 api.rng），也无跨帧状态。
void registerEnergySpill(ModAPI& api) {
    api.events.subscribe(EventType::FRAME_END, [&api](const SimEvent& e) {
        if (api.world.finished()) return;
        if (e.frame % kTickPeriod != 0) return;
        spillTick(api);  // FRAME_END 时死亡核已被移除，网格即当前存活集合
    });
}

// 预期可观测效果（单独启用）：跑 --energy-summary 可见 SPILL 的 in/out 均非零且
// 严格相等（总能量守恒）；富核能量增速放缓、濒危贫核被托举回阈值之上，
// 基线种群的 20 → 1 崩溃显著减缓，核间能量基尼系数下降。

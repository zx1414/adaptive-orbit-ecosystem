// ============================================================
// mod id:        symbiosis_bond
// 名称:          核间共生
// 机制:          距离门控的严格零和转移：半径700内，能量>1.0×阈值的富核向<0.9×阈值的贫核转移。
//                步长=阈值×0.02，每捐赠者每tick上限=0.05×其能量，最多2受体。每10帧执行一次。
//                全局每tick预算：alive * 8.0，耗尽即停止，防抖动失控
// 平衡作用:      修正基线仅捕食无合作导致的种群崩溃(20→1)，引入能量再分配维持生态多样性。
// touches:       SYMBIOSIS
// incompatible:  
// 依赖:          无
// ============================================================

#include "mod_api.h"
#include "World.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace {

// Tunable constants
// 【实测标定 by deputy】基线实测（--balls 1000 --nuclei 20，种子 1/2/42）：
//   平均核能量仅 34~78，而 energyThreshold init 范围 80~200（均值≈140）。
//   原 kRichFactor=1.5 → 富核门槛 1.5×140=210，基线几乎无人达标 → 从不触发。
//   原 kBondRadius=250 在 2000×2000 世界、20 核时：
//     E[邻居数] = π·r²/Area·N = π·250²/4e6·20 ≈ 0.98  —— 平均不到 1 个邻居，
//   即使有富核也找不到贫核配对。两个条件同时落空 → 账目整行缺失。
// 修正：门槛降到 1.2/0.8，半径放大到 700（E[邻居数]≈15.4），并加贫核接收上限，
//       防止"富→贫→贫变富→再抽"的单向漏损循环。
// 【实测标定 by deputy】基线实测：唯一幸存核 energy=130.03 / threshold=148.8
// = **0.874×阈值**。原门槛 kRichFactor=1.5（需 >1.5×）与 kPoorFactor=0.5
// （需 <0.5×）在基线分布下**互斥且都够不着**：既没有富核也没有贫核 → 零和循环
// 一次都不成立 → 账目整行空白。
// 修正：两个门槛必须落在实际分布内且富/贫两侧都有人。用 1.0 / 0.9：
//   富核 = 高于自身繁殖阈值（正在攒能量准备繁殖）；
//   贫核 = 低于 0.9×阈值（刚被繁殖/代谢压到阈值下，最需要补给）。
// 这也符合生物学直觉：把"临界的富核"能量匀给"濒临繁殖的贫核"。
constexpr double kBondRadius = 700.0;
constexpr double kRichFactor = 1.0;   // 富核：energy > threshold × 1.0
constexpr double kPoorFactor = 0.9;   // 贫核：energy < threshold × 0.9
constexpr int kTickPeriod = 10;
constexpr double kStepFraction = 0.02;
constexpr double kMaxDrainPerTick = 0.05;  // 每 tick 最多抽 donor 5%（原 0.5 = 50%，过猛）
constexpr int kMaxRecipientsPerDonor = 2;
constexpr double kCellSize = 700.0;  // >= kBondRadius, ensures 3x3 covers all neighbors
constexpr double kMinRecipEnergy = 1.0;  // 受体能量绝对下限（仅防病态，见下方说明)
// 全局每帧转移预算：每个活核允许的最大转移总量（防能量抖动失控）
// 取 8.0：alive=1000 时 budget=8,000，远低于观测到的 1.69M 抖动量级
constexpr double kGlobalCapPerAlive = 8.0;

} // anonymous namespace

// 【命名空间隔离】核心 src/SpatialGrid.h 已导出全局类 SpatialGrid。
// mod 自定义类型一律放进 bondmod 独立命名空间并加 Mod 前缀，
// 绝不在全局作用域定义与核心同名的类型（会触发 C2872 二义性）。
namespace bondmod {

struct GridCell {
    std::vector<int> indices;  // indices into the current frame's nuclei vector
};

class BondGrid {
public:
    // 【越界崩溃修复】cols_/rows_ 必须至少为 1：若 worldWidth/worldHeight 为 0
    // （或传入负值），ceil(0/cell)=0 会让 cells_ 为空；随后 getCell 的 clamp 会把
    // col 夹到 cols_-1 = -1，cells_[row*cols_+col] 变成 cells_[-1] → 访问违例
    // 0xC0000005（Release/Debug 都崩，与优化无关）。这里用 max(1, ...) 兜底。
    BondGrid(double worldWidth, double worldHeight, double cellSize)
        : width_(worldWidth > 0.0 ? worldWidth : 0.0),
          height_(worldHeight > 0.0 ? worldHeight : 0.0),
          cellSize_(cellSize > 0.0 ? cellSize : 1.0),
          cols_(std::max(1, static_cast<int>(std::ceil(width_ / cellSize_)))),
          rows_(std::max(1, static_cast<int>(std::ceil(height_ / cellSize_)))),
          cells_(static_cast<std::size_t>(cols_) * static_cast<std::size_t>(rows_)) {}

    void clear() {
        for (auto& cell : cells_) {
            cell.indices.clear();
        }
    }

    void insert(int idx, const Vec2& pos) {
        const std::size_t i = cellIndex(clampCol(pos.x), clampRow(pos.y));
        if (i < cells_.size()) cells_[i].indices.push_back(idx);
    }

    const GridCell& getCell(int col, int row) const {
        const std::size_t i = cellIndex(col, row);
        if (i >= cells_.size()) return kEmptyCell;   // 兜底：绝不返回越界引用
        return cells_[i];
    }

    int cols() const { return cols_; }
    int rows() const { return rows_; }

private:
    // 静态空 cell：越界时的安全返回目标（生命周期 = 整个程序）
    static const GridCell kEmptyCell;

    int clampCol(double x) const {
        int c = static_cast<int>(x / cellSize_);
        if (c < 0) c = 0;
        if (c >= cols_) c = cols_ - 1;
        return c;
    }
    int clampRow(double y) const {
        int r = static_cast<int>(y / cellSize_);
        if (r < 0) r = 0;
        if (r >= rows_) r = rows_ - 1;
        return r;
    }
    // col/row 允许越界（调用方传邻域偏移），这里统一夹取 + 返回 size_t 下标
    std::size_t cellIndex(int col, int row) const {
        if (col < 0) col = 0;
        if (col >= cols_) col = cols_ - 1;
        if (row < 0) row = 0;
        if (row >= rows_) row = rows_ - 1;
        return static_cast<std::size_t>(row) * static_cast<std::size_t>(cols_) +
               static_cast<std::size_t>(col);
    }

    double width_, height_, cellSize_;
    int cols_, rows_;
    std::vector<GridCell> cells_;
};

// kEmptyCell 的定义（静态存储期，安全返回目标）
const GridCell BondGrid::kEmptyCell{};

} // namespace bondmod

void registerSymbiosisBond(ModAPI& api) {
    using bondmod::BondGrid;

    // 【生命周期铁律】grid 必须是 static：EventBus 的 std::function 比 World
    // 活得更久（见 mod_api.h:21-22）。用 registerXxx 的局部变量 + [&] 捕获时，
    // 函数返回后 grid 即销毁，回调访问悬垂引用 → 0xC0000005 访问违例。
    // （与 rare_ball_subsidy 是同一个 bug；ball_loss_relief 用 static 是正确对照。）
    static BondGrid grid(1.0, 1.0, kCellSize);

    api.events.subscribe(EventType::FRAME_END, [&](const SimEvent& e) {
        if (api.world.finished()) return;
        if (e.frame % kTickPeriod != 0) return;

        // Rebuild grid for current frame
        const auto& nuclei = api.world.nuclei();
        const WorldConfig& cfg = api.world.config();
        
        grid = BondGrid(cfg.width, cfg.height, kCellSize);
        for (int i = 0; i < static_cast<int>(nuclei.size()); ++i) {
            if (nuclei[i].alive) {
                grid.insert(i, nuclei[i].pos);
            }
        }

        // Get mutable reference to nuclei for energy.apply.
        // 非 const 重载：World.h:104-106 说明该重载就是为 energy.apply 提供的，
        // 不需要也不允许 const_cast。
        auto& mutableNuclei = api.world.nuclei();

        // 全局每帧转移预算：统计活核数 → budget = alive * kGlobalCapPerAlive
        // 在预算耗尽前进行转移；耗尽后本帧停止转移。保持严格零和（同一个 x 正负各一次）。
        int alive = 0;
        for (int i = 0; i < static_cast<int>(mutableNuclei.size()); ++i) {
            if (mutableNuclei[i].alive) ++alive;
        }
        const double budget = alive * kGlobalCapPerAlive;
        double moved = 0.0;

        // Process each donor (rich nucleus)
        for (int donorIdx = 0; donorIdx < static_cast<int>(mutableNuclei.size()); ++donorIdx) {
            Nucleus& donor = mutableNuclei[donorIdx];
            if (!donor.alive) continue;

            double donorThreshold = donor.params.energyThreshold;
            double richThreshold = donorThreshold * kRichFactor;
            if (donor.energy <= richThreshold) continue;  // Not rich enough

            // Find potential recipients in 3x3 neighborhood
            int donorCol = static_cast<int>(donor.pos.x / kCellSize);
            int donorRow = static_cast<int>(donor.pos.y / kCellSize);
            
            std::vector<int> recipients;
            recipients.reserve(8);
            
            for (int dc = -1; dc <= 1; ++dc) {
                for (int dr = -1; dr <= 1; ++dr) {
                    const bondmod::GridCell& cell = grid.getCell(donorCol + dc, donorRow + dr);
                    for (int recipIdx : cell.indices) {
                        if (recipIdx == donorIdx) continue;
                        Nucleus& recip = mutableNuclei[recipIdx];
                        if (!recip.alive) continue;
                        
                        double recipThreshold = recip.params.energyThreshold;
                        double poorThreshold = recipThreshold * kPoorFactor;
                        // 接收保底：受体补到 kPoorFactor 阈值即停手。
                        // 没有这条，受体会被一路补成"富核"，再被其它 donor 抽走，
                        // 形成 富→贫→贫变富→再抽 的单向漏损循环（系统净能量被 mod 抽走）。
                        if (recip.energy >= poorThreshold) continue;
                        
                        // Distance check
                        double dx = recip.pos.x - donor.pos.x;
                        double dy = recip.pos.y - donor.pos.y;
                        double distSq = dx * dx + dy * dy;
                        if (distSq > kBondRadius * kBondRadius) continue;
                        
                        recipients.push_back(recipIdx);
                    }
                }
            }

            if (recipients.empty()) continue;

            // Limit recipient count per donor
            if (static_cast<int>(recipients.size()) > kMaxRecipientsPerDonor) {
                // Simple deterministic selection: closest first
                std::sort(recipients.begin(), recipients.end(),
                    [&](int a, int b) {
                        double dx1 = mutableNuclei[a].pos.x - donor.pos.x;
                        double dy1 = mutableNuclei[a].pos.y - donor.pos.y;
                        double dx2 = mutableNuclei[b].pos.x - donor.pos.x;
                        double dy2 = mutableNuclei[b].pos.y - donor.pos.y;
                        return (dx1*dx1 + dy1*dy1) < (dx2*dx2 + dy2*dy2);
                    });
                recipients.resize(kMaxRecipientsPerDonor);
            }

            // Compute step amount (absolute, same for all transfers this tick)
            double x = donorThreshold * kStepFraction;
            if (x <= 0) continue;

            // Cap total outflow per donor per tick
            double maxOutflow = donor.energy * kMaxDrainPerTick;
            int maxTransfers = static_cast<int>(std::floor(maxOutflow / x));
            if (maxTransfers <= 0) continue;
            if (static_cast<int>(recipients.size()) > maxTransfers) {
                recipients.resize(maxTransfers);
            }

            // Perform strictly zero-sum transfers
            for (int recipIdx : recipients) {
                Nucleus& recip = mutableNuclei[recipIdx];
                if (!recip.alive) continue;

                // Guard: donor must stay above the poor threshold after paying.
                if (donor.energy - x <= donorThreshold * kPoorFactor) break;

                // 【空转修复】原实现有 recipMaxInflow = recip.energy * kMaxDrainPerTick，
                // 而 kMaxDrainPerTick 已降到 0.05、且受体正是"贫核"（能量本就最低），
                // 于是 recipMaxInflow 恒小于单步 x → 每对都被 `continue` 跳过，
                // 表现为账目完全空白。受体接收上限应按**阈值**而非自身能量来定，
                // 且由上面的 kPoorFactor 接收上限保证不会喂过头。
                // 保留一个仅防病态的绝对下限：
                if (recip.energy < kMinRecipEnergy) continue;

                // 全局预算检查：若剩余预算不足，本 tick 停止转移
                if (moved + x > budget) {
                    goto symbiosis_transfer_done;
                }

                // STRICTLY ZERO-SUM: same x for both calls
                api.energy.apply(donor, -x, EnergyReason::SYMBIOSIS);
                api.energy.apply(recip, +x, EnergyReason::SYMBIOSIS);
                moved += x;
            }
        }
        symbiosis_transfer_done: ;
    });
}

// No cross-frame state needed; save/load intentionally omitted.
// If future state is added, implement:
// void saveSymbiosisBond(std::ostream& out);
// void loadSymbiosisBond(std::istream& in);
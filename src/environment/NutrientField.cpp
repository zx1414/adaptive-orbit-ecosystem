// agentsim - environment/NutrientField.cpp
// 残骸/养分网格场的实现：纯确定性、零 RNG、不碰能量账本、不依赖遍历顺序之外的
// 隐式状态。分区/分解数学的定义见 NutrientField.h 的文件头注释。
#include "NutrientField.h"

#include <algorithm>
#include <cmath>

namespace agentsim {

namespace {

// 单轴分区下标：v 必须已 wrap 到 [0, worldW)。以 cell（= worldW/cols）为步长
// 取 floor 再 clamp——这样分区边界与 cellCenter 使用同一个 cell 常量，两者
// 数学上自洽（不依赖 worldW/cell 恰为整数的浮点巧合）。
// n <= 1 或 cell <= 0（退化世界）时恒为 0；NaN 也落到 0（NaN > 0 为假）。
int axisIndex(double v, double cell, int n) {
    if (n <= 1 || !(cell > 0.0)) return 0;
    const double scaled = v / cell;
    if (!(scaled > 0.0)) return 0;
    const double f = std::floor(scaled);
    if (f >= static_cast<double>(n)) return n - 1;
    return static_cast<int>(f);
}

// 只接受有限正增量：NaN / inf / <=0 一律无操作（不产生 NaN 库存）。
bool acceptableAmount(double amount) {
    return std::isfinite(amount) && amount > 0.0;
}

} // namespace

NutrientField::NutrientField(double worldW, double worldH, double regionSize,
                             double initialPerRegion) {
    configure(worldW, worldH, regionSize, initialPerRegion);
}

void NutrientField::configure(double worldW, double worldH, double regionSize,
                              double initialPerRegion) {
    // 非法/退化输入退化为 1x1 网格：保证 cellCount() >= 1、任何坐标都有归属、
    // 且不会出现除以 0 的 cell 尺寸。
    const bool okW = std::isfinite(worldW) && worldW > 0.0;
    const bool okH = std::isfinite(worldH) && worldH > 0.0;
    const bool okRegion = std::isfinite(regionSize) && regionSize > 0.0;
    worldW_ = okW ? worldW : 0.0;
    worldH_ = okH ? worldH : 0.0;
    // regionSize 非法时退化为 1.0 并落到 1x1 网格；regionSize() 恒报一个正数，
    // 不会给调用方返回 NaN/0/负数。
    regionSize_ = okRegion ? regionSize : 1.0;
    cols_ = 1;
    rows_ = 1;
    if (okW && okH && okRegion) {
        // floor 而非 round：分区尺寸 >= regionSize 的约定（"至少这么大一块"）。
        // 上限 4096 只是防呆护栏（worldW/regionSize 极端时 vector 会炸），
        // 不改变正常参数的语义。
        const double c = std::floor(worldW_ / regionSize_);
        const double r = std::floor(worldH_ / regionSize_);
        const double cap = 4096.0;
        cols_ = static_cast<int>(c < 1.0 ? 1.0 : (c > cap ? cap : c));
        rows_ = static_cast<int>(r < 1.0 ? 1.0 : (r > cap ? cap : r));
    }
    cellW_ = worldW_ / static_cast<double>(cols_);
    cellH_ = worldH_ / static_cast<double>(rows_);
    const double init = acceptableAmount(initialPerRegion) ? initialPerRegion : 0.0;
    const std::size_t n =
        static_cast<std::size_t>(cols_) * static_cast<std::size_t>(rows_);
    // 初值只写 nutrient：debris 必须由死亡等真实来源产生，否则开局就有一条
    // 无来源的分解流（会把"养分守恒"的口径弄脏）。
    cells_.assign(n, NutrientCell{});
    for (NutrientCell& c : cells_) c.nutrient = init;
}

std::size_t NutrientField::indexOf(int ix, int iy) const {
    if (ix < 0) ix = 0;
    if (iy < 0) iy = 0;
    if (ix >= cols_) ix = cols_ - 1;
    if (iy >= rows_) iy = rows_ - 1;
    return static_cast<std::size_t>(iy) * static_cast<std::size_t>(cols_) +
           static_cast<std::size_t>(ix);
}

int NutrientField::ixOf(std::size_t index) const {
    if (cells_.empty()) return 0;
    return static_cast<int>(index % static_cast<std::size_t>(cols_));
}

int NutrientField::iyOf(std::size_t index) const {
    if (cells_.empty()) return 0;
    return static_cast<int>(index / static_cast<std::size_t>(cols_));
}

std::size_t NutrientField::indexAt(Vec2 p) const {
    const Vec2 q = wrap(p, worldW_, worldH_);
    return indexOf(axisIndex(q.x, cellW_, cols_), axisIndex(q.y, cellH_, rows_));
}

Vec2 NutrientField::cellCenter(std::size_t index) const {
    const std::size_t safe = index < cells_.size() ? index : 0;
    const double ix = static_cast<double>(ixOf(safe));
    const double iy = static_cast<double>(iyOf(safe));
    return Vec2{(ix + 0.5) * cellW_, (iy + 0.5) * cellH_};
}

Vec2 NutrientField::centerAt(Vec2 p) const { return cellCenter(indexAt(p)); }

const NutrientCell& NutrientField::cellAt(Vec2 p) const {
    return cells_[indexAt(p)];
}

double NutrientField::takeNutrient(Vec2 p, double want) {
    if (!(want > 0.0)) return 0.0; // NaN / 0 / 负数：不动，返回 0
    NutrientCell& c = cells_[indexAt(p)];
    const double have = c.nutrient > 0.0 ? c.nutrient : 0.0;
    if (have <= 0.0) return 0.0;
    const double taken = want < have ? want : have; // inf 时取的是 have
    c.nutrient = have - taken;                      // 相等时恰好为 0，绝不欠账
    return taken;
}

void NutrientField::addNutrient(Vec2 p, double amount) {
    if (!acceptableAmount(amount)) return;
    cells_[indexAt(p)].nutrient += amount;
}

void NutrientField::addDebris(Vec2 p, double amount) {
    if (!acceptableAmount(amount)) return;
    cells_[indexAt(p)].debris += amount;
}

double NutrientField::decompose(double ratePerTick) {
    if (!(ratePerTick > 0.0)) return 0.0; // NaN / 0 / 负数：无操作
    const double rate = ratePerTick > 1.0 ? 1.0 : ratePerTick; // 上限 1：禁止透支
    double convertedTotal = 0.0;
    // 逐区域按下标顺序处理：converted 同时参与"减残骸"与"加养分"，区域内严格
    // 零和；fl(debris*rate) <= debris 对 rate<=1 恒成立，故残骸不会变负。
    for (NutrientCell& c : cells_) {
        if (!(c.debris > 0.0)) continue;
        const double converted = c.debris * rate;
        c.debris -= converted;
        c.nutrient += converted;
        convertedTotal += converted;
    }
    return convertedTotal;
}

void NutrientField::diffuse(double rate) {
    if (!acceptableAmount(rate) || cells_.size() <= 1) return;
    rate = std::min(rate, 1.0);
    const int neighbourCount = (cols_ > 1 ? 2 : 0) + (rows_ > 1 ? 2 : 0);
    std::vector<double> next(cells_.size(), 0.0);
    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < cols_; ++x) {
            const auto index = indexOf(x, y);
            const double stock = cells_[index].nutrient;
            const double distributed = stock * rate;
            const double share = distributed / neighbourCount;
            next[index] += stock - distributed;
            if (cols_ > 1) {
                next[indexOf((x + cols_ - 1) % cols_, y)] += share;
                next[indexOf((x + 1) % cols_, y)] += share;
            }
            if (rows_ > 1) {
                next[indexOf(x, (y + rows_ - 1) % rows_)] += share;
                next[indexOf(x, (y + 1) % rows_)] += share;
            }
        }
    }
    for (std::size_t i = 0; i < cells_.size(); ++i) cells_[i].nutrient = next[i];
}

double NutrientField::totalDebris() const {
    double sum = 0.0;
    for (const NutrientCell& c : cells_) sum += c.debris;
    return sum;
}

double NutrientField::totalNutrient() const {
    double sum = 0.0;
    for (const NutrientCell& c : cells_) sum += c.nutrient;
    return sum;
}

bool NutrientField::restoreCells(const std::vector<NutrientCell>& cells) {
    if (cells.size() != cells_.size()) return false;
    // 非法值（负/NaN）在入口就拒绝整批，保持"库存恒非负"的不变量。
    for (const NutrientCell& c : cells) {
        if (!(c.debris >= 0.0) || !(c.nutrient >= 0.0)) return false;
    }
    cells_ = cells;
    return true;
}

} // namespace agentsim

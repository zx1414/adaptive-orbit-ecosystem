#include "world/SpatialIndex.h"
#include "core/Math.h"
#include <algorithm>
#include <cmath>

namespace world {
SpatialIndex::SpatialIndex(core::Bounds bounds, double cellSize)
    : bounds_(bounds.valid() ? bounds : core::Bounds{}) {
    constexpr double kMaxCellsPerAxis = 4096.0;
    const double width = std::max(0.0, bounds_.width());
    const double height = std::max(0.0, bounds_.height());
    cellSize_ = std::max({1.0, cellSize, width / kMaxCellsPerAxis, height / kMaxCellsPerAxis});
    columns_ = std::clamp(static_cast<int>(std::ceil(width / cellSize_)), 1, 4096);
    rows_ = std::clamp(static_cast<int>(std::ceil(height / cellSize_)), 1, 4096);
    cells_.resize(static_cast<std::size_t>(columns_ * rows_));
}
int SpatialIndex::cellX(double x) const {
    if (!std::isfinite(x)) return 0;
    const double value = std::floor((x - bounds_.minX) / cellSize_);
    return std::clamp(static_cast<int>(std::clamp(value, 0.0, static_cast<double>(columns_ - 1))), 0, columns_ - 1);
}
int SpatialIndex::cellY(double y) const {
    if (!std::isfinite(y)) return 0;
    const double value = std::floor((y - bounds_.minY) / cellSize_);
    return std::clamp(static_cast<int>(std::clamp(value, 0.0, static_cast<double>(rows_ - 1))), 0, rows_ - 1);
}
int SpatialIndex::clampedCellX(double x) const { return cellX(x); }
int SpatialIndex::clampedCellY(double y) const { return cellY(y); }
void SpatialIndex::clear() {
    for (auto& cell : cells_) cell.clear();
    entries_.clear();
}
void SpatialIndex::insert(core::EntityId id, core::Vec2 position) {
    if (id == core::kInvalidEntityId || !core::Math::isFinite(position)) return;
    entries_.push_back({id, position});
    const int x = cellX(position.x);
    const int y = cellY(position.y);
    cells_[static_cast<std::size_t>(y * columns_ + x)].push_back({id, position});
}
void SpatialIndex::queryUnsorted(core::Vec2 center, double radius, std::vector<core::EntityId>& out) const {
    out.clear();
    if (!core::Math::isFinite(center) || !std::isfinite(radius) || radius < 0.0) return;
    const int x0 = cellX(center.x - radius);
    const int x1 = cellX(center.x + radius);
    const int y0 = cellY(center.y - radius);
    const int y1 = cellY(center.y + radius);
    const double radiusSq = radius * radius;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const auto& cell = cells_[static_cast<std::size_t>(y * columns_ + x)];
            for (const CellEntry& entry : cell) {
                if ((entry.position - center).lengthSq() <= radiusSq) out.push_back(entry.id);
            }
        }
    }
}
void SpatialIndex::query(core::Vec2 center, double radius, std::vector<core::EntityId>& out) const {
    queryUnsorted(center, radius, out);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}
} // namespace world

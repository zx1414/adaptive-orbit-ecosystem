#pragma once
#include "core/Types.h"
#include <vector>

namespace world {
class SpatialIndex {
public:
    struct Entry {
        core::EntityId id = core::kInvalidEntityId;
        core::Vec2 position{};
    };
    SpatialIndex(core::Bounds bounds, double cellSize = 80.0);
    void clear();
    void insert(core::EntityId id, core::Vec2 position);
    void query(core::Vec2 center, double radius, std::vector<core::EntityId>& out) const;
    // Same candidate set as query(), but without sorting/deduplicating. Callers
    // that only count or select by distance use this to avoid O(k log k).
    void queryUnsorted(core::Vec2 center, double radius, std::vector<core::EntityId>& out) const;
    std::size_t size() const { return entries_.size(); }
private:
    struct CellEntry { core::EntityId id; core::Vec2 position; };
    int cellX(double x) const;
    int cellY(double y) const;
    int clampedCellX(double x) const;
    int clampedCellY(double y) const;
    core::Bounds bounds_;
    double cellSize_;
    int columns_;
    int rows_;
    std::vector<std::vector<CellEntry>> cells_;
    std::vector<Entry> entries_;
};
} // namespace world

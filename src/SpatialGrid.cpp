#include "SpatialGrid.h"
#include <algorithm>
#include <cmath>

SpatialGrid::SpatialGrid(double worldWidth, double worldHeight, double cellSize)
    : cellSize_(cellSize),
      worldWidth_(worldWidth), worldHeight_(worldHeight) {
    cols_ = std::max(1, (int)std::ceil(worldWidth_ / cellSize_));
    rows_ = std::max(1, (int)std::ceil(worldHeight_ / cellSize_));
    ballCells_.assign((size_t)cols_ * rows_, {});
    nucleusCells_.assign((size_t)cols_ * rows_, {});
}

void SpatialGrid::clear() {
    for (auto& c : ballCells_) c.clear();
    for (auto& c : nucleusCells_) c.clear();
}

int SpatialGrid::cellX(double x) const {
    int cx = (int)(x / cellSize_);
    if (cx < 0) cx = 0;
    if (cx >= cols_) cx = cols_ - 1;
    return cx;
}

int SpatialGrid::cellY(double y) const {
    int cy = (int)(y / cellSize_);
    if (cy < 0) cy = 0;
    if (cy >= rows_) cy = rows_ - 1;
    return cy;
}

void SpatialGrid::addBall(int index, const Vec2& pos) {
    int cx = cellX(pos.x), cy = cellY(pos.y);
    ballCells_[(size_t)cy * cols_ + cx].push_back(index);
}

void SpatialGrid::addNucleus(int index, const Vec2& pos) {
    int cx = cellX(pos.x), cy = cellY(pos.y);
    nucleusCells_[(size_t)cy * cols_ + cx].push_back(index);
}

void SpatialGrid::cellRange(double minX, double minY, double maxX, double maxY,
                            int& cx0, int& cx1, int& cy0, int& cy1) const {
    cx0 = cellX(minX);
    cx1 = cellX(maxX);
    cy0 = cellY(minY);
    cy1 = cellY(maxY);
}

const std::vector<int>& SpatialGrid::ballsAt(int cx, int cy) const {
    return ballCells_[(size_t)cy * cols_ + cx];
}

const std::vector<int>& SpatialGrid::nucleiAt(int cx, int cy) const {
    return nucleusCells_[(size_t)cy * cols_ + cx];
}

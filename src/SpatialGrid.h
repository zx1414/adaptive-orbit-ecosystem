#pragma once
#include <vector>
#include "Vec2.h"

// 空间哈希网格：把小球/核的索引按世界坐标分桶，用于邻近查询。
// 仅存储索引（不复制对象），每帧重建。
class SpatialGrid {
public:
    SpatialGrid(double worldWidth, double worldHeight, double cellSize);

    void clear();
    void addBall(int index, const Vec2& pos);
    void addNucleus(int index, const Vec2& pos);

    // 计算覆盖矩形 [minX,maxX]x[minY,maxY] 的单元格范围（已裁剪到网格内）。
    void cellRange(double minX, double minY, double maxX, double maxY,
                   int& cx0, int& cx1, int& cy0, int& cy1) const;

    const std::vector<int>& ballsAt(int cx, int cy) const;
    const std::vector<int>& nucleiAt(int cx, int cy) const;

    int cellX(double x) const;
    int cellY(double y) const;
    double cellSize() const { return cellSize_; }

private:
    double cellSize_;
    int cols_, rows_;
    double worldWidth_, worldHeight_;
    std::vector<std::vector<int>> ballCells_;
    std::vector<std::vector<int>> nucleusCells_;
};

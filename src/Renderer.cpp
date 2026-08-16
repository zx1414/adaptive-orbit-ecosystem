#include "Renderer.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

Renderer::Renderer(int cols, int rows) : cols_(cols), rows_(rows) {}

namespace {
void clearScreen() {
#ifdef _WIN32
    std::system("cls");
#else
    std::system("clear");
#endif
}
}  // namespace

void Renderer::render(const World& world) {
    const auto& balls = world.balls();
    const auto& nuclei = world.nuclei();
    const WorldConfig& cfg = world.config();

    // 优先级：核(-1) 覆盖小球；小球按类型优先级 Shield(0) > Worker(1) > Scout(2)。
    std::vector<char> grid((size_t)cols_ * rows_, ' ');
    std::vector<int> priority((size_t)cols_ * rows_, INT_MAX);

    auto cellFor = [&](const Vec2& p, int& cx, int& cy) {
        cx = (int)(p.x / cfg.width * cols_);
        cy = (int)(p.y / cfg.height * rows_);
        cx = std::clamp(cx, 0, cols_ - 1);
        cy = std::clamp(cy, 0, rows_ - 1);
    };

    for (const Ball& b : balls) {
        int cx, cy;
        cellFor(b.pos, cx, cy);
        int idx = cy * cols_ + cx;
        int p = static_cast<int>(b.type);
        if (p < priority[idx]) {
            priority[idx] = p;
            grid[idx] = ballChar(b.type);
        }
    }

    for (const Nucleus& n : nuclei) {
        if (!n.alive) continue;
        int cx, cy;
        cellFor(n.pos, cx, cy);
        int idx = cy * cols_ + cx;
        grid[idx] = (n.energy >= n.params.energyThreshold) ? 'N' : 'n';
        priority[idx] = -1;
    }

    if (clear_) clearScreen();

    std::string out;
    out.reserve((size_t)(cols_ + 1) * rows_ + 128);
    for (int cy = 0; cy < rows_; ++cy) {
        for (int cx = 0; cx < cols_; ++cx) {
            out.push_back(grid[(size_t)cy * cols_ + cx]);
        }
        out.push_back('\n');
    }

    std::cout << out;
    std::cout << "帧 " << world.frame()
              << " | 存活核 " << world.aliveNucleusCount()
              << " | 小球 " << balls.size()
              << " | 平均能量 " << std::fixed << std::setprecision(2)
              << world.averageNucleusEnergy() << "\n";
    std::cout.flush();
}

#include "ConsoleRenderer.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

ConsoleRenderer::ConsoleRenderer(int cols, int rows) : cols_(cols), rows_(rows) {}

namespace {
void clearScreen() {
#ifdef _WIN32
    std::system("cls");
#else
    std::system("clear");
#endif
}

// 单元格优先级（**数值越小越优先**，沿用原有约定）：
//   核 = -1（最高） > 小球 = 0/1/2（按类型） > 蓝细菌节点 = 3 > 蓝细菌连线 = 4（最低）
// 因此核与小球的既有优先级语义**一字未改**：蓝细菌只在核/球未占用的格子上落地。
constexpr int kPrioCyanoNode = 3;
constexpr int kPrioCyanoLink = 4;
constexpr char kCyanoLinkChar = '.';  // 连线用点状字符，与节点字符区分且不喧宾夺主

// cyanoRenderChar：0='*' 1='o' 2='+'（与 n/N/s/w/c/空格 均不冲突）。
char cyanoNodeChar(int which) {
    switch (which) {
        case 1: return 'o';
        case 2: return '+';
        case 0:
        default: return '*';
    }
}
}  // namespace

void ConsoleRenderer::render(const World& world) {
    const auto& balls = world.balls();
    const auto& nuclei = world.nuclei();
    const WorldConfig& cfg = world.config();

    // 关闭态（群落为空）→ 完全不触碰蓝细菌任何接口，零额外开销。
    const Cyanobacteria& cyano = world.cyano();
    const bool cyanoOn = !cyano.empty();

    // 优先级：核(-1) 覆盖小球；小球按类型优先级 Shield(0) > Worker(1) > Scout(2)。
    std::vector<char> grid((size_t)cols_ * rows_, ' ');
    std::vector<int> priority((size_t)cols_ * rows_, INT_MAX);

    auto cellFor = [&](const Vec2& p, int& cx, int& cy) {
        cx = (int)(p.x / cfg.width * cols_);
        cy = (int)(p.y / cfg.height * rows_);
        cx = std::clamp(cx, 0, cols_ - 1);
        cy = std::clamp(cy, 0, rows_ - 1);
    };

    // ---- 蓝细菌（先画，优先级最低，不会盖住核与球）----
    if (cyanoOn) {
        // 连线：字符网格上用 Bresenham 画点状线；只写优先级更低的格子。
        if (cfg.cyanoRenderLinks) {
            const std::vector<CyanoNode>& ns = cyano.nodes();
            for (const CyanoLink& l : cyano.links()) {
                if (l.a >= ns.size() || l.b >= ns.size()) continue;
                if (ns[l.a].alive == 0 || ns[l.b].alive == 0) continue;  // 帧内瞬态：跳过
                int x0, y0, x1, y1;
                cellFor(ns[l.a].pos, x0, y0);
                cellFor(ns[l.b].pos, x1, y1);
                int dx = std::abs(x1 - x0);
                int dy = -std::abs(y1 - y0);
                int sx = (x0 < x1) ? 1 : -1;
                int sy = (y0 < y1) ? 1 : -1;
                int err = dx + dy;
                for (;;) {
                    const int idx = y0 * cols_ + x0;
                    if (kPrioCyanoLink < priority[idx]) {
                        priority[idx] = kPrioCyanoLink;
                        grid[idx] = kCyanoLinkChar;
                    }
                    if (x0 == x1 && y0 == y1) break;
                    const int e2 = 2 * err;
                    if (e2 >= dy) { err += dy; x0 += sx; }
                    if (e2 <= dx) { err += dx; y0 += sy; }
                }
            }
        }
        // 节点：字符由 cyanoRenderChar 决定（钳制到 0..2，非法值按 '*' 处理）。
        const char nc = cyanoNodeChar(cfg.cyanoRenderChar);
        for (const CyanoNode& nd : cyano.nodes()) {
            if (nd.alive == 0) continue;
            int cx, cy2;
            cellFor(nd.pos, cx, cy2);
            const int idx = cy2 * cols_ + cx;
            if (kPrioCyanoNode < priority[idx]) {
                priority[idx] = kPrioCyanoNode;
                grid[idx] = nc;
            }
        }
    }

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
              << world.averageNucleusEnergy();
    // 蓝细菌统计：仅在开启时追加（关闭态输出与基线**逐字节一致**）。
    if (cyanoOn) {
        std::cout << " | 蓝细菌节点 " << cyano.aliveNodeCount()
                  << " | 蓝细菌连接 " << cyano.aliveLinkCount()
                  << " | 节点平均能量 " << std::setprecision(2)
                  << cyano.averageNodeEnergy()
                  << " | 阳光 " << std::setprecision(2) << world.sunLevel();
    }
    std::cout << "\n";
    std::cout.flush();
}

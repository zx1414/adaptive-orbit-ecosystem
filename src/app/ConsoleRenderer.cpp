#include "app/ConsoleRenderer.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace app {
ConsoleRenderer::ConsoleRenderer(int columns, int rows)
    : columns_(std::max(20, columns)), rows_(std::max(10, rows)) {}
void ConsoleRenderer::render(const world::WorldView& view, std::ostream& out) const {
    std::vector<char> grid(static_cast<std::size_t>(columns_ * rows_), '.');
    auto cell = [&](core::Vec2 position) {
        const double w = std::max(1e-9, view.bounds.width());
        const double h = std::max(1e-9, view.bounds.height());
        double x = (position.x - view.bounds.minX) / w * columns_;
        double y = (position.y - view.bounds.minY) / h * rows_;
        if (!std::isfinite(x) || !std::isfinite(y)) return std::size_t{0};
        x = std::clamp(x, 0.0, static_cast<double>(columns_ - 1));
        y = std::clamp(y, 0.0, static_cast<double>(rows_ - 1));
        return static_cast<std::size_t>(static_cast<int>(y) * columns_ + static_cast<int>(x));
    };
    for (const auto& producer : view.producers) {
        grid[cell(producer.position)] = producer.cap > 0.0 && producer.energy / producer.cap > 0.5 ? 'P' : 'p';
    }
    for (const auto& agent : view.agents) {
        grid[cell(agent.position)] = agent.cap > 0.0 && agent.energy / agent.cap > 0.5 ? 'A' : 'a';
    }
    out << "\x1b[2J\x1b[H";
    for (int y = 0; y < rows_; ++y) {
        out.write(grid.data() + static_cast<std::size_t>(y * columns_), columns_);
        out << '\n';
    }
    const auto& stats = view.stats;
    out << "tick=" << stats.tick << " producers=" << stats.producers
        << " agents=" << stats.agents << " energy=" << stats.totalEnergy
        << " injected=" << stats.injected << " dissipated=" << stats.dissipated
        << " audit=" << (stats.auditOk ? "ok" : "FAIL") << '\n';
}
} // namespace app

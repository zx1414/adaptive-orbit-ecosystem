#pragma once
#include "world/WorldView.h"
#include <ostream>

namespace app {
class ConsoleRenderer {
public:
    ConsoleRenderer(int columns = 80, int rows = 40);
    void render(const world::WorldView& view, std::ostream& out) const;
private:
    int columns_;
    int rows_;
};
} // namespace app

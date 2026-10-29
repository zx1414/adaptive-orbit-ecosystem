#pragma once
#include "sim/Simulation.h"
#include <cstddef>
#include <fstream>
#include <string>

namespace app {
class StatsWriter {
public:
    StatsWriter(const std::string& path, const sim::Simulation& world);
    bool ok() const { return ok_; }
    void write(const sim::Simulation& world);
private:
    std::ofstream out_;
    std::size_t geneCount_ = 0;
    bool ok_ = false;
};
} // namespace app

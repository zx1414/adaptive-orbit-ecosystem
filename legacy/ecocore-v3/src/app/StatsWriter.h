#pragma once
#include "colony/ColonySystem.h"
#include "sim/Simulation.h"
#include <cstddef>
#include <fstream>
#include <string>

namespace app {
// Streams per-sample CSV statistics. Write/flush failures are reported to the caller so
// the process can exit with the I/O error code instead of silently truncating the file.
class StatsWriter {
public:
    StatsWriter(const std::string& path, const sim::Simulation& world);
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    // Returns false when the row could not be written and flushed.
    bool write(const sim::Simulation& world);
private:
    std::string path_;
    std::string error_;
    std::ofstream out_;
    std::size_t geneCount_ = 0;
    bool ok_ = false;
};
} // namespace app

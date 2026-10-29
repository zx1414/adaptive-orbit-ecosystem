#pragma once
#include "core/Config.h"
#include <string>

namespace app {
struct CliOptions {
    core::SimConfig config{};
    bool render = true;
    int renderInterval = 500;
    bool energySummary = false;
    bool bench = false;
    bool help = false;
    bool version = false;
    std::string stateHashPath;
};
CliOptions parseCli(int argc, char** argv);
std::string usage();
} // namespace app

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
    std::string statsPath;
    std::string savePath;
    std::string loadPath;
    std::string htmlPath;
    bool ticksProvided = false;
    int htmlInterval = 100;
    int statsInterval = 100;
};
CliOptions parseCli(int argc, char** argv);
std::string usage();
} // namespace app
#pragma once
#include "core/Config.h"
#include <string>

namespace app {
struct CliOptions {
    core::SimConfig config{};
    // The colony model needs these to be non-zero to simulate anything; the defaults live
    // here rather than in SimConfig so the unit tests can build minimal worlds.
    bool smallDefaults = false;
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
    std::string traceJsonPath;
    bool ticksProvided = false;
    int htmlInterval = 100;
    int traceJsonInterval = 100;
    int statsInterval = 100;
};
CliOptions parseCli(int argc, char** argv);
std::string usage();
} // namespace app
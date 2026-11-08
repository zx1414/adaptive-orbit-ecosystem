// agentsim - app/Cli.h : command line options and parsing entry.
#pragma once

#include <string>

#include "core/Config.h"

namespace agentsim {

struct CliOptions {
    SimConfig config{};
    bool stats = false;
    std::string statsPath;        // csv path; default build/stats.csv when --stats given
    int statsInterval = 100;
    bool trace = false;           // --trace <path>: export JSON trajectory
    std::string tracePath;        // required value of --trace
    int traceInterval = 1;        // --trace-interval N (N > 0): frames downsample
    // --trace-genome：每帧额外导出每个核的全部 24 个基因 + hue（键 "g"）。
    // 体积大（每核 25 个数），只用于生态分析（tools/analyze.mjs 的判据 3/4/6）。
    // 默认关闭：常规 trace 保持精简。
    bool traceGenome = false;
    bool help = false;
    bool version = false;
    bool render = false;
    int agentMode = 1;            // mirror of config.agent.agentMode
    bool audit = false;           // --audit：开启运行时账目核对（默认关，见 Simulation::setAuditEnabled）
    std::string policy = "rule";  // mirror of config.policy.name
    int nuclei = -1;              // mirror of config.colony.nuclei; -1 = not specified
    double thermalWThermal = -1.0; // mirror of --thermal-w-thermal; <0 = not specified
    double thermalWCaution = -1.0; // mirror of --thermal-w-caution; <0 = not specified
};

// Parses argv; on unknown/missing/invalid fills err and returns false (caller exits 2).
// F1: numeric parsing rejects ERANGE, non-finite values and (by default) leading '-'.
bool parseCli(int argc, char** argv, CliOptions& out, std::string& err);

} // namespace agentsim

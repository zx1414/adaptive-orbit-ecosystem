// agentsim - app/Cli.cpp : command line parsing (deterministic, no RNG).
// R1-B：--food / --regrow / --no-regrow 已删除（Food 实体不存在）；新增
// --cyano-nodes / --cyano-max-nodes / --sun-base / --sun-amplitude。
#include "app/Cli.h"
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>

namespace agentsim {
namespace {

// F1: unified numeric parsing guards - errno==ERANGE, non-finite values and
// leading '-' are all rejected (strtoull("-1") wraps to ULLONG_MAX, which once
// made --ticks -1 hang main forever; ERANGE once fed reserve() 68GB).
constexpr std::uint64_t kMaxNuclei = 10000;
constexpr int kMaxCyano = 100000; // R1-B：--cyano-* 上限（防 reserve 爆炸）

bool parseDouble(const char* s, double& v) {
    if (s == nullptr || *s == '\0') return false;
    if (s[0] == '-') return false; // width/height etc. never accept negatives
    errno = 0;
    char* end = nullptr;
    const double d = std::strtod(s, &end);
    if (end == s || *end != '\0') return false;
    if (errno == ERANGE) return false;
    if (!std::isfinite(d)) return false; // nan / inf rejected
    v = d;
    return true;
}

bool parseU64(const char* s, std::uint64_t& v) {
    if (s == nullptr || *s == '\0') return false;
    if (s[0] == '-') return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long u = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0') return false;
    if (errno == ERANGE) return false;
    v = static_cast<std::uint64_t>(u);
    return true;
}

bool parseInt(const char* s, int& v, bool allowNegative) {
    if (s == nullptr || *s == '\0') return false;
    if (!allowNegative && s[0] == '-') return false;
    errno = 0;
    char* end = nullptr;
    const long n = std::strtol(s, &end, 10);
    if (end == s || *end != '\0') return false;
    if (errno == ERANGE) return false;
    if (n < static_cast<long>(std::numeric_limits<int>::min()) ||
        n > static_cast<long>(std::numeric_limits<int>::max())) return false;
    v = static_cast<int>(n);
    return true;
}

} // namespace

bool parseCli(int argc, char** argv, CliOptions& out, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") { out.help = true; return true; }
        if (arg == "--version") { out.version = true; return true; }
        if (arg == "--audit") { out.audit = true; continue; }
        if (arg == "--render") { out.render = true; continue; }
        if (arg == "--no-render") { out.render = false; continue; }
        if (arg == "--width" || arg == "--height") {
            if (i + 1 >= argc) { err = "missing value for " + arg; return false; }
            const char* val = argv[++i];
            double d = 0.0;
            if (!parseDouble(val, d) || d <= 0.0) { err = "invalid value for " + arg + ": " + val; return false; }
            if (arg == "--width") out.config.world.width = d; else out.config.world.height = d;
            continue;
        }
        if (arg == "--ticks" || arg == "--seed") {
            if (i + 1 >= argc) { err = "missing value for " + arg; return false; }
            const char* val = argv[++i];
            std::uint64_t u = 0;
            if (!parseU64(val, u)) { err = "invalid value for " + arg + ": " + val; return false; }
            if (arg == "--ticks") out.config.world.maxTicks = u; else out.config.world.seed = u;
            continue;
        }
        if (arg == "--nuclei") {
            if (i + 1 >= argc) { err = "missing value for --nuclei"; return false; }
            const char* val = argv[++i];
            int n = 0;
            if (!parseInt(val, n, false) || n < 0) { err = "invalid value for --nuclei: " + std::string(val); return false; }
            if (n > static_cast<int>(kMaxNuclei)) { // ERANGE overflow guard upstream, cap here
                err = "invalid value for --nuclei: " + std::string(val) +
                      " (max " + std::to_string(kMaxNuclei) + ")";
                return false;
            }
            out.nuclei = n;
            out.config.colony.nuclei = static_cast<std::size_t>(n);
            continue;
        }
        // R1-B：蓝细菌库存（初始节点数 / 上限）。改值必须真生效（check.ps1 探针）。
        if (arg == "--world-width" || arg == "--world-height") {
        if (i + 1 >= argc) { err = std::string("missing value for ") + arg; return false; }
        const char* val = argv[++i];
        double d = 0;
        if (!parseDouble(val, d) || d < 32.0 || d > 100000.0) {
            err = std::string("invalid value for ") + arg + ": " + std::string(val); return false;
        }
        if (arg == "--world-width") out.config.world.width = d;
        else out.config.world.height = d;
        continue;
    }
    if (arg == "--cyano-nodes" || arg == "--cyano-max-nodes") {
            if (i + 1 >= argc) { err = "missing value for " + arg; return false; }
            const char* val = argv[++i];
            int n = 0;
            if (!parseInt(val, n, false) || n < 0) { err = "invalid value for " + arg + ": " + val; return false; }
            if (n > kMaxCyano) {
                err = "invalid value for " + arg + ": " + std::string(val) +
                      " (max " + std::to_string(kMaxCyano) + ")";
                return false;
            }
            if (arg == "--cyano-nodes") {
                out.config.cyanoNodeCount = static_cast<std::size_t>(n);
            } else {
                out.config.cyanoMaxNodes = static_cast<std::size_t>(n);
            }
            continue;
        }
        // R1-B：阳光参数化（SunConfig）。--sun-base 改基线、--sun-amplitude 改
        // 昼夜幅度——直接改变每帧 sunLevel，进而改变光合入账与生态轨迹。
        if (arg == "--sun-base" || arg == "--sun-amplitude") {
            if (i + 1 >= argc) { err = "missing value for " + arg; return false; }
            const char* val = argv[++i];
            double d = 0.0;
            if (!parseDouble(val, d)) { err = "invalid value for " + arg + ": " + val; return false; }
            if (arg == "--sun-base") {
                out.config.sun.baseLevel = d;
            } else {
                out.config.sun.dayAmplitude = d;
            }
            continue;
        }
        if (arg == "--policy") {
            if (i + 1 >= argc) { err = "missing value for --policy"; return false; }
            const std::string val = argv[++i];
            if (val != "rule" && val != "stub" && val != "perceptron" && val != "thermal" &&
                val != "hysteresis" && val != "foragebank" && val != "bandit" &&
                val != "riskaverse") {
                err = "invalid --policy: " + val +
                      " (expected rule|stub|perceptron|thermal|hysteresis|foragebank|bandit|riskaverse)";
                return false;
            }
            out.policy = val;
            out.config.policy.name = val; // F2: consumed by Simulation composition root
            if (val == "perceptron") {
                out.config.policy.usePerceptron = true; // composition root assembles PerceptronPolicy
            }
            if (val == "thermal") {
                out.config.policy.useThermal = true; // composition root assembles ThermalPolicy
            }
            out.config.policy.mutation = (val == "rule") ? 0.05 : 0.0;
            continue;
        }
        if (arg == "--perceptron-seed" || arg == "--perceptron-scale") {
            if (i + 1 >= argc) { err = "missing value for " + arg; return false; }
            const char* val = argv[++i];
            if (arg == "--perceptron-seed") {
                std::uint64_t u = 0;
                if (!parseU64(val, u)) { err = "invalid value for " + arg + ": " + val; return false; }
                out.config.policy.perceptronSeed = u;
            } else {
                double d = 0.0;
                if (!parseDouble(val, d)) { err = "invalid value for " + arg + ": " + val; return false; }
                out.config.policy.perceptronScale = d;
            }
            continue;
        }
        if (arg == "--thermal-pref" || arg == "--thermal-w-thermal" ||
            arg == "--thermal-w-caution") {
            if (i + 1 >= argc) { err = "missing value for " + arg; return false; }
            const char* val = argv[++i];
            double d = 0.0;
            if (!parseDouble(val, d)) { err = "invalid value for " + arg + ": " + val; return false; }
            // R4-P1：权重直接写进 config（CliOptions 的 mirror 字段曾无人读取，导致死参数）。
            if (arg == "--thermal-pref") {
                out.config.policy.thermalPreferredTemp = d;
            } else if (arg == "--thermal-w-thermal") {
                out.config.policy.thermalWThermal = d;
            } else {
                out.config.policy.thermalWCaution = d;
            }
            continue;
        }
        if (arg == "--terrain-obstacles" || arg == "--terrain-waters" ||
            arg == "--terrain-thermal") {
            if (i + 1 >= argc) { err = "missing value for " + arg; return false; }
            const char* val = argv[++i];
            int n = 0;
            if (!parseInt(val, n, false) || n < 0) { err = "invalid value for " + arg + ": " + val; return false; }
            if (arg == "--terrain-obstacles") {
                out.config.terrain.obstacleCount = static_cast<std::size_t>(n);
            } else if (arg == "--terrain-waters") {
                out.config.terrain.waterCount = static_cast<std::size_t>(n);
            } else {
                out.config.terrain.thermalZoneCount = static_cast<std::size_t>(n);
            }
            continue;
        }
        if (arg == "--agent-mode") {
            // F2: mode 0 (orbital paradigm) is NOT implemented; refuse loudly at
            // parse time instead of silently ignoring the flag.
            if (i + 1 >= argc) { err = "missing value for --agent-mode"; return false; }
            const char* val = argv[++i];
            if (!parseInt(val, out.agentMode, false)) {
                err = "invalid value for --agent-mode: " + std::string(val);
                return false;
            }
            if (out.agentMode == 0) {
                err = "unsupported --agent-mode 0: orbital paradigm not implemented yet "
                      "(only 1 = autonomous is available)";
                return false;
            }
            if (out.agentMode != 1) {
                err = "invalid value for --agent-mode: " + std::string(val) + " (expected 1)";
                return false;
            }
            out.config.agent.agentMode = out.agentMode;
            continue;
        }
        if (arg == "--trace") {
            // Required value: a path must follow; a leading '-' starts another
            // option, so it is never consumed as a path.
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                err = "missing value for --trace";
                return false;
            }
            // 纯数字 token 不作路径（红队 R7 报 P2）：`--trace 2 --trace-interval 2`
            // 原本会把 "2" 当文件名、把用户想表达的两个数字误解反，还在 cwd 里
            // 留下一个名为 "2" 的文件。这里显式拒绝，给出明确错误。
            {
                const std::string candidate = argv[i + 1];
                bool allDigits = !candidate.empty();
                for (const char ch : candidate) {
                    if (ch < '0' || ch > '9') { allDigits = false; break; }
                }
                if (allDigits) {
                    err = "invalid path for --trace: \"" + candidate +
                          "\" is a plain number, not a file path "
                          "(did you mean --trace-interval " + candidate + "?)";
                    return false;
                }
            }
            out.trace = true;
            out.tracePath = argv[++i];
            continue;
        }
        if (arg == "--trace-genome") { out.traceGenome = true; continue; }
        if (arg == "--trace-interval") {
            if (i + 1 >= argc) { err = "missing value for --trace-interval"; return false; }
            const char* val = argv[++i];
            if (!parseInt(val, out.traceInterval, false) || out.traceInterval <= 0) {
                err = "invalid value for --trace-interval: " + std::string(val);
                return false;
            }
            continue;
        }
        if (arg == "--stats") {
            // Optional value: "--stats <path>" or bare "--stats" (main.cpp then
            // falls back to build/stats.csv). A following '-' begins another
            // option, so it is never consumed as a path.
            out.stats = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                out.statsPath = argv[++i];
            }
            continue;
        }
        if (arg == "--stats-interval") {
            if (i + 1 >= argc) { err = "missing value for --stats-interval"; return false; }
            const char* val = argv[++i];
            if (!parseInt(val, out.statsInterval, false) || out.statsInterval <= 0) {
                err = "invalid value for --stats-interval: " + std::string(val);
                return false;
            }
            continue;
        }
        if (arg.rfind("--", 0) == 0) { err = "unknown option: " + arg; return false; }
        err = "unexpected positional argument: " + arg;
        return false;
    }
    return true;
}

} // namespace agentsim

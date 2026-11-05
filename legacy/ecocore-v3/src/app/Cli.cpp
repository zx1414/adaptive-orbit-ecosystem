#include "app/Cli.h"
#include "decision/PolicyRegistry.h"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace app {
namespace {
std::string nextValue(int& index, int argc, char** argv, const char* name) {
    if (index + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + name);
    return argv[++index];
}
// Rejects trailing garbage: std::stod("1junk") parses 1 and silently ignores the rest.
double parseNonNegative(int& index, int argc, char** argv, const char* name) {
    const std::string text = nextValue(index, argc, argv, name);
    if (text.empty() || text[0] == '-') throw std::runtime_error(std::string(name) + " must be non-negative");
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size()) throw std::runtime_error(std::string(name) + " is not a number");
    if (!std::isfinite(value)) throw std::runtime_error(std::string(name) + " must be finite");
    return value;
}
std::size_t parseNonNegativeSize(int& index, int argc, char** argv, const char* name) {
    const std::string text = nextValue(index, argc, argv, name);
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos || text[first] == '-') {
        throw std::runtime_error(std::string(name) + " must be non-negative");
    }
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed);
    if (consumed != text.size()) throw std::runtime_error(std::string(name) + " is not an integer");
    if (value > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error(std::string(name) + " is out of range");
    }
    return static_cast<std::size_t>(value);
}
// Intervals are strict positive integers; -1/0/junk must be parameter errors (exit 2).
int parsePositiveInt(int& index, int argc, char** argv, const char* name) {
    const std::string text = nextValue(index, argc, argv, name);
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos || text[first] == '-') {
        throw std::runtime_error(std::string(name) + " must be a positive integer");
    }
    std::size_t consumed = 0;
    const long long value = std::stoll(text, &consumed);
    if (consumed != text.size()) throw std::runtime_error(std::string(name) + " is not an integer");
    if (value <= 0 || value > std::numeric_limits<int>::max()) {
        throw std::runtime_error(std::string(name) + " must be a positive integer");
    }
    return static_cast<int>(value);
}
} // namespace

CliOptions parseCli(int argc, char** argv) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") { options.help = true; }
        else if (arg == "--version") { options.version = true; }
        else if (arg == "--width") options.config.world.width = parseNonNegative(i, argc, argv, "--width");
        else if (arg == "--height") options.config.world.height = parseNonNegative(i, argc, argv, "--height");
        else if (arg == "--seed") options.config.world.seed = static_cast<std::uint64_t>(parseNonNegativeSize(i, argc, argv, "--seed"));
        else if (arg == "--ticks") {
            options.config.world.maxTicks = static_cast<core::Tick>(parseNonNegativeSize(i, argc, argv, "--ticks"));
            options.ticksProvided = true;
        }
        else if (arg == "--policy") options.config.policy.rule = nextValue(i, argc, argv, "--policy");
        else if (arg == "--render") options.render = true;
        else if (arg == "--no-render") options.render = false;
        else if (arg == "--render-interval") options.renderInterval = parsePositiveInt(i, argc, argv, "--render-interval");
        else if (arg == "--cyano") options.config.cyano.initial = parseNonNegativeSize(i, argc, argv, "--cyano");
        else if (arg == "--nuclei") options.config.colony.initialNuclei = parseNonNegativeSize(i, argc, argv, "--nuclei");
        else if (arg == "--balls") options.config.colony.initialBallsPerNucleus = parseNonNegative(i, argc, argv, "--balls");
        else if (arg == "--nucleus-energy") options.config.colony.initialEnergy = parseNonNegative(i, argc, argv, "--nucleus-energy");
        else if (arg == "--deploy-cost") options.config.colony.deployCost = parseNonNegative(i, argc, argv, "--deploy-cost");
        else if (arg == "--detach-radius") options.config.colony.detachRadius = parseNonNegative(i, argc, argv, "--detach-radius");
        else if (arg == "--sense-radius") options.config.policy.senseRadius = parseNonNegative(i, argc, argv, "--sense-radius");
        else if (arg == "--mutation") options.config.policy.mutation = parseNonNegative(i, argc, argv, "--mutation");
        else if (arg == "--energy-summary") options.energySummary = true;
        else if (arg == "--audit") { /* auditing is mandatory */ }
        else if (arg == "--bench") options.bench = true;
        else if (arg == "--save") options.savePath = nextValue(i, argc, argv, "--save");
        else if (arg == "--load") options.loadPath = nextValue(i, argc, argv, "--load");
        else if (arg == "--html") options.htmlPath = nextValue(i, argc, argv, "--html");
        else if (arg == "--html-interval") options.htmlInterval = parsePositiveInt(i, argc, argv, "--html-interval");
        else if (arg == "--trace-json") options.traceJsonPath = nextValue(i, argc, argv, "--trace-json");
        else if (arg == "--trace-json-interval") options.traceJsonInterval = parsePositiveInt(i, argc, argv, "--trace-json-interval");
        else if (arg == "--state-hash") options.stateHashPath = nextValue(i, argc, argv, "--state-hash");
        else if (arg == "--stats") options.statsPath = nextValue(i, argc, argv, "--stats");
        else if (arg == "--stats-interval") options.statsInterval = parsePositiveInt(i, argc, argv, "--stats-interval");
        else throw std::runtime_error("unknown argument: " + arg);
    }
    if (!options.loadPath.empty()) {
        static const char* const configFlags[] = {
            "--width", "--height", "--seed", "--policy",
            "--cyano", "--nuclei", "--balls", "--nucleus-energy",
            "--deploy-cost", "--detach-radius", "--sense-radius", "--mutation"
        };
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            for (const char* flag : configFlags) {
                if (arg == flag) {
                    throw std::runtime_error("--load cannot be combined with simulation config overrides: " + arg);
                }
            }
        }
    }
    // Every output path truncates its target; pointing two flags at one file would silently
    // destroy the earlier output, so reject that at parse time.
    {
        const std::string* paths[] = {&options.statsPath, &options.savePath, &options.htmlPath,
                                      &options.traceJsonPath, &options.stateHashPath};
        for (std::size_t i = 0; i < std::size(paths); ++i) {
            if (paths[i]->empty()) continue;
            for (std::size_t j = i + 1; j < std::size(paths); ++j) {
                if (paths[j]->empty()) continue;
                if (*paths[i] == *paths[j]) {
                    throw std::runtime_error("output paths must be distinct: " + *paths[i]);
                }
            }
        }
    }
    if (!std::isfinite(options.config.world.width) || options.config.world.width <= 0.0 ||
        !std::isfinite(options.config.world.height) || options.config.world.height <= 0.0) {
        throw std::runtime_error("world dimensions must be positive and finite");
    }
    if (!decision::PolicyRegistry::create(options.config.policy.rule)) {
        throw std::runtime_error("unknown policy: " + options.config.policy.rule);
    }
    if (options.statsInterval <= 0) throw std::runtime_error("--stats-interval must be positive");
    if (options.htmlInterval <= 0) throw std::runtime_error("--html-interval must be positive");
    if (options.traceJsonInterval <= 0) throw std::runtime_error("--trace-json-interval must be positive");
    if (!std::isfinite(options.config.policy.senseRadius) || options.config.policy.senseRadius <= 0.0) {
        throw std::runtime_error("--sense-radius must be positive and finite");
    }
    return options;
}

std::string usage() {
    return "EcoCore M5 (colony model)\n"
           "  --width <n> --height <n> --seed <n> --ticks <n>\n"
           "  --cyano <n> --nuclei <n> --balls <n> --nucleus-energy <n>\n"
           "  --deploy-cost <n> --detach-radius <n> --policy rule|stub\n"
           "  --sense-radius <n> --mutation <n>\n"
           "  --render | --no-render --render-interval <n>\n"
           "  --energy-summary --stats <path> --stats-interval <n>\n"
           "  --save <path> --load <path>\n"
           "  --html <path> --html-interval <n> --trace-json <path> --trace-json-interval <n>\n"
           "  --state-hash <path> --bench --audit --help --version\n";
}
} // namespace app

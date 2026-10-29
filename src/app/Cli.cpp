#include "app/Cli.h"
#include "decision/PolicyRegistry.h"
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace app {
namespace {
std::string nextValue(int& index, int argc, char** argv, const char* name) {
    if (index + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + name);
    return argv[++index];
}
double parseNonNegative(int& index, int argc, char** argv, const char* name) {
    const std::string text = nextValue(index, argc, argv, name);
    if (text.empty() || text[0] == '-') throw std::runtime_error(std::string(name) + " must be non-negative");
    const double value = std::stod(text);
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
    return static_cast<std::size_t>(value);
}
}
CliOptions parseCli(int argc, char** argv) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") { options.help = true; }
        else if (arg == "--version") { options.version = true; }
        else if (arg == "--width") options.config.world.width = std::stod(nextValue(i, argc, argv, "--width"));
        else if (arg == "--height") options.config.world.height = std::stod(nextValue(i, argc, argv, "--height"));
        else if (arg == "--seed") options.config.world.seed = static_cast<std::uint64_t>(parseNonNegativeSize(i, argc, argv, "--seed"));
        else if (arg == "--ticks") options.config.world.maxTicks = static_cast<core::Tick>(parseNonNegativeSize(i, argc, argv, "--ticks"));
        else if (arg == "--producers") options.config.producer.initial = parseNonNegativeSize(i, argc, argv, "--producers");
        else if (arg == "--agents") options.config.agent.initial = parseNonNegativeSize(i, argc, argv, "--agents");
        else if (arg == "--policy") options.config.policy.rule = nextValue(i, argc, argv, "--policy");
        else if (arg == "--render") options.render = true;
        else if (arg == "--no-render") options.render = false;
        else if (arg == "--render-interval") options.renderInterval = std::stoi(nextValue(i, argc, argv, "--render-interval"));
        else if (arg == "--photo") options.config.producer.photo = parseNonNegative(i, argc, argv, "--photo");
        else if (arg == "--producer-maint") options.config.producer.maint = parseNonNegative(i, argc, argv, "--producer-maint");
        else if (arg == "--producer-repro") options.config.producer.repro = parseNonNegative(i, argc, argv, "--producer-repro");
        else if (arg == "--producer-max") options.config.producer.max = parseNonNegative(i, argc, argv, "--producer-max");
        else if (arg == "--producer-crowding") options.config.producer.crowding = parseNonNegative(i, argc, argv, "--producer-crowding");
        else if (arg == "--producer-radius") options.config.producer.interactionRadius = parseNonNegative(i, argc, argv, "--producer-radius");
        else if (arg == "--agent-metab") options.config.agent.metab = parseNonNegative(i, argc, argv, "--agent-metab");
        else if (arg == "--agent-harvest") options.config.agent.harvest = parseNonNegative(i, argc, argv, "--agent-harvest");
        else if (arg == "--agent-reserve") options.config.agent.harvestReserve = parseNonNegative(i, argc, argv, "--agent-reserve");
        else if (arg == "--agent-repro") options.config.agent.repro = parseNonNegative(i, argc, argv, "--agent-repro");
        else if (arg == "--agent-max") options.config.agent.max = parseNonNegative(i, argc, argv, "--agent-max");
        else if (arg == "--agent-lifespan") options.config.agent.lifespan = static_cast<core::Tick>(parseNonNegativeSize(i, argc, argv, "--agent-lifespan"));
        else if (arg == "--sense-radius") options.config.policy.senseRadius = parseNonNegative(i, argc, argv, "--sense-radius");
        else if (arg == "--mutation") options.config.policy.mutation = parseNonNegative(i, argc, argv, "--mutation");
        else if (arg == "--energy-summary") options.energySummary = true;
        else if (arg == "--audit") { /* auditing is mandatory */ }
        else if (arg == "--bench") options.bench = true;
        else if (arg == "--state-hash") options.stateHashPath = nextValue(i, argc, argv, "--state-hash");
        else if (arg == "--stats") options.statsPath = nextValue(i, argc, argv, "--stats");
        else if (arg == "--stats-interval") options.statsInterval = std::stoi(nextValue(i, argc, argv, "--stats-interval"));
        else throw std::runtime_error("unknown argument: " + arg);
    }
    if (!std::isfinite(options.config.world.width) || options.config.world.width <= 0.0 ||
        !std::isfinite(options.config.world.height) || options.config.world.height <= 0.0) {
        throw std::runtime_error("world dimensions must be positive and finite");
    }
    if (!decision::PolicyRegistry::create(options.config.policy.rule)) {
        throw std::runtime_error("unknown policy: " + options.config.policy.rule);
    }
    if (options.statsInterval <= 0) throw std::runtime_error("--stats-interval must be positive");
    if (!std::isfinite(options.config.policy.senseRadius) || options.config.policy.senseRadius <= 0.0) {
        throw std::runtime_error("--sense-radius must be positive and finite");
    }
    return options;
}
std::string usage() {
    return "EcoCore M2\n"
           "  --width <n> --height <n> --seed <n> --ticks <n>\n"
           "  --producers <n> --agents <n> --policy rule|stub\n"
           "  --render | --no-render --render-interval <n>\n"
           "  --photo --producer-maint --producer-repro --producer-max\n"
           "  --producer-crowding --producer-radius\n"
           "  --agent-metab --agent-harvest --agent-reserve --agent-repro --agent-max --agent-lifespan\n"
           "  --sense-radius --mutation\n"
           "  --energy-summary --stats <path> --stats-interval <n>\n"
           "  --state-hash <path> --bench --audit --help --version\n";
}
} // namespace app

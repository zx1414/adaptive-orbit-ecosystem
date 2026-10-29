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
}
CliOptions parseCli(int argc, char** argv) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") { options.help = true; }
        else if (arg == "--version") { options.version = true; }
        else if (arg == "--width") options.config.world.width = std::stod(nextValue(i, argc, argv, "--width"));
        else if (arg == "--height") options.config.world.height = std::stod(nextValue(i, argc, argv, "--height"));
        else if (arg == "--seed") options.config.world.seed = static_cast<std::uint64_t>(std::stoull(nextValue(i, argc, argv, "--seed")));
        else if (arg == "--ticks") options.config.world.maxTicks = static_cast<core::Tick>(std::stoull(nextValue(i, argc, argv, "--ticks")));
        else if (arg == "--producers") options.config.producer.initial = static_cast<std::size_t>(std::stoull(nextValue(i, argc, argv, "--producers")));
        else if (arg == "--agents") options.config.agent.initial = static_cast<std::size_t>(std::stoull(nextValue(i, argc, argv, "--agents")));
        else if (arg == "--policy") options.config.policy.rule = nextValue(i, argc, argv, "--policy");
        else if (arg == "--render") options.render = true;
        else if (arg == "--no-render") options.render = false;
        else if (arg == "--render-interval") options.renderInterval = std::stoi(nextValue(i, argc, argv, "--render-interval"));
        else if (arg == "--energy-summary") options.energySummary = true;
        else if (arg == "--audit") { /* auditing is mandatory */ }
        else if (arg == "--bench") options.bench = true;
        else if (arg == "--state-hash") options.stateHashPath = nextValue(i, argc, argv, "--state-hash");
        else throw std::runtime_error("unknown argument: " + arg);
    }
    if (!std::isfinite(options.config.world.width) || options.config.world.width <= 0.0 ||
        !std::isfinite(options.config.world.height) || options.config.world.height <= 0.0) {
        throw std::runtime_error("world dimensions must be positive and finite");
    }
    if (!decision::PolicyRegistry::create(options.config.policy.rule)) {
        throw std::runtime_error("unknown policy: " + options.config.policy.rule);
    }
    return options;
}
std::string usage() {
    return "EcoCore M1\n"
           "  --width <n> --height <n> --seed <n> --ticks <n>\n"
           "  --producers <n> --agents <n> --policy rule|stub\n"
           "  --render | --no-render --render-interval <n>\n"
           "  --energy-summary --state-hash <path> --bench --audit --help --version\n";
}
} // namespace app

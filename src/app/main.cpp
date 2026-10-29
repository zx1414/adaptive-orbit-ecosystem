#include "app/Cli.h"
#include "app/ConsoleRenderer.h"
#include "app/StatsWriter.h"
#include "sim/Simulation.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    using Clock = std::chrono::steady_clock;
    app::CliOptions options;
    try {
        options = app::parseCli(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    if (options.help) { std::cout << app::usage(); return 0; }
    if (options.version) { std::cout << "EcoCore M2\n"; return 0; }
    try {
        sim::Simulation world(options.config);
        app::ConsoleRenderer renderer;
        std::unique_ptr<app::StatsWriter> statsWriter;
        if (!options.statsPath.empty()) {
            statsWriter = std::make_unique<app::StatsWriter>(options.statsPath, world);
            if (!statsWriter->ok()) {
                std::cerr << "cannot write stats: " << options.statsPath << '\n';
                return 4;
            }
        }
        const auto start = Clock::now();
        core::Tick lastStatsTick = std::numeric_limits<core::Tick>::max();
        while (!world.finished()) {
            if (statsWriter && world.tick() % static_cast<core::Tick>(options.statsInterval) == 0 &&
                world.tick() != lastStatsTick) {
                statsWriter->write(world);
                lastStatsTick = world.tick();
            }
            if (options.render && options.renderInterval > 0 &&
                world.tick() % static_cast<core::Tick>(options.renderInterval) == 0) {
                renderer.render(world.view(), std::cout);
            }
            world.step();
        }
        if (statsWriter && world.tick() != lastStatsTick) statsWriter->write(world);
        const auto end = Clock::now();
        const auto& stats = world.view().stats;
        std::cout << std::setprecision(10)
                  << "done tick=" << stats.tick
                  << " producers=" << stats.producers
                  << " agents=" << stats.agents
                  << " total_energy=" << stats.totalEnergy
                  << " injected=" << stats.injected
                  << " dissipated=" << stats.dissipated
                  << " transferred=" << stats.transferred
                  << " audit=" << (stats.auditOk ? "ok" : "FAIL")
                  << " policy=" << options.config.policy.rule
                  << " reason=" << (stats.finishReason.empty() ? "running" : stats.finishReason)
                  << '\n';
        if (options.energySummary) {
            std::cout << "energy summary:\n";
            for (std::size_t i = 0; i < world::kEnergyReasonCount; ++i) {
                const auto reason = static_cast<world::EnergyReason>(i);
                const double in = world.ledger().totalIn(reason);
                const double out = world.ledger().totalOut(reason);
                if (in == 0.0 && out == 0.0) continue;
                std::cout << "  " << world::energyReasonName(reason)
                          << " in=" << in << " out=" << out << '\n';
            }
        }
        if (!options.stateHashPath.empty()) {
            std::ofstream file(options.stateHashPath);
            if (!file) {
                std::cerr << "cannot write state hash: " << options.stateHashPath << '\n';
                return 4;
            }
            file << std::hex << world.stateHash() << '\n';
            if (!file) {
                std::cerr << "failed while writing state hash: " << options.stateHashPath << '\n';
                return 4;
            }
        }
        if (options.bench) {
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::cout << "bench_ms=" << milliseconds << '\n';
        }
        return stats.auditOk ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}

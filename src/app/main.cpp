#include "app/Cli.h"
#include "app/ConsoleRenderer.h"
#include "app/HtmlTraceWriter.h"
#include "app/TraceJsonWriter.h"
#include "app/StatsWriter.h"
#include "sim/Simulation.h"
#include "sim/SnapshotStore.h"
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
    if (options.version) { std::cout << "EcoCore M4\n"; return 0; }
    try {
        std::unique_ptr<sim::Simulation> worldPtr;
        if (!options.loadPath.empty()) {
            std::string snapshotError;
            worldPtr = sim::SnapshotStore::load(options.loadPath, options.ticksProvided,
                                                options.config.world.maxTicks, snapshotError);
            if (!worldPtr) {
                std::cerr << "cannot load snapshot: " << snapshotError << '\n';
                return 5;
            }
        } else {
            worldPtr = std::make_unique<sim::Simulation>(options.config);
        }
        sim::Simulation& world = *worldPtr;
        app::ConsoleRenderer renderer;
        std::unique_ptr<app::StatsWriter> statsWriter;
        std::unique_ptr<app::HtmlTraceWriter> htmlWriter;
        if (!options.htmlPath.empty()) {
            htmlWriter = std::make_unique<app::HtmlTraceWriter>(options.htmlPath);
            if (!htmlWriter->ok()) {
                std::cerr << "cannot write html trace: " << htmlWriter->error() << '\n';
                return 4;
            }
        }
        std::unique_ptr<app::TraceJsonWriter> traceJsonWriter;
        if (!options.traceJsonPath.empty()) {
            std::string metaJson = "{\"seed\":" + std::to_string(world.config().world.seed) +
                ",\"maxTicks\":" + std::to_string(world.config().world.maxTicks) + "}";
            traceJsonWriter = std::make_unique<app::TraceJsonWriter>(
                options.traceJsonPath, "EcoCore trace", metaJson);
            if (!traceJsonWriter->ok()) {
                std::cerr << "cannot write trace json: " << traceJsonWriter->error() << '\n';
                return 4;
            }
        }
        if (!options.statsPath.empty()) {
            statsWriter = std::make_unique<app::StatsWriter>(options.statsPath, world);
            if (!statsWriter->ok()) {
                std::cerr << "cannot write stats: " << options.statsPath << '\n';
                return 4;
            }
        }
        const auto start = Clock::now();
        core::Tick lastStatsTick = std::numeric_limits<core::Tick>::max();
        core::Tick lastHtmlTick = std::numeric_limits<core::Tick>::max();
        core::Tick lastTraceJsonTick = std::numeric_limits<core::Tick>::max();
        if (statsWriter && !options.loadPath.empty()) {
            statsWriter->write(world);
            lastStatsTick = world.tick();
        }
        while (!world.finished()) {
            if (statsWriter && world.tick() % static_cast<core::Tick>(options.statsInterval) == 0 &&
                world.tick() != lastStatsTick) {
                statsWriter->write(world);
                lastStatsTick = world.tick();
            }
            if (htmlWriter && world.tick() % static_cast<core::Tick>(options.htmlInterval) == 0 &&
                world.tick() != lastHtmlTick) {
                htmlWriter->capture(world.view());
                lastHtmlTick = world.tick();
            }
            if (traceJsonWriter && world.tick() % static_cast<core::Tick>(options.traceJsonInterval) == 0 &&
                world.tick() != lastTraceJsonTick) {
                traceJsonWriter->capture(world.view());
                lastTraceJsonTick = world.tick();
            }
            if (options.render && options.renderInterval > 0 &&
                world.tick() % static_cast<core::Tick>(options.renderInterval) == 0) {
                renderer.render(world.view(), std::cout);
            }
            world.step();
        }
        if (statsWriter && world.tick() != lastStatsTick) statsWriter->write(world);
        if (htmlWriter && world.tick() != lastHtmlTick) htmlWriter->capture(world.view());
        if (traceJsonWriter && world.tick() != lastTraceJsonTick) traceJsonWriter->capture(world.view());
        if (!options.savePath.empty()) {
            std::string snapshotError;
            if (!sim::SnapshotStore::save(world, options.savePath, snapshotError)) {
                std::cerr << "cannot save snapshot: " << snapshotError << '\n';
                return 4;
            }
        }
        if (htmlWriter) {
            if (!htmlWriter->write()) {
                std::cerr << "cannot write html trace: " << htmlWriter->error() << '\n';
                return 4;
            }
            std::cout << "html_frames=" << htmlWriter->frameCount()
                      << " downsampled=" << (htmlWriter->downsampled() ? 1 : 0) << '\n';
        }
        if (traceJsonWriter) {
            if (!traceJsonWriter->write()) {
                std::cerr << "cannot write trace json: " << traceJsonWriter->error() << '\n';
                return 4;
            }
            std::cout << "trace_json_frames=" << traceJsonWriter->frameCount()
                      << " downsampled=" << (traceJsonWriter->downsampled() ? 1 : 0) << '\n';
        }
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
                  << " policy=" << world.config().policy.rule
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
#include "app/StatsWriter.h"
#include "decision/PolicyRegistry.h"
#include <iomanip>
#include <vector>

namespace app {
StatsWriter::StatsWriter(const std::string& path, const sim::Simulation& world) : path_(path) {
    out_.open(path);
    if (!out_) {
        error_ = "cannot open stats file: " + path_;
        return;
    }
    const auto policy = decision::PolicyRegistry::create(world.config().policy.rule);
    geneCount_ = policy ? policy->spec().genomeDim : world.meanGenome().size();
    out_ << "tick,producers,agents,total_energy,injected,dissipated,transferred,"
            "mean_producer_energy,mean_agent_energy,"
            "producer_births,producer_deaths,agent_births,agent_deaths";
    for (std::size_t i = 0; i < geneCount_; ++i) out_ << ",gene_" << i;
    out_ << ",audit_ok\n";
    out_.flush();
    if (!out_) {
        error_ = "failed while writing stats header: " + path_;
        return;
    }
    ok_ = true;
}

bool StatsWriter::write(const sim::Simulation& world) {
    if (!ok_) return false;
    const auto& stats = world.view().stats;
    out_ << std::setprecision(10) << stats.tick << ',' << stats.producers << ',' << stats.agents
         << ',' << stats.totalEnergy << ',' << stats.injected << ',' << stats.dissipated
         << ',' << stats.transferred << ',' << world.meanProducerEnergy()
         << ',' << world.meanAgentEnergy() << ',' << world.producerSystem().births()
         << ',' << world.producerSystem().deaths() << ',' << world.agentSystem().births()
         << ',' << world.agentSystem().deaths();
    const std::vector<double> genes = world.meanGenome();
    for (std::size_t i = 0; i < geneCount_; ++i) {
        out_ << ',' << (i < genes.size() ? genes[i] : 0.0);
    }
    out_ << ',' << (stats.auditOk ? 1 : 0) << '\n';
    out_.flush();
    if (!out_) {
        // Never report success for a truncated CSV: the caller must exit with the I/O code.
        error_ = "failed while writing stats row: " + path_;
        ok_ = false;
        return false;
    }
    return true;
}
} // namespace app

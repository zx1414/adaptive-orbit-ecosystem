#include "app/StatsWriter.h"
#include <iomanip>

namespace app {
namespace {
// Mean value of one heritable nucleus parameter across living nuclei.
double meanParam(const sim::Simulation& world, double colony::NucleusParams::*member) {
    double sum = 0.0;
    std::size_t count = 0;
    for (const auto& colony : world.colonies().colonies()) {
        if (!colony.nucleus.alive()) continue;
        sum += colony.nucleus.params.*member;
        ++count;
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}
} // namespace

StatsWriter::StatsWriter(const std::string& path, const sim::Simulation& world) : path_(path) {
    (void)world;  // the header is fixed; rows are written per tick
    out_.open(path);
    if (!out_) {
        error_ = "cannot open stats file: " + path_;
        return;
    }
    out_ << "tick,cyanobacteria,nuclei,balls,total_energy,injected,dissipated,"
            "transferred,mean_nucleus_energy,mean_orbit_radius,mean_affinity,"
            "mean_max_speed,mean_spot_radius,audit_ok\n";
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
    double nucleusEnergy = 0.0;
    std::size_t nuclei = 0;
    for (const auto& colony : world.colonies().colonies()) {
        if (!colony.nucleus.alive()) continue;
        nucleusEnergy += colony.nucleus.energy.energy;
        ++nuclei;
    }
    const double meanEnergy = nuclei > 0 ? nucleusEnergy / static_cast<double>(nuclei) : 0.0;
    out_ << std::setprecision(10) << stats.tick << ',' << stats.cyanobacteria << ','
         << stats.nuclei << ',' << stats.balls << ',' << stats.totalEnergy << ','
         << stats.injected << ',' << stats.dissipated << ',' << stats.transferred << ','
         << meanEnergy << ',' << meanParam(world, &colony::NucleusParams::orbitRadius) << ','
         << meanParam(world, &colony::NucleusParams::affinity) << ','
         << meanParam(world, &colony::NucleusParams::maxSpeed) << ','
         << meanParam(world, &colony::NucleusParams::spotRadius) << ','
         << (stats.auditOk ? 1 : 0) << '\n';
    out_.flush();
    if (!out_) {
        error_ = "failed while writing stats row: " + path_;
        ok_ = false;
        return false;
    }
    return true;
}
} // namespace app

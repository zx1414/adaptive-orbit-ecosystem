#include "Sampler.h"
#include <fstream>
#include <iomanip>
#include <sstream>

Sampler::Sampler(std::string dir) : dir_(std::move(dir)) {}

namespace {
std::string padFrame(int frame) {
    std::ostringstream ss;
    ss << std::setw(6) << std::setfill('0') << frame;
    return ss.str();
}

const char* trendHeader() {
    return "frame,nucleus_id,energy,affinity0,affinity1,affinity2,orbitRadius0,orbitRadius1,orbitRadius2,"
           "absorbPreference0,absorbPreference1,absorbPreference2,repelStrength0,repelStrength1,"
           "repelStrength2,attackRange,attackStrength,avoidRange,avoidStrength,maxSpeed,"
           "energyThreshold,mutationRate";
}

void writeTrendRow(std::ostream& os, int frame, const Nucleus& n) {
    const NucleusParams& p = n.params;
    os << std::fixed << std::setprecision(6);
    os << frame << ',' << n.id << ',' << n.energy << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.affinity[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.orbitRadius[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.absorbPreference[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT - 1; ++i) os << p.repelStrength[i] << ',';
    os << p.repelStrength[BALL_TYPE_COUNT - 1] << ',';
    os << p.attackRange << ',' << p.attackStrength << ',' << p.avoidRange << ','
       << p.avoidStrength << ',' << p.maxSpeed << ',' << p.energyThreshold << ','
       << p.mutationRate << '\n';
}

const char* nucleiHeader() {
    return "x,y,energy,affinity0,affinity1,affinity2,orbitRadius0,orbitRadius1,orbitRadius2,"
           "absorbPreference0,absorbPreference1,absorbPreference2,repelStrength0,repelStrength1,"
           "repelStrength2,attackRange,attackStrength,avoidRange,avoidStrength,maxSpeed,"
           "energyThreshold,mutationRate";
}

void writeNucleusRow(std::ostream& os, const Nucleus& n) {
    const NucleusParams& p = n.params;
    os << std::fixed << std::setprecision(6);
    os << n.pos.x << ',' << n.pos.y << ',' << n.energy << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.affinity[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.orbitRadius[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.absorbPreference[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT - 1; ++i) os << p.repelStrength[i] << ',';
    os << p.repelStrength[BALL_TYPE_COUNT - 1] << ',';
    os << p.attackRange << ',' << p.attackStrength << ',' << p.avoidRange << ','
       << p.avoidStrength << ',' << p.maxSpeed << ',' << p.energyThreshold << ','
       << p.mutationRate << '\n';
}
}  // namespace

void Sampler::sample(const World& world) {
    std::string padded = padFrame(world.frame());

    {
        std::ofstream f(dir_ + "/balls_" + padded + ".csv");
        f << "x,y,type,ownerId\n";
        f << std::fixed << std::setprecision(6);
        for (const Ball& b : world.balls()) {
            f << b.pos.x << ',' << b.pos.y << ','
              << static_cast<int>(b.type) << ',' << b.ownerId << '\n';
        }
    }

    {
        std::ofstream f(dir_ + "/nuclei_" + padded + ".csv");
        f << nucleiHeader() << '\n';
        for (const Nucleus& n : world.nuclei()) {
            if (!n.alive) continue;
            writeNucleusRow(f, n);
        }
    }
}

void Sampler::writeSurvivors(const World& world) {
    std::ofstream f(dir_ + "/survivors.csv");
    f << nucleiHeader() << '\n';
    for (const Nucleus& n : world.nuclei()) {
        if (n.alive) writeNucleusRow(f, n);
    }
}

bool Sampler::openTrend(const std::string& path) {
    trendOut_.open(path);
    if (!trendOut_.is_open()) {
        return false;
    }
    trendOut_ << trendHeader() << '\n';
    return true;
}

void Sampler::sampleTrend(const World& world) {
    if (!trendOut_.is_open()) return;
    int frame = world.frame();
    for (const Nucleus& n : world.nuclei()) {
        if (n.alive) writeTrendRow(trendOut_, frame, n);
    }
}

void Sampler::closeTrend() {
    if (trendOut_.is_open()) trendOut_.close();
}

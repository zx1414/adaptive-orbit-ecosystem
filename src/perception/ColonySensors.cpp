#include "perception/ColonySensors.h"

#include "core/Math.h"

#include <algorithm>
#include <cmath>

namespace agentsim {
namespace {
constexpr double kTwoPi = 6.28318530717958647692;

float finiteClamp(float value, float lo, float hi) {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : 0.0f;
}

// One sensed candidate, already decayed and noised.
struct Report {
    core::Vec2 delta{};
    double distance = 0.0;
    double strength = 0.0;
    double energy = 0.0;
};

// Reads a target at distance d from a scout. Signal decays linearly to zero at spotRadius;
// noise is proportional to the noise floor and inversely to signal, so weak, distant readings
// are the unreliable ones.
double sampleReading(double value, double strength, double noiseSigma, core::Random& random) {
    if (!(strength > 0.0)) return 0.0;
    const double noisy = random.normal(0.0, noiseSigma > 0.0 ? noiseSigma : 0.0);
    return value * strength * (1.0 + noisy);
}

void writeSlot(decision::Observation& observation, int base,
               const std::vector<Report>& reports, double senseRadius) {
    // Empty slots use -1 distance so a real target at distance 0 is not mistaken for empty.
    observation[base + 2] = -1.0f;
    if (reports.empty()) return;
    const Report& report = reports.front();
    const double scale = senseRadius > 0.0 ? senseRadius : 1.0;
    observation[base + 0] = finiteClamp(static_cast<float>(report.delta.x / scale), -1.0f, 1.0f);
    observation[base + 1] = finiteClamp(static_cast<float>(report.delta.y / scale), -1.0f, 1.0f);
    observation[base + 2] = finiteClamp(static_cast<float>(report.distance / scale), 0.0f, 1.0f);
    observation[base + 3] = finiteClamp(static_cast<float>(report.energy), 0.0f, 1.0f);
}
} // namespace

decision::Observation buildColonyObservation(const colony::Colony& self,
                                             const colony::ColonySystem& colonies,
                                             const environment::Cyanobacteria& cyano,
                                             core::Bounds bounds, core::Tick tick,
                                             const core::LightConfig& light,
                                             core::Random& sensorRandom) {
    decision::Observation observation;
    const colony::Nucleus& nucleus = self.nucleus;
    const double spotRadius = std::max(1e-9, nucleus.params.spotRadius);
    const double nativeCap = std::max(1e-9, colony::colonyCapFor(colonies, nucleus));
    const double maxSpeed = std::max(1e-9, nucleus.params.maxSpeed);

    // Internal channels: always available, no scout required.
    observation[decision::obs::EnergyRatio] =
        finiteClamp(static_cast<float>(nucleus.energy.energy / nativeCap), 0.0f, 1.0f);
    observation[decision::obs::StorageRatio] =
        finiteClamp(static_cast<float>(colonies.storedEnergyFor(nucleus) / nativeCap), 0.0f, 1.0f);
    observation[decision::obs::SpeedRatio] =
        finiteClamp(static_cast<float>(nucleus.vel.length() / maxSpeed), 0.0f, 1.0f);
    observation[decision::obs::EdgeLowX] =
        finiteClamp(static_cast<float>((nucleus.pos.x - bounds.minX) / spotRadius), 0.0f, 1.0f);
    observation[decision::obs::EdgeHighX] =
        finiteClamp(static_cast<float>((bounds.maxX - nucleus.pos.x) / spotRadius), 0.0f, 1.0f);
    observation[decision::obs::EdgeLowY] =
        finiteClamp(static_cast<float>((nucleus.pos.y - bounds.minY) / spotRadius), 0.0f, 1.0f);
    observation[decision::obs::EdgeHighY] =
        finiteClamp(static_cast<float>((bounds.maxY - nucleus.pos.y) / spotRadius), 0.0f, 1.0f);

    // Organ inventory.
    std::size_t resources = 0;
    std::size_t scouts = 0;
    std::size_t shields = 0;
    for (const std::size_t index : self.balls) {
        if (index >= colonies.balls().size()) continue;
        const colony::Ball& ball = colonies.balls()[index];
        if (!ball.alive()) continue;
        switch (ball.type) {
            case colony::BallType::Resource: ++resources; break;
            case colony::BallType::Scout: ++scouts; break;
            case colony::BallType::Shield: ++shields; break;
        }
    }
    observation[decision::obs::ResourceBalls] =
        finiteClamp(static_cast<float>(resources) / 4.0f, 0.0f, 1.0f);
    observation[decision::obs::ScoutBalls] =
        finiteClamp(static_cast<float>(scouts) / 4.0f, 0.0f, 1.0f);
    observation[decision::obs::ShieldBalls] =
        finiteClamp(static_cast<float>(shields) / 4.0f, 0.0f, 1.0f);

    // External channels: only scouts grant them.
    observation[decision::obs::Prey0 + 2] = -1.0f;
    observation[decision::obs::Prey1 + 2] = -1.0f;
    observation[decision::obs::Rival0 + 2] = -1.0f;
    observation[decision::obs::Rival1 + 2] = -1.0f;
    if (scouts == 0) {
        // Blind: no prey or rival information at all.
        return observation;
    }

    // Gather scout positions; each report is decayed and noised per scout.
    std::vector<core::Vec2> scoutPositions;
    scoutPositions.reserve(scouts);
    for (const std::size_t index : self.balls) {
        if (index >= colonies.balls().size()) continue;
        const colony::Ball& ball = colonies.balls()[index];
        if (!ball.alive() || ball.type != colony::BallType::Scout) continue;
        scoutPositions.push_back(ball.pos);
    }

    // Noise floor shrinks as more scouts report, so investing in sensing pays off.
    const double noiseSigma = 0.25 / std::sqrt(static_cast<double>(scouts));

    std::vector<environment::CyanoSample> preySamples;
    std::vector<Report> prey;
    for (const core::Vec2& scout : scoutPositions) {
        cyano.querySamples(scout, spotRadius, preySamples);
        for (const auto& sample : preySamples) {
            const double distance = (sample.position - scout).length();
            const double strength = std::max(0.0, 1.0 - distance / spotRadius);
            if (!(strength > 0.0)) continue;
            Report report;
            report.delta = sample.position - nucleus.pos;
            report.distance = report.delta.length();
            report.strength = strength;
            const double cap = std::max(1e-9, sample.cap);
            report.energy = sampleReading(sample.energy / cap, strength, noiseSigma, sensorRandom);
            prey.push_back(report);
        }
    }
    std::sort(prey.begin(), prey.end(), [](const Report& a, const Report& b) {
        return a.distance < b.distance;
    });
    if (prey.size() > 2) prey.resize(2);
    writeSlot(observation, decision::obs::Prey0, prey, spotRadius);
    if (prey.size() > 1) {
        std::vector<Report> second{prey[1]};
        writeSlot(observation, decision::obs::Prey1, second, spotRadius);
    }

    std::vector<Report> rivals;
    for (const colony::Colony& other : colonies.colonies()) {
        if (other.nucleus.id == nucleus.id) continue;
        if (!other.nucleus.alive()) continue;
        for (const core::Vec2& scout : scoutPositions) {
            const double distance = (other.nucleus.pos - scout).length();
            const double strength = std::max(0.0, 1.0 - distance / spotRadius);
            if (!(strength > 0.0)) continue;
            Report report;
            report.delta = other.nucleus.pos - nucleus.pos;
            report.distance = report.delta.length();
            report.strength = strength;
            const double relative =
                (other.nucleus.energy.energy - nucleus.energy.energy) / nativeCap;
            report.energy = sampleReading(relative, strength, noiseSigma, sensorRandom);
            rivals.push_back(report);
            break;  // one report per rival is enough
        }
    }
    std::sort(rivals.begin(), rivals.end(), [](const Report& a, const Report& b) {
        return a.distance < b.distance;
    });
    if (rivals.size() > 2) rivals.resize(2);
    writeSlot(observation, decision::obs::Rival0, rivals, spotRadius);
    if (rivals.size() > 1) {
        std::vector<Report> second{rivals[1]};
        writeSlot(observation, decision::obs::Rival1, second, spotRadius);
    }

    for (float& value : observation.values) {
        if (!std::isfinite(value)) value = 0.0f;
    }
    (void)tick;
    (void)light;
    (void)kTwoPi;
    return observation;
}

} // namespace agentsim

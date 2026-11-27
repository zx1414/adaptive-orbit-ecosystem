#include "app/ResearchEpisode.h"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <istream>
#include <locale>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>

#include "sim/Simulation.h"

namespace agentsim::app {
namespace {

void writeFrame(std::ostream& out, const Simulation& sim,
                const std::vector<AgentObservation>& observations,
                const std::vector<AgentOutcome>& outcomes, bool terminated, bool truncated) {
    const auto matter = sim.matter();
    out << "{\"version\":1,\"tick\":" << sim.tick()
        << ",\"terminated\":" << std::boolalpha << terminated
        << ",\"truncated\":" << truncated << ",\"audit_ok\":" << sim.auditOk()
        << ",\"world\":{\"nuclei\":" << sim.nucleusCount()
        << ",\"cyano\":" << (sim.cyano().aliveCount() - sim.cyano().sporeCount())
        << ",\"spores\":" << sim.cyano().sporeCount() << ",\"energy\":" << sim.totalEnergy()
        << ",\"nutrient\":" << matter.nutrient << ",\"debris\":" << matter.debris
        << ",\"injected\":" << sim.ledger().totalInjected()
        << ",\"dissipated\":" << sim.ledger().totalDissipated()
        << ",\"matter_residual\":" << (matter.total() - matter.externalIn) << "},\"observations\":[";
    bool first = true;
    for (const auto& observation : observations) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":\"" << observation.id << "\",\"energy\":" << observation.energy
            << ",\"sensors\":[";
        for (int i = 0; i < SensorVector::kDim; ++i) {
            if (i != 0) out << ',';
            out << observation.sensors.v[i];
        }
        out << "],\"food_ids\":[";
        for (std::size_t i = 0; i < observation.sensors.foodIds.size(); ++i) {
            if (i != 0) out << ',';
            const auto id = observation.sensors.foodIds[i];
            if (id == 0) out << "null";
            else out << '"' << id << '"';
        }
        out << "],\"food_distances\":[";
        for (std::size_t i = 0; i < observation.sensors.foodDistance.size(); ++i) {
            if (i != 0) out << ',';
            out << observation.sensors.foodDistance[i];
        }
        out << "],\"food_is_spore\":[";
        for (std::size_t i = 0; i < observation.sensors.foodIsSpore.size(); ++i) {
            if (i != 0) out << ',';
            out << observation.sensors.foodIsSpore[i];
        }
        out << "],\"bite_radius\":" << observation.sensors.biteRadius << '}';
    }
    out << "],\"outcomes\":[";
    first = true;
    for (const auto& outcome : outcomes) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":\"" << outcome.id << "\",\"energy_before\":" << outcome.energyBefore
            << ",\"energy_after\":" << outcome.energyAfter << ",\"alive\":" << outcome.alive
            << ",\"material_taken\":" << outcome.materialTaken
            << ",\"energy_absorbed\":" << outcome.energyAbsorbed << '}';
    }
    out << "],\"birth_ids\":[";
    if (sim.tick() != 0) {
        std::set<EntityId> previous;
        for (const auto& outcome : outcomes) previous.insert(outcome.id);
        first = true;
        for (const Nucleus& nucleus : sim.nuclei()) {
            if (!nucleus.alive || previous.count(nucleus.id) != 0) continue;
            if (!first) out << ',';
            first = false;
            out << '"' << nucleus.id << '"';
        }
    }
    out << "]}\n" << std::flush;
}

bool readActions(const std::string& line, const std::vector<AgentObservation>& observations,
                 std::vector<ActionVector>& actions) {
    std::istringstream in(line);
    in.imbue(std::locale::classic());
    std::string command;
    std::size_t count = 0;
    if (!(in >> command >> count) || command != "step" || count > observations.size()) return false;
    std::map<EntityId, std::size_t> indices;
    for (std::size_t i = 0; i < observations.size(); ++i) indices.emplace(observations[i].id, i);
    actions.assign(observations.size(), {});
    for (std::size_t i = 0; i < count; ++i) {
        std::string id;
        ActionVector action;
        if (!(in >> id >> action.dirX >> action.dirY >> action.speedScale >> action.boost) ||
            !(std::abs(action.dirX) <= 1.0 && std::abs(action.dirY) <= 1.0 &&
              action.speedScale >= 0.0 && action.speedScale <= 1.0 &&
              action.boost >= 0.0 && action.boost <= 1.0)) return false;
        EntityId entity = 0;
        const auto parsed = std::from_chars(id.data(), id.data() + id.size(), entity);
        if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size()) return false;
        const auto index = indices.find(entity);
        if (index == indices.end()) return false;
        actions[index->second] = action;
        indices.erase(index);
    }
    in >> std::ws;
    return in.eof();
}

} // namespace

int runResearchEpisode(const SimConfig& config, bool audit, std::istream& in, std::ostream& out) {
    Simulation sim(config);
    sim.setAuditEnabled(audit);
    out.imbue(std::locale::classic());
    out << std::setprecision(17);
    std::vector<AgentOutcome> outcomes;
    const Simulation::DecideBatch decide = [&](Tick, const std::vector<AgentObservation>& observations,
                                                std::vector<ActionVector>& actions) {
        writeFrame(out, sim, observations, outcomes, false, false);
        for (std::string line; std::getline(in, line);) {
            if (line == "close") return false;
            if (line.size() <= 1048576 && readActions(line, observations, actions)) return true;
            out << "{\"error\":\"invalid action batch\"}\n" << std::flush;
        }
        return false;
    };
    while (sim.nucleusCount() != 0 && sim.tick() < config.world.maxTicks && sim.auditOk()) {
        if (!sim.stepWithActions(decide, outcomes)) return 0;
    }
    writeFrame(out, sim, {}, outcomes, sim.nucleusCount() == 0,
               sim.nucleusCount() != 0 && sim.tick() >= config.world.maxTicks);
    return sim.auditOk() ? 0 : 3;
}

} // namespace agentsim::app

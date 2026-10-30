#include "world/Energy.h"
#include "core/Math.h"
#include <algorithm>

namespace world {
const char* energyReasonName(EnergyReason reason) {
    switch (reason) {
        case EnergyReason::InitialEndowment: return "INITIAL_ENDOWMENT";
        case EnergyReason::Photosynthesis: return "PHOTOSYNTHESIS";
        case EnergyReason::ProducerMaintenance: return "PRODUCER_MAINTENANCE";
        case EnergyReason::ProducerReproduction: return "PRODUCER_REPRODUCTION";
        case EnergyReason::AgentMetabolism: return "AGENT_METABOLISM";
        case EnergyReason::AgentMove: return "AGENT_MOVE";
        case EnergyReason::AgentHarvest: return "AGENT_HARVEST";
        case EnergyReason::AgentReproduction: return "AGENT_REPRODUCTION";
        case EnergyReason::AgentDeath: return "AGENT_DEATH";
        case EnergyReason::ProducerDeath: return "PRODUCER_DEATH";
        case EnergyReason::CyanoPhotosynthesis: return "CYANO_PHOTOSYNTHESIS";
        case EnergyReason::CyanoMetabolism: return "CYANO_METABOLISM";
        case EnergyReason::CyanoTransfer: return "CYANO_TRANSFER";
        case EnergyReason::CyanoReproduction: return "CYANO_REPRODUCTION";
        case EnergyReason::CyanoDeath: return "CYANO_DEATH";
        case EnergyReason::Count: break;
    }
    return "UNKNOWN";
}
bool EnergyLedger::validAmount(double amount) {
    return std::isfinite(amount) && amount > 0.0;
}
double EnergyLedger::headroom(const core::EnergyState& state) {
    if (!state.alive || !std::isfinite(state.energy) || !std::isfinite(state.cap)) return 0.0;
    return std::max(0.0, state.cap - state.energy);
}
double EnergyLedger::injectInitial(core::EnergyState& state, core::EntityId, double amount) {
    if (initialSealed_ || !validAmount(amount) || !state.alive) return 0.0;
    const double actual = std::min(amount, headroom(state));
    if (actual <= 0.0) return 0.0;
    state.energy += actual;
    stats_.reasons[static_cast<std::size_t>(EnergyReason::InitialEndowment)].in += actual;
    stats_.injected += actual;
    return actual;
}
double EnergyLedger::injectPrimary(core::EnergyState& state, core::EntityId, double amount) {
    if (!validAmount(amount) || !state.alive) return 0.0;
    const double actual = std::min(amount, headroom(state));
    if (actual <= 0.0) return 0.0;
    state.energy += actual;
    stats_.reasons[static_cast<std::size_t>(EnergyReason::Photosynthesis)].in += actual;
    stats_.injected += actual;
    return actual;
}
double EnergyLedger::transfer(core::EnergyState& from, core::EnergyState& to, double amount, EnergyReason reason) {
    if (&from == &to || !validAmount(amount) || !from.alive || !to.alive) return 0.0;
    const double actual = std::min({amount, std::max(0.0, from.energy), headroom(to)});
    if (actual <= 0.0) return 0.0;
    from.energy -= actual;
    to.energy += actual;
    stats_.reasons[static_cast<std::size_t>(reason)].out += actual;
    stats_.reasons[static_cast<std::size_t>(reason)].in += actual;
    stats_.transferred += actual;
    return actual;
}
double EnergyLedger::dissipate(core::EnergyState& state, core::EntityId, double amount, EnergyReason reason) {
    if (!validAmount(amount) || !state.alive || !std::isfinite(state.energy)) return 0.0;
    const double actual = std::min(amount, std::max(0.0, state.energy));
    if (actual <= 0.0) return 0.0;
    state.energy -= actual;
    if (state.energy < 1e-12) {
        // Do not silently destroy the sub-epsilon remainder: count it as
        // dissipation so the global energy balance stays exact.
        const double residual = std::max(0.0, state.energy);
        if (residual > 0.0) {
            stats_.reasons[static_cast<std::size_t>(reason)].out += residual;
            stats_.dissipated += residual;
        }
        state.energy = 0.0;
    }
    stats_.reasons[static_cast<std::size_t>(reason)].out += actual;
    stats_.dissipated += actual;
    return actual;
}
void EnergyLedger::kill(core::EnergyState& state, core::EntityId, EnergyReason reason) {
    if (!state.alive) return;
    const double residual = std::max(0.0, state.energy);
    if (residual > 0.0) {
        state.energy = 0.0;
        stats_.reasons[static_cast<std::size_t>(reason)].out += residual;
        stats_.dissipated += residual;
    }
    state.alive = false;
}
double EnergyLedger::totalIn(EnergyReason reason) const {
    return stats_.reasons[static_cast<std::size_t>(reason)].in;
}
double EnergyLedger::totalOut(EnergyReason reason) const {
    return stats_.reasons[static_cast<std::size_t>(reason)].out;
}
double EnergyLedger::auditError(double storedEnergy) const {
    return (stats_.injected - stats_.dissipated) - storedEnergy;
}
bool EnergyLedger::audit(double storedEnergy, double epsilon) const {
    return std::isfinite(storedEnergy) && std::abs(auditError(storedEnergy)) <= epsilon;
}
void EnergyLedger::reset() { stats_ = {}; initialSealed_ = false; }
} // namespace world

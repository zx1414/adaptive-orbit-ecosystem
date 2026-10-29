#pragma once
#include "core/Types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace producers { class PhotoProducer; }

namespace world {
enum class EnergyReason : std::uint8_t {
    InitialEndowment = 0,
    Photosynthesis,
    ProducerMaintenance,
    ProducerReproduction,
    AgentMetabolism,
    AgentMove,
    AgentHarvest,
    AgentReproduction,
    AgentDeath,
    ProducerDeath,
    Count
};
constexpr std::size_t kEnergyReasonCount = static_cast<std::size_t>(EnergyReason::Count);
const char* energyReasonName(EnergyReason reason);

struct ReasonTotals { double in = 0.0; double out = 0.0; };

struct EnergyStats {
    std::array<ReasonTotals, kEnergyReasonCount> reasons{};
    double injected = 0.0;
    double dissipated = 0.0;
    double transferred = 0.0;
    double injectedInitial() const { return reasons[static_cast<std::size_t>(EnergyReason::InitialEndowment)].in; }
    double injectedPrimary() const { return reasons[static_cast<std::size_t>(EnergyReason::Photosynthesis)].in; }
};

class EnergyLedger {
public:
    // One-time world setup only. World seals the ledger after initialization;
    // any later call is ignored.
    double injectInitial(core::EnergyState& state, core::EntityId id, double amount);
    void sealInitial() { initialSealed_ = true; }
    bool initialSealed() const { return initialSealed_; }
    double transfer(core::EnergyState& from, core::EnergyState& to, double amount, EnergyReason reason);
    double dissipate(core::EnergyState& state, core::EntityId id, double amount, EnergyReason reason);
    void kill(core::EnergyState& state, core::EntityId id, EnergyReason reason);
    const EnergyStats& stats() const { return stats_; }
    double totalIn(EnergyReason reason) const;
    double totalOut(EnergyReason reason) const;
    double auditError(double storedEnergy) const;
    bool audit(double storedEnergy, double epsilon = 1e-9) const;
    void reset();
private:
    static bool validAmount(double amount);
    static double headroom(const core::EnergyState& state);
    // The only primary energy injector is a producer's photosynthesis.
    friend class producers::PhotoProducer;
    double injectPrimary(core::EnergyState& state, core::EntityId id, double amount);
    EnergyStats stats_{};
    bool initialSealed_ = false;
};
} // namespace world

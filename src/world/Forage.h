#pragma once
#include "core/Types.h"

namespace world {
// Mutable access to a producer for foraging. Implemented by the composition root.
class Forage {
public:
    virtual ~Forage() = default;
    // Move up to amount energy from producer producerId into to, leaving at least
    // reserve in the producer. Kills the producer if it is exhausted.
    // Returns the energy actually moved (0 if the producer is missing or dead).
    virtual double harvest(core::EntityId producerId, double amount, double reserve,
                           core::EnergyState& to) = 0;
};
} // namespace world

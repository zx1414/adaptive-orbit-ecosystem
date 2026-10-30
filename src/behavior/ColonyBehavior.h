#pragma once
#include "colony/ColonySystem.h"
#include "core/Config.h"
#include "core/Random.h"
#include "core/Types.h"
#include "decision/Action.h"
#include "environment/Cyanobacteria.h"
#include "world/Energy.h"

namespace agentsim {

// Executes a nucleus's action against the world. This is the only layer that writes nucleus,
// ball and cyanobacteria state, and every energy change goes through the ledger.
struct BehaviorResult {
    bool reproduced = false;
    bool consumedPrey = false;
    double energyGained = 0.0;
};

BehaviorResult executeColonyAction(colony::ColonySystem& colonies, std::size_t colonyIndex,
                                   const decision::Action& action,
                                   environment::Cyanobacteria& cyano,
                                   world::EnergyLedger& ledger, core::Random& random,
                                   const core::SimConfig& config);

// Applies metabolism to every living nucleus and kills the ones that cannot pay.
void applyColonyMetabolism(colony::ColonySystem& colonies, world::EnergyLedger& ledger,
                           const core::SimConfig& config);

// Reproduction: a nucleus above its gene threshold splits, the child inherits mutated genes.
// Returns the number of new nuclei.
std::size_t reproduceColonies(colony::ColonySystem& colonies, world::EnergyLedger& ledger,
                              core::Random& random, const core::SimConfig& config);

// Adopts free balls lying within adoptRadius of any nucleus.
void adoptFreeBalls(colony::ColonySystem& colonies);

} // namespace agentsim

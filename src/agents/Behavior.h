#pragma once
#include "agents/Agent.h"
#include "decision/Action.h"
#include "world/Energy.h"
#include "world/Forage.h"
#include "world/WorldQuery.h"

namespace agentsim {
class Behavior {
public:
    static void execute(Agent& agent, const decision::Action& intent,
                        const world::WorldQuery& query, world::Forage& forage,
                        world::EnergyLedger& ledger);
};
} // namespace agentsim

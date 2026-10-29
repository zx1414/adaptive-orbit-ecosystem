#pragma once
#include "agents/Agent.h"
#include "decision/Action.h"
#include "world/Energy.h"

namespace world { class World; }

namespace agentsim {
class Behavior {
public:
    static void execute(Agent& agent, const decision::Action& intent,
                        world::World& world, world::EnergyLedger& ledger);
};
} // namespace agentsim

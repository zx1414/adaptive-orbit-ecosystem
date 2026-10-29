#pragma once
#include "agents/Agent.h"
#include "decision/Observation.h"
#include "world/WorldQuery.h"

namespace agentsim {
decision::Observation buildObservation(const Agent& agent, const world::WorldQuery& query);
} // namespace agentsim

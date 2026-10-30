#pragma once
#include "colony/ColonySystem.h"
#include "core/Random.h"
#include "decision/Observation.h"
#include "environment/Cyanobacteria.h"

#include <vector>

namespace agentsim {

// Builds Observation v2 for one nucleus.
//
// Sensing is distributed: each scout ball samples its own neighbourhood, the reading decays
// with distance and carries noise, and the nucleus keeps the strongest report per target. With
// no scout ball the external channels stay at the empty sentinel, so a nucleus that spent
// nothing on sensing is genuinely blind to prey and rivals.
decision::Observation buildColonyObservation(const colony::Colony& self,
                                             const colony::ColonySystem& colonies,
                                             const environment::Cyanobacteria& cyano,
                                             core::Bounds bounds, core::Tick tick,
                                             const core::LightConfig& light,
                                             core::Random& sensorRandom);

} // namespace agentsim

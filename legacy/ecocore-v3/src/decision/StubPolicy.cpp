#include "decision/StubPolicy.h"
#include <algorithm>

namespace decision {
PolicySpec StubPolicy::spec() const {
    return {"stub", kObservationVersion, Observation::kDim, 1, 0};
}
Action StubPolicy::decide(const Observation& observation) {
    Action action;
    if (observation.version != kObservationVersion) return action;
    action.kind = Action::Kind::Move;
    action.dirX = 0.70710678f;
    action.dirY = 0.70710678f;
    action.magnitude = 0.5f;
    return action;
}
std::unique_ptr<IPolicy> StubPolicy::clone() const { return std::make_unique<StubPolicy>(); }
void StubPolicy::mutate(core::Random&, float) {}
std::vector<float> StubPolicy::genome() const { return {}; }
void StubPolicy::setGenome(const float*, std::size_t) {}
} // namespace decision

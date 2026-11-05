// agentsim - environment/AgentEnvironment.cpp
#include "AgentEnvironment.h"

namespace agentsim {

AgentEnvironment::AgentEnvironment(const SimConfig& config) : config_(config) {}

void AgentEnvironment::setBlockedPredicate(BlockedFn fn, void* ctx) {
    blockedFn_ = fn;
    blockedCtx_ = ctx;
}

void AgentEnvironment::tick() { ++tick_; }

Vec2 AgentEnvironment::wrap(Vec2 v) const {
    return agentsim::wrap(v, config_.world.width, config_.world.height);
}

Vec2 AgentEnvironment::torusDelta(Vec2 a, Vec2 b) const {
    return agentsim::torusDelta(a, b, config_.world.width, config_.world.height);
}

double AgentEnvironment::torusDistance(Vec2 a, Vec2 b) const {
    return agentsim::torusDistance(a, b, config_.world.width, config_.world.height);
}

} // namespace agentsim

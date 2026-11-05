// agentsim - ledger/AgentLedger.cpp
#include "AgentLedger.h"

namespace agentsim {

void AgentLedger::injectInitial(std::uint64_t /*entityId*/, double amount) {
    injected_ += amount;
}

void AgentLedger::injectPrimary(std::uint64_t /*entityId*/, double amount) {
    injected_ += amount;
}

void AgentLedger::dissipate(const std::string& reason, double amount) {
    dissipated_ += amount;
    dissipatedByReason_[reason] += amount;
}

void AgentLedger::transfer(std::uint64_t /*from*/, std::uint64_t /*to*/, double amount) {
    // 搬运不改总量：只记账转移流量，injected_/dissipated_ 不动。
    transferred_ += amount;
}

double AgentLedger::dissipatedBy(const std::string& reason) const {
    const auto it = dissipatedByReason_.find(reason);
    return it == dissipatedByReason_.end() ? 0.0 : it->second;
}

bool AgentLedger::audit(double tolerance, double sumLiving) const {
    const double diff = netInjected() - sumLiving;
    return diff <= tolerance && diff >= -tolerance;
}

void AgentLedger::reset() {
    injected_ = 0.0;
    dissipated_ = 0.0;
    transferred_ = 0.0;
    dissipatedByReason_.clear();
}

} // namespace agentsim

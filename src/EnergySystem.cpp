#include "EnergySystem.h"
#include "Nucleus.h"

double EnergySystem::apply(Nucleus& n, double delta, EnergyReason why) {
    if (!n.alive) return n.energy;
    n.energy += delta;
    if (delta > 0.0) {
        totals_[(size_t)why][0] += delta;
    } else if (delta < 0.0) {
        totals_[(size_t)why][1] += -delta;
    }
    if (n.energy <= 0.0) {
        n.alive = false;
        SimEvent e;
        e.type = EventType::NUCLEUS_DIED;
        e.frame = 0;  // 帧号由调用方无从得知，保持 0；mod 可读核状态
        e.nucleus = &n;
        bus_.emit(e);
    }
    return n.energy;
}

void EnergySystem::printSummary(std::ostream& os) const {
    static const char* names[] = {
        "ABSORB", "COMBAT", "METABOLISM", "REPRODUCTION",
        "AGING", "CROWDING", "PLAGUE", "BALL_LOSS"};
    os << "能量收支摘要（累计）：\n";
    for (size_t i = 0; i < kReasonCount; ++i) {
        if (totals_[i][0] == 0.0 && totals_[i][1] == 0.0) continue;
        os << "  " << names[i] << ": in " << totals_[i][0]
           << ", out " << totals_[i][1] << "\n";
    }
}

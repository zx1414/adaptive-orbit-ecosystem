#include "core/Time.h"
#include <cmath>

namespace core {
namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

double DayNightCycle::phase(Tick tick) const {
    if (period_ == 0) return 0.0;
    const double p = static_cast<double>(tick % period_) / static_cast<double>(period_);
    const double value = p + offset_;
    return value - std::floor(value);
}
double DayNightCycle::sine(Tick tick) const { return std::sin(kTwoPi * phase(tick)); }
double DayNightCycle::cosine(Tick tick) const { return std::cos(kTwoPi * phase(tick)); }
double DayNightCycle::daylight(Tick tick) const { return 0.5 + 0.5 * sine(tick); }

double dayPhase(Tick tick, Tick period, double offset) {
    return DayNightCycle(period, offset).phase(tick);
}
double daylight(Tick tick, Tick period, double offset) {
    return DayNightCycle(period, offset).daylight(tick);
}

} // namespace core

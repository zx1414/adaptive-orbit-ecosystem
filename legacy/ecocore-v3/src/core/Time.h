#pragma once

#include "core/Types.h"

namespace core {

class DayNightCycle {
public:
    explicit DayNightCycle(Tick period = 2400, double offset = 0.0)
        : period_(period), offset_(offset) {}

    Tick period() const { return period_; }
    double offset() const { return offset_; }
    double phase(Tick tick) const;
    double sine(Tick tick) const;
    double cosine(Tick tick) const;
    double daylight(Tick tick) const;
    bool isDay(Tick tick) const { return daylight(tick) > 0.0; }
    Vec2 direction(Tick tick) const { return {cosine(tick), sine(tick)}; }

private:
    Tick period_;
    double offset_;
};

using DayPhase = DayNightCycle;
double dayPhase(Tick tick, Tick period, double offset = 0.0);
double daylight(Tick tick, Tick period, double offset = 0.0);

} // namespace core

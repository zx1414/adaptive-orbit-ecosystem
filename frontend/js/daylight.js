// Day/night mirror of src/core/Time.cpp DayNightCycle (default LightConfig: period 2400, offset 0).
(function (NS) {
    'use strict';
    const TAU = 6.283185307179586476925286766559;
    NS.DAY_PERIOD = 2400;

    NS.dayPhase = function (tick, period, offset) {
        period = period || NS.DAY_PERIOD;
        offset = offset || 0;
        if (!period) return 0;
        const p = (tick % period) / period;
        const v = p + offset;
        return v - Math.floor(v);
    };
    NS.daylightAt = function (tick, period, offset) {
        return 0.5 + 0.5 * Math.sin(TAU * NS.dayPhase(tick, period, offset));
    };
    NS.dayLabel = function (tick, period) {
        period = period || NS.DAY_PERIOD;
        const day = Math.floor(tick / period) + 1;
        const minutes = Math.round(NS.dayPhase(tick, period) * 24 * 60);
        const hh = String(Math.floor(minutes / 60)).padStart(2, '0');
        const mm = String(minutes % 60).padStart(2, '0');
        return { day: day, clock: hh + ':' + mm, phase: NS.dayPhase(tick, period) };
    };
})(window.EcoScope = window.EcoScope || {});

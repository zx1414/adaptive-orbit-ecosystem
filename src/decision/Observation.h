#pragma once
#include <array>
#include <cstdint>

namespace decision {
constexpr int kObservationDim = 28;
constexpr std::uint32_t kObservationVersion = 1;

// Version 1 layout. Sensors fills every slot; policies must treat this as the
// contract and reject observations with a different version.
namespace obs {
constexpr int EnergyRatio = 0;   // self energy / cap, [0,1]
constexpr int SpeedRatio = 1;    // |velocity| / maxSpeed, [0,1]
constexpr int EdgeLowX = 2;      // distance to minX / senseRadius, [0,1]
constexpr int EdgeHighX = 3;     // distance to maxX / senseRadius, [0,1]
constexpr int EdgeLowY = 4;      // distance to minY / senseRadius, [0,1]
constexpr int EdgeHighY = 5;     // distance to maxY / senseRadius, [0,1]
constexpr int Light = 6;         // light * 0.5, [0,1]
constexpr int DaySin = 7;        // sin(day phase), [-1,1]
constexpr int Producer0 = 8;     // two nearest producers, 4 floats each:
constexpr int Producer1 = 12;    // dx/sense, dy/sense, dist/sense (-1 if empty), energy/cap
constexpr int Agent0 = 16;       // two nearest other agents, 4 floats each:
constexpr int Agent1 = 20;       // dx/sense, dy/sense, dist/sense (-1 if empty), relative energy
constexpr int Density = 24;      // nearby agent count / 4, [0,1]
constexpr int DaySinRepeat = 25; // sin(day phase), kept for wander
constexpr int DayCos = 26;       // cos(day phase), [-1,1]
constexpr int Reserved = 27;     // always 0 in version 1
constexpr int SlotStride = 4;
} // namespace obs

struct Observation {
    static constexpr int kDim = kObservationDim;
    std::array<float, kObservationDim> values{};
    std::uint32_t version = kObservationVersion;
    float operator[](int index) const { return values[static_cast<std::size_t>(index)]; }
    float& operator[](int index) { return values[static_cast<std::size_t>(index)]; }
};
} // namespace decision

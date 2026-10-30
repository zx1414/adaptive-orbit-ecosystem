#pragma once
#include <array>
#include <cstdint>

namespace decision {
// 26 = 10 scalar channels + two 4-float prey slots + two 4-float rival slots.
// Get this wrong and the last slot's writes land past values[] and corrupt `version`.
constexpr int kObservationDim = 26;
constexpr std::uint32_t kObservationVersion = 2;

// Version 2 layout: the colony model. A nucleus senses its own state, what its scout balls
// reported, and its organ inventory. Policies must reject a different version.
//
// Sensing is gated by scouts: with no scout ball the external channels (prey/other) stay at
// their empty sentinel, so a nucleus that invested nothing in sensing is blind.
namespace obs {
constexpr int EnergyRatio = 0;    // nucleus energy / colony capacity, [0,1]
constexpr int StorageRatio = 1;   // ball-stored energy / colony capacity, [0,1]
constexpr int SpeedRatio = 2;     // |velocity| / maxSpeed, [0,1]
constexpr int EdgeLowX = 3;
constexpr int EdgeHighX = 4;
constexpr int EdgeLowY = 5;
constexpr int EdgeHighY = 6;
constexpr int ResourceBalls = 7;  // resource ball count / 4, [0,1]
constexpr int ScoutBalls = 8;     // scout ball count / 4, [0,1]
constexpr int ShieldBalls = 9;    // shield ball count / 4, [0,1]
constexpr int Prey0 = 10;         // nearest cyanobacteria node: dx, dy, dist (-1 empty), energy
constexpr int Prey1 = 14;         // second nearest
constexpr int Rival0 = 18;        // nearest other nucleus: dx, dy, dist (-1 empty), relative energy
constexpr int Rival1 = 22;        // second nearest (occupies 22..25)
constexpr int SlotStride = 4;
// Guard: the highest index any slot may write.
constexpr int kLastIndex = Rival1 + SlotStride - 1;
static_assert(kLastIndex == kObservationDim - 1, "slot layout must fill exactly kObservationDim");
} // namespace obs

struct Observation {
    static constexpr int kDim = kObservationDim;
    std::array<float, kObservationDim> values{};
    std::uint32_t version = kObservationVersion;
    float operator[](int index) const { return values[static_cast<std::size_t>(index)]; }
    float& operator[](int index) { return values[static_cast<std::size_t>(index)]; }
};
} // namespace decision

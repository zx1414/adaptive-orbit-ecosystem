#pragma once

#include "core/Types.h"
#include "world/WorldView.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace app {
// Shared frame buffer for the trace exporters (HTML replay and plain JSON).
// Contract:
//   { "bounds": {...}, "frames": [ { "tick", "cyanobacteria", "nuclei", "balls",
//     "totalEnergy", "c": [[x,y,energy,cap]], "n": [[x,y,energy,cap,id,0]],
//     "b": [[x,y,energy,cap,ownerId,type]] } ] }
// Nodes are the environment layer (producers), nuclei are the consumers, and balls carry
// ownerId so the frontend can draw them orbiting their nucleus.
namespace trace {

struct Body {
    double x = 0.0;
    double y = 0.0;
    double energy = 0.0;
    double cap = 1.0;
    std::uint64_t ownerId = 0;
    std::uint8_t type = 0;
};

struct Frame {
    core::Tick tick = 0;
    std::size_t cyanobacteria = 0;
    std::size_t nuclei = 0;
    std::size_t balls = 0;
    double totalEnergy = 0.0;
    std::vector<Body> nodeBodies;
    std::vector<Body> nucleusBodies;
    std::vector<Body> ballBodies;
};

class Buffer final {
public:
    void capture(const world::WorldView& view);
    const std::vector<Frame>& frames() const { return frames_; }
    const core::Bounds& bounds() const { return bounds_; }
    bool downsampled() const { return downsampled_; }
    std::string toJson(const std::string& name, const std::string& metaJson) const;

private:
    core::Bounds bounds_{};
    std::vector<Frame> frames_;
    bool downsampled_ = false;
};

} // namespace trace
} // namespace app

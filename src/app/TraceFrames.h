#pragma once

#include "core/Types.h"
#include "world/WorldView.h"

#include <cstddef>
#include <string>
#include <vector>

namespace app {
// Shared frame buffer for the trace exporters (HTML replay and plain JSON).
// This is the single source of truth for the frontend data contract:
//   { "bounds": {...}, "frames": [ { "tick", "producers", "agents",
//     "totalEnergy", "p": [[x,y,energy,cap],...], "a": [...] } ] }
namespace trace {

struct Body {
    double x = 0.0;
    double y = 0.0;
    double energy = 0.0;
    double cap = 1.0;
};

struct Frame {
    core::Tick tick = 0;
    std::size_t producers = 0;
    std::size_t agents = 0;
    double totalEnergy = 0.0;
    std::vector<Body> producerBodies;
    std::vector<Body> agentBodies;
};

class Buffer final {
public:
    // Appends a frame sampled from the view. When the frame cap is reached the
    // buffer keeps the whole time span by halving the resolution; the final
    // frame is always retained, so the end state is never silently lost.
    void capture(const world::WorldView& view);

    const std::vector<Frame>& frames() const { return frames_; }
    const core::Bounds& bounds() const { return bounds_; }
    bool downsampled() const { return downsampled_; }

    // Serializes the buffer as the plain JSON trace document (no trailing
    // newline). Optional name/meta are emitted first when provided.
    std::string toJson(const std::string& name, const std::string& metaJson) const;

private:
    core::Bounds bounds_{};
    std::vector<Frame> frames_;
    bool downsampled_ = false;
};

} // namespace trace
} // namespace app

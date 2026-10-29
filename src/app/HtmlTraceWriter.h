#pragma once

#include "core/Types.h"
#include "world/WorldView.h"

#include <cstddef>
#include <string>
#include <vector>

namespace app {
// Captures WorldView frames and writes one self-contained HTML replay file.
// The class never mutates the simulation and performs no I/O until write().
class HtmlTraceWriter final {
public:
    explicit HtmlTraceWriter(std::string path);

    void capture(const world::WorldView& view);
    bool write();
    bool ok() const { return ok_; }
    bool downsampled() const { return downsampled_; }
    const std::string& error() const { return error_; }
    std::size_t frameCount() const { return frames_.size(); }

private:
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

    std::string path_;
    std::string error_;
    bool ok_ = true;
    bool downsampled_ = false;
    core::Bounds bounds_{};
    std::vector<Frame> frames_;
};
} // namespace app
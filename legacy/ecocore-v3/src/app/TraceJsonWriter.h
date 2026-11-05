#pragma once

#include "app/TraceFrames.h"
#include "core/Types.h"

#include <string>

namespace app {
// Writes the plain JSON trace document consumed by the frontend (drag & drop).
class TraceJsonWriter final {
public:
    // name/meta are optional decorations understood by the frontend.
    TraceJsonWriter(std::string path, std::string name, std::string metaJson);

    void capture(const world::WorldView& view);
    bool write();
    bool ok() const { return ok_; }
    bool downsampled() const { return buffer_.downsampled(); }
    const std::string& error() const { return error_; }
    std::size_t frameCount() const { return buffer_.frames().size(); }

private:
    std::string path_;
    std::string name_;
    std::string metaJson_;
    std::string error_;
    bool ok_ = true;
    trace::Buffer buffer_;
};
} // namespace app

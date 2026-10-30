#pragma once

#include "app/TraceFrames.h"
#include "core/Types.h"

#include <string>

namespace app {
// Writes a self-contained HTML replay file around the shared trace buffer.
class HtmlTraceWriter final {
public:
    explicit HtmlTraceWriter(std::string path);

    void capture(const world::WorldView& view);
    bool write();
    bool ok() const { return ok_; }
    bool downsampled() const { return buffer_.downsampled(); }
    const std::string& error() const { return error_; }
    std::size_t frameCount() const { return buffer_.frames().size(); }

private:
    std::string path_;
    std::string error_;
    bool ok_ = true;
    trace::Buffer buffer_;
};
} // namespace app

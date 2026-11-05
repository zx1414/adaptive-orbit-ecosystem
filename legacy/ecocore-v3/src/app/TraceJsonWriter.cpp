#include "app/TraceJsonWriter.h"

#include <fstream>
#include <utility>

namespace app {
TraceJsonWriter::TraceJsonWriter(std::string path, std::string name, std::string metaJson)
    : path_(std::move(path)), name_(std::move(name)), metaJson_(std::move(metaJson)) {
    // Fail fast instead of discovering an unwritable destination after a long run.
    std::ofstream probe(path_, std::ios::binary | std::ios::trunc);
    if (!probe) {
        error_ = "cannot open trace json for writing: " + path_;
        ok_ = false;
        return;
    }
    probe.flush();
    if (!probe) {
        error_ = "failed while probing trace json: " + path_;
        ok_ = false;
    }
}

void TraceJsonWriter::capture(const world::WorldView& view) { buffer_.capture(view); }

bool TraceJsonWriter::write() {
    if (!ok_) return false;
    const std::string json = buffer_.toJson(name_, metaJson_);

    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) { error_ = "cannot open trace json for writing: " + path_; return false; }
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
    out.put('\n');
    out.flush();
    if (!out) { error_ = "failed while writing trace json: " + path_; return false; }
    error_.clear();
    return true;
}
} // namespace app

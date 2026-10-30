#include "sim/Snapshot.h"

#include <cstring>
#include <fstream>
#include <limits>

namespace sim::snapshot {
namespace {
void writeU32(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out[offset + static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
}
void writeU64(std::vector<std::uint8_t>& out, std::size_t offset, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) out[offset + static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
}
std::uint32_t readU32(const std::uint8_t* data) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(data[i]) << (8 * i);
    return value;
}
std::uint64_t readU64(const std::uint8_t* data) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(data[i]) << (8 * i);
    return value;
}
} // namespace

void Writer::fail(const std::string& message) {
    if (ok_) { ok_ = false; error_ = message; }
}
void Writer::u8(std::uint8_t value) { data_.push_back(value); }
void Writer::u32(std::uint32_t value) {
    for (int i = 0; i < 4; ++i) data_.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
}
void Writer::u64(std::uint64_t value) {
    for (int i = 0; i < 8; ++i) data_.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
}
void Writer::f32(float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected float size");
    std::memcpy(&bits, &value, sizeof(bits));
    u32(bits);
}
void Writer::f64(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    std::memcpy(&bits, &value, sizeof(bits));
    u64(bits);
}
void Writer::str(const std::string& value) {
    if (value.size() > kMaxString || value.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail("string too large for snapshot");
        return;
    }
    u32(static_cast<std::uint32_t>(value.size()));
    bytes(value.data(), value.size());
}
void Writer::bytes(const void* data, std::size_t size) {
    if (size > kMaxPayload) { fail("byte block too large for snapshot"); return; }
    if (size == 0) return;
    if (data == nullptr) { fail("null byte block"); return; }
    const auto* begin = static_cast<const std::uint8_t*>(data);
    data_.insert(data_.end(), begin, begin + size);
}

bool Reader::fail(const std::string& message) {
    if (ok_) { ok_ = false; error_ = message; }
    return false;
}
bool Reader::need(std::size_t size) {
    if (!ok_) return false;
    if (size > data_.size() - position_) return fail("truncated snapshot payload");
    return true;
}
bool Reader::u8(std::uint8_t& value) {
    if (!need(1)) return false;
    value = data_[position_++];
    return true;
}
bool Reader::u32(std::uint32_t& value) {
    if (!need(4)) return false;
    value = readU32(data_.data() + position_);
    position_ += 4;
    return true;
}
bool Reader::u64(std::uint64_t& value) {
    if (!need(8)) return false;
    value = readU64(data_.data() + position_);
    position_ += 8;
    return true;
}
bool Reader::f32(float& value) {
    std::uint32_t bits = 0;
    if (!u32(bits)) return false;
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}
bool Reader::f64(double& value) {
    std::uint64_t bits = 0;
    if (!u64(bits)) return false;
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}
bool Reader::str(std::string& value) {
    std::uint32_t size = 0;
    if (!u32(size)) return false;
    if (size > kMaxString) return fail("string too large in snapshot");
    if (!need(size)) return false;
    value.assign(reinterpret_cast<const char*>(data_.data() + position_), size);
    position_ += size;
    return true;
}
bool Reader::bytes(std::vector<std::uint8_t>& value, std::size_t size) {
    if (size > kMaxPayload) return fail("byte block too large in snapshot");
    if (!need(size)) return false;
    value.assign(data_.begin() + static_cast<std::ptrdiff_t>(position_),
                 data_.begin() + static_cast<std::ptrdiff_t>(position_ + size));
    position_ += size;
    return true;
}

std::uint64_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(data[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool encode(const std::vector<std::uint8_t>& payload, std::vector<std::uint8_t>& file,
            std::string& error) {
    if (payload.size() > kMaxPayload) { error = "snapshot payload too large"; return false; }
    file.resize(kHeaderSize + payload.size());
    std::memcpy(file.data(), kMagic, sizeof(kMagic));
    writeU32(file, 8, kFormatVersion);
    writeU32(file, 12, static_cast<std::uint32_t>(kHeaderSize));
    writeU64(file, 16, static_cast<std::uint64_t>(payload.size()));
    writeU64(file, 24, fnv1a(payload.data(), payload.size()));
    if (!payload.empty()) std::memcpy(file.data() + kHeaderSize, payload.data(), payload.size());
    error.clear();
    return true;
}

bool decode(const std::vector<std::uint8_t>& file, std::vector<std::uint8_t>& payload,
            std::string& error) {
    if (file.size() < kHeaderSize) { error = "snapshot file is shorter than its header"; return false; }
    if (std::memcmp(file.data(), kMagic, sizeof(kMagic)) != 0) {
        error = "snapshot magic mismatch";
        return false;
    }
    const std::uint32_t version = readU32(file.data() + 8);
    if (version != kFormatVersion) {
        error = "unsupported snapshot format version " + std::to_string(version);
        return false;
    }
    const std::uint32_t headerSize = readU32(file.data() + 12);
    if (headerSize != kHeaderSize) { error = "snapshot header size mismatch"; return false; }
    const std::uint64_t payloadSize = readU64(file.data() + 16);
    if (payloadSize > kMaxPayload) { error = "snapshot payload size exceeds limit"; return false; }
    if (file.size() != kHeaderSize + static_cast<std::size_t>(payloadSize)) {
        error = "snapshot file size does not match payload size";
        return false;
    }
    const std::uint64_t checksum = readU64(file.data() + 24);
    const std::uint64_t actual = fnv1a(file.data() + kHeaderSize,
                                       static_cast<std::size_t>(payloadSize));
    if (checksum != actual) { error = "snapshot checksum mismatch"; return false; }
    payload.assign(file.begin() + static_cast<std::ptrdiff_t>(kHeaderSize), file.end());
    error.clear();
    return true;
}

bool writeFile(const std::string& path, const std::vector<std::uint8_t>& payload,
               std::string& error) {
    std::vector<std::uint8_t> file;
    if (!encode(payload, file, error)) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot open snapshot for writing: " + path; return false; }
    out.write(reinterpret_cast<const char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
    out.flush();
    if (!out) { error = "failed while writing snapshot: " + path; return false; }
    error.clear();
    return true;
}

LoadStatus readFile(const std::string& path, std::vector<std::uint8_t>& payload,
                    std::string& error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) { error = "cannot open snapshot: " + path; return LoadStatus::IoError; }
    const std::streamoff length = in.tellg();
    if (length < 0) { error = "cannot determine snapshot size: " + path; return LoadStatus::IoError; }
    if (static_cast<std::uint64_t>(length) > kMaxPayload + kHeaderSize) {
        error = "snapshot file exceeds size limit: " + path;
        return LoadStatus::FormatError;
    }
    std::vector<std::uint8_t> file(static_cast<std::size_t>(length));
    in.seekg(0, std::ios::beg);
    if (!in) { error = "cannot seek snapshot: " + path; return LoadStatus::IoError; }
    if (!file.empty()) {
        in.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size()));
        if (!in) { error = "failed while reading snapshot: " + path; return LoadStatus::IoError; }
    }
    // Anything past this point is a problem with the file contents, not the file system.
    return decode(file, payload, error) ? LoadStatus::Ok : LoadStatus::FormatError;
}

} // namespace sim::snapshot

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sim::snapshot {

constexpr std::uint32_t kFormatVersion = 1;
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kMaxPayload = 1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxString = 1024ULL * 1024ULL;
inline constexpr std::uint8_t kMagic[8] = {'E', 'C', 'O', 'C', 'O', 'R', 'E', '3'};

// Failure class for the load path. The CLI maps IoError to exit 4 and FormatError to exit 5.
enum class LoadStatus { Ok, IoError, FormatError };

// Little-endian payload writer. Once an error is recorded, ok() is false and
// callers must abort encoding.
class Writer {
public:
    void u8(std::uint8_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void f32(float value);
    void f64(double value);
    void str(const std::string& value);
    void bytes(const void* data, std::size_t size);
    const std::vector<std::uint8_t>& data() const { return data_; }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

private:
    void fail(const std::string& message);
    std::vector<std::uint8_t> data_;
    bool ok_ = true;
    std::string error_;
};

// Bounds-checked little-endian payload reader.
class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& data) : data_(data) {}
    bool u8(std::uint8_t& value);
    bool u32(std::uint32_t& value);
    bool u64(std::uint64_t& value);
    bool f32(float& value);
    bool f64(double& value);
    bool str(std::string& value);
    bool bytes(std::vector<std::uint8_t>& value, std::size_t size);
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    std::size_t remaining() const { return data_.size() - position_; }

private:
    bool need(std::size_t size);
    bool fail(const std::string& message);
    const std::vector<std::uint8_t>& data_;
    std::size_t position_ = 0;
    bool ok_ = true;
    std::string error_;
};

std::uint64_t fnv1a(const std::uint8_t* data, std::size_t size);

// Serialize payload into magic/version/length/checksum header + payload.
bool encode(const std::vector<std::uint8_t>& payload, std::vector<std::uint8_t>& file,
            std::string& error);
// Validate magic/version/header size/length/checksum and extract payload.
bool decode(const std::vector<std::uint8_t>& file, std::vector<std::uint8_t>& payload,
            std::string& error);

bool writeFile(const std::string& path, const std::vector<std::uint8_t>& payload,
               std::string& error);
// Reports IoError for open/size/read failures and FormatError for an undecodable file, so the
// CLI can map them to distinct exit codes without matching error strings.
LoadStatus readFile(const std::string& path, std::vector<std::uint8_t>& payload,
                    std::string& error);

} // namespace sim::snapshot

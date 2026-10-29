#include "TestHarness.h"

#include "sim/Snapshot.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> samplePayload() {
    sim::snapshot::Writer writer;
    writer.u8(0x12);
    writer.u32(0xA1B2C3D4U);
    writer.u64(0x0102030405060708ULL);
    writer.f32(1.5f);
    writer.f64(-2.25);
    writer.str("ecocore");
    const std::uint8_t bytes[3] = {7, 8, 9};
    writer.bytes(bytes, sizeof(bytes));
    return writer.data();
}
std::filesystem::path tempPath(const char* name) {
    std::error_code ignored;
    std::filesystem::create_directories("build", ignored);
    return std::filesystem::path("build") / (std::string("ecocore_m3_") + name + ".snap");
}
} // namespace

TEST("snapshot format roundtrips every primitive") {
    const std::vector<std::uint8_t> payload = samplePayload();
    std::vector<std::uint8_t> file;
    std::string error;
    CHECK(sim::snapshot::encode(payload, file, error));
    CHECK(error.empty());
    CHECK(file.size() == sim::snapshot::kHeaderSize + payload.size());

    std::vector<std::uint8_t> decoded;
    CHECK(sim::snapshot::decode(file, decoded, error));
    CHECK(decoded == payload);

    sim::snapshot::Reader reader(decoded);
    std::uint8_t u8 = 0;
    std::uint32_t u32 = 0;
    std::uint64_t u64 = 0;
    float f32 = 0.0f;
    double f64 = 0.0;
    std::string text;
    std::vector<std::uint8_t> bytes;
    CHECK(reader.u8(u8));
    CHECK(reader.u32(u32));
    CHECK(reader.u64(u64));
    CHECK(reader.f32(f32));
    CHECK(reader.f64(f64));
    CHECK(reader.str(text));
    CHECK(reader.bytes(bytes, 3));
    CHECK(reader.ok());
    CHECK(reader.remaining() == 0);
    CHECK(u8 == 0x12);
    CHECK(u32 == 0xA1B2C3D4U);
    CHECK(u64 == 0x0102030405060708ULL);
    CHECK_NEAR(f32, 1.5f, 0.0);
    CHECK_NEAR(f64, -2.25, 0.0);
    CHECK(text == "ecocore");
    CHECK(bytes.size() == 3);
    CHECK(bytes[0] == 7);
}

TEST("snapshot format accepts an empty payload") {
    std::vector<std::uint8_t> file;
    std::string error;
    CHECK(sim::snapshot::encode({}, file, error));
    std::vector<std::uint8_t> decoded;
    CHECK(sim::snapshot::decode(file, decoded, error));
    CHECK(decoded.empty());
}

TEST("snapshot format rejects corrupt headers and truncation") {
    std::vector<std::uint8_t> file;
    std::string error;
    CHECK(sim::snapshot::encode(samplePayload(), file, error));

    std::vector<std::uint8_t> badMagic = file;
    badMagic[0] ^= 0xFFU;
    std::vector<std::uint8_t> decoded;
    CHECK(!sim::snapshot::decode(badMagic, decoded, error));
    CHECK(!error.empty());

    std::vector<std::uint8_t> badVersion = file;
    badVersion[8] = 2;
    CHECK(!sim::snapshot::decode(badVersion, decoded, error));

    std::vector<std::uint8_t> truncated = file;
    truncated.pop_back();
    CHECK(!sim::snapshot::decode(truncated, decoded, error));

    std::vector<std::uint8_t> trailing = file;
    trailing.push_back(0);
    CHECK(!sim::snapshot::decode(trailing, decoded, error));

    std::vector<std::uint8_t> badChecksum = file;
    badChecksum.back() ^= 0x01U;
    CHECK(!sim::snapshot::decode(badChecksum, decoded, error));
}

TEST("snapshot reader fails cleanly at end of payload") {
    std::vector<std::uint8_t> payload;
    sim::snapshot::Reader reader(payload);
    std::uint64_t value = 0;
    CHECK(!reader.u64(value));
    CHECK(!reader.ok());
    CHECK(!reader.error().empty());
}

TEST("snapshot file roundtrip and missing file error") {
    const std::filesystem::path path = tempPath("roundtrip");
    std::string error;
    CHECK(sim::snapshot::writeFile(path.string(), samplePayload(), error));
    std::vector<std::uint8_t> payload;
    CHECK(sim::snapshot::readFile(path.string(), payload, error));
    CHECK(payload == samplePayload());
    std::filesystem::remove(path);

    CHECK(!sim::snapshot::readFile(tempPath("missing").string(), payload, error));
    CHECK(!error.empty());
}
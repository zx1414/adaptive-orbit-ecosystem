#include "WebRenderer.h"
#include <cstring>

namespace {
constexpr uint8_t kMagic = 'W';
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderSize = 64;
constexpr size_t kBallRecSize = 6;
constexpr size_t kNucleusRecSize = 28;

void writeU8(std::vector<uint8_t>& v, size_t off, uint8_t x) { v[off] = x; }
void writeU16(std::vector<uint8_t>& v, size_t off, uint16_t x) {
    v[off] = (uint8_t)(x & 0xFF);
    v[off + 1] = (uint8_t)(x >> 8);
}
void writeU32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off] = (uint8_t)(x & 0xFF);
    v[off + 1] = (uint8_t)((x >> 8) & 0xFF);
    v[off + 2] = (uint8_t)((x >> 16) & 0xFF);
    v[off + 3] = (uint8_t)((x >> 24) & 0xFF);
}
void writeF32(std::vector<uint8_t>& v, size_t off, float x) {
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(x), "float must be 32-bit");
    std::memcpy(&bits, &x, sizeof(bits));
    writeU32(v, off, bits);
}
}  // namespace

WebRenderer::WebRenderer(const World& world) {
    buildSnapshot(world);
}

void WebRenderer::render(const World& world) {
    buildSnapshot(world);
}

std::vector<uint8_t> WebRenderer::snapshotBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
}

void WebRenderer::buildSnapshot(const World& world) {
    const auto& balls = world.balls();
    const auto& nuclei = world.nuclei();
    const WorldConfig& cfg = world.config();

    std::vector<uint8_t> out;
    out.resize(kHeaderSize + balls.size() * kBallRecSize + nuclei.size() * kNucleusRecSize);

    writeU8(out, 0, kMagic);
    writeU8(out, 1, kVersion);
    writeU8(out, 2, world.finished() ? 1 : 0);
    writeU8(out, 3, 0);
    writeU32(out, 4, (uint32_t)world.frame());
    writeF32(out, 8, (float)cfg.width);
    writeF32(out, 12, (float)cfg.height);
    writeU32(out, 16, (uint32_t)balls.size());
    writeU32(out, 20, (uint32_t)world.aliveNucleusCount());

    int typeCount[3] = {0, 0, 0};
    for (const Ball& b : balls) {
        ++typeCount[(size_t)b.type];
    }
    writeU32(out, 24, (uint32_t)typeCount[0]);
    writeU32(out, 28, (uint32_t)typeCount[1]);
    writeU32(out, 32, (uint32_t)typeCount[2]);

    std::string reason = world.finishReason() ? world.finishReason() : "";
    for (size_t i = 0; i < 28; ++i) {
        out[36 + i] = i < reason.size() ? (uint8_t)reason[i] : 0;
    }

    const double sx = (double)65535.0 / cfg.width;
    const double sy = (double)65535.0 / cfg.height;
    size_t off = kHeaderSize;
    for (const Ball& b : balls) {
        uint16_t x = (uint16_t)(b.pos.x * sx);
        uint16_t y = (uint16_t)(b.pos.y * sy);
        uint32_t ownerBits = 0;
        if (b.ownerId >= 0 && b.ownerId < (int)nuclei.size()) {
            ownerBits = (uint32_t)(b.ownerId + 1);
        }
        if (ownerBits > 16383) ownerBits = 16383;
        uint16_t meta = (uint16_t)((ownerBits << 2) | (uint32_t)b.type);
        writeU16(out, off, x);
        writeU16(out, off + 2, y);
        writeU16(out, off + 4, meta);
        off += kBallRecSize;
    }

    for (const Nucleus& n : nuclei) {
        if (!n.alive) continue;
        writeF32(out, off, (float)n.pos.x);
        writeF32(out, off + 4, (float)n.pos.y);
        writeF32(out, off + 8, (float)n.energy);
        writeF32(out, off + 12, (float)n.params.attackStrength);
        writeF32(out, off + 16, (float)n.params.maxSpeed);
        writeF32(out, off + 20, (float)n.params.energyThreshold);
        writeU32(out, off + 24, (uint32_t)n.id);
        off += kNucleusRecSize;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    bytes_.swap(out);
}

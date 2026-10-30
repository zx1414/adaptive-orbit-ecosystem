#include "app/TraceFrames.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <utility>

namespace app::trace {
namespace {
constexpr std::size_t kMaxFrames = 2000;

void appendDouble(std::string& out, double value) {
    if (!std::isfinite(value)) value = 0.0;
    char buffer[64] = {};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                      std::chars_format::general, 10);
    if (result.ec == std::errc()) out.append(buffer, result.ptr);
    else out += '0';
}
void appendU64(std::string& out, std::uint64_t value) {
    char buffer[32] = {};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec == std::errc()) out.append(buffer, result.ptr);
    else out += '0';
}
void appendBounds(std::string& out, const core::Bounds& bounds) {
    out += "{\"minX\":";
    appendDouble(out, bounds.minX);
    out += ",\"minY\":";
    appendDouble(out, bounds.minY);
    out += ",\"maxX\":";
    appendDouble(out, bounds.maxX);
    out += ",\"maxY\":";
    appendDouble(out, bounds.maxY);
    out += '}';
}
void appendBodies(std::string& out, const std::vector<Body>& bodies) {
    out += '[';
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (i != 0) out += ',';
        out += '[';
        appendDouble(out, bodies[i].x);
        out += ',';
        appendDouble(out, bodies[i].y);
        out += ',';
        appendDouble(out, bodies[i].energy);
        out += ',';
        appendDouble(out, bodies[i].cap);
        out += ']';
    }
    out += ']';
}
} // namespace

void Buffer::capture(const world::WorldView& view) {
    if (frames_.size() >= kMaxFrames) {
        std::vector<Frame> reduced;
        reduced.reserve(frames_.size() / 2 + 1);
        for (std::size_t i = 0; i < frames_.size(); i += 2) {
            reduced.push_back(std::move(frames_[i]));
        }
        frames_ = std::move(reduced);
        downsampled_ = true;
    }
    bounds_ = view.bounds;
    Frame frame;
    frame.tick = view.stats.tick;
    frame.producers = view.stats.producers;
    frame.agents = view.stats.agents;
    frame.totalEnergy = view.stats.totalEnergy;
    frame.producerBodies.reserve(view.producers.size());
    for (const world::ProducerView& producer : view.producers) {
        frame.producerBodies.push_back({producer.position.x, producer.position.y,
                                        producer.energy, producer.cap});
    }
    frame.agentBodies.reserve(view.agents.size());
    for (const world::AgentView& agent : view.agents) {
        frame.agentBodies.push_back({agent.position.x, agent.position.y, agent.energy, agent.cap});
    }
    frames_.push_back(std::move(frame));
}

std::string Buffer::toJson(const std::string& name, const std::string& metaJson) const {
    std::string json;
    json.reserve(frames_.size() * 4096 + 256);
    json += '{';
    if (!name.empty()) {
        // Emit printable ASCII as-is; escape everything else as \u00xx so the
        // string is always valid JSON regardless of its content.
        json += "\"name\":\"";
        for (unsigned char c : name) {
            if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') {
                json += static_cast<char>(c);
            } else {
                char hex[8] = {};
                std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned>(c));
                json += hex;
            }
        }
        json += "\",";
    }
    if (!metaJson.empty()) {
        json += "\"meta\":";
        json += metaJson;
        json += ',';
    }
    json += "\"bounds\":";
    appendBounds(json, bounds_);
    json += ",\"frames\":[";
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        if (i != 0) json += ',';
        const Frame& frame = frames_[i];
        json += "{\"tick\":";
        appendU64(json, frame.tick);
        json += ",\"producers\":";
        appendU64(json, static_cast<std::uint64_t>(frame.producers));
        json += ",\"agents\":";
        appendU64(json, static_cast<std::uint64_t>(frame.agents));
        json += ",\"totalEnergy\":";
        appendDouble(json, frame.totalEnergy);
        json += ",\"p\":";
        appendBodies(json, frame.producerBodies);
        json += ",\"a\":";
        appendBodies(json, frame.agentBodies);
        json += '}';
    }
    json += "]}";
    return json;
}

} // namespace app::trace

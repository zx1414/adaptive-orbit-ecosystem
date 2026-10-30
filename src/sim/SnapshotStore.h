#pragma once

#include "core/Types.h"
#include "sim/Snapshot.h"

#include <memory>
#include <string>

namespace sim {
class Simulation;

// Binary snapshot persistence. The format is defined in docs/M3-PLAN.md; this
// class is the only code allowed to touch the private simulation state needed
// to reconstruct an exact continuation.
class SnapshotStore final {
public:
    // Save a complete snapshot. Returns false and fills error on I/O or
    // encoding failure.
    static bool save(const Simulation& simulation, const std::string& path, std::string& error);

    // Load a complete snapshot. When overrideTicks is true, maxTicks replaces
    // the stored world.maxTicks. Returns nullptr and fills error on failure; `status`
    // reports whether the failure was I/O (exit 4) or format/semantic (exit 5).
    static std::unique_ptr<Simulation> load(const std::string& path, bool overrideTicks,
                                            core::Tick maxTicks, std::string& error,
                                            snapshot::LoadStatus& status);
};
} // namespace sim

#include "TestHarness.h"

#include "colony/Colony.h"
#include "sim/Simulation.h"
#include "sim/Snapshot.h"
#include "sim/SnapshotStore.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
core::SimConfig testConfig() {
    core::SimConfig config;
    config.world.maxTicks = 1000;
    // Environment and colony both populated so the snapshot has real content to carry.
    config.cyano.initial = 40;
    config.cyano.maxNodes = 400;
    config.cyano.photoRate = 1.0;
    config.cyano.metabolism = 0.0005;
    config.cyano.reproThreshold = 200.0;
    config.cyano.reproProbability = 0.01;
    config.colony.initialNuclei = 4;
    config.colony.maxNuclei = 20;
    config.colony.initialEnergy = 120.0;
    config.colony.initialBallsPerNucleus = 3.0;
    config.colony.deployCost = 5.0;
    return config;
}
std::string pathFor(const char* name) {
    std::error_code ignored;
    std::filesystem::create_directories("build", ignored);
    return (std::filesystem::path("build") / (std::string("m5_snap_") + name + ".snap")).string();
}
void copyFile(const std::string& from, const std::string& to) {
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
}
} // namespace

TEST("snapshot save/load preserves the colony simulation state") {
    const core::SimConfig config = testConfig();
    sim::Simulation original(config);
    for (int i = 0; i < 300; ++i) original.step();
    const std::string path = pathFor("roundtrip");
    std::string error;
    CHECK(sim::SnapshotStore::save(original, path, error));

    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::FormatError;
    std::unique_ptr<sim::Simulation> loaded =
        sim::SnapshotStore::load(path, false, 0, error, status);
    CHECK(loaded != nullptr);
    CHECK(status == sim::snapshot::LoadStatus::Ok);
    if (!loaded) return;
    CHECK(loaded->tick() == original.tick());
    CHECK(loaded->stateHash() == original.stateHash());
    CHECK(loaded->auditOk());
    CHECK(loaded->view().stats.nuclei == original.view().stats.nuclei);
    CHECK(loaded->view().stats.balls == original.view().stats.balls);
    CHECK(loaded->view().stats.cyanobacteria == original.view().stats.cyanobacteria);
    CHECK_NEAR(loaded->totalEnergy(), original.totalEnergy(), 0.0);
    CHECK_NEAR(loaded->ledger().stats().injected, original.ledger().stats().injected, 0.0);

    for (int i = 0; i < 80; ++i) {
        original.step();
        loaded->step();
    }
    CHECK(loaded->stateHash() == original.stateHash());
    CHECK(loaded->auditOk());
    std::filesystem::remove(path);
}

TEST("snapshot split run matches uninterrupted run") {
    const core::SimConfig config = testConfig();
    sim::Simulation direct(config);
    for (int i = 0; i < 600; ++i) direct.step();
    const std::uint64_t directHash = direct.stateHash();

    sim::Simulation split(config);
    for (int i = 0; i < 300; ++i) split.step();
    const std::string path = pathFor("split");
    std::string error;
    CHECK(sim::SnapshotStore::save(split, path, error));
    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::FormatError;
    std::unique_ptr<sim::Simulation> resumed =
        sim::SnapshotStore::load(path, false, 0, error, status);
    CHECK(resumed != nullptr);
    if (!resumed) return;
    for (int i = 0; i < 300; ++i) resumed->step();
    CHECK(resumed->stateHash() == directHash);
    CHECK(resumed->auditOk());
    std::filesystem::remove(path);
}

TEST("snapshot at tick zero reloads without re-injecting initial energy") {
    const core::SimConfig config = testConfig();
    sim::Simulation original(config);
    const std::string path = pathFor("tick0");
    std::string error;
    CHECK(sim::SnapshotStore::save(original, path, error));
    const double injected = original.ledger().stats().injected;

    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::FormatError;
    std::unique_ptr<sim::Simulation> loaded =
        sim::SnapshotStore::load(path, false, 0, error, status);
    CHECK(loaded != nullptr);
    if (loaded) {
        CHECK(loaded->tick() == 0);
        CHECK_NEAR(loaded->ledger().stats().injected, injected, 0.0);
        CHECK(loaded->stateHash() == original.stateHash());
    }
    std::filesystem::remove(path);
}

TEST("snapshot rejects truncated, corrupted and missing files with the right status") {
    const core::SimConfig config = testConfig();
    sim::Simulation original(config);
    for (int i = 0; i < 60; ++i) original.step();
    const std::string path = pathFor("corrupt");
    std::string error;
    CHECK(sim::SnapshotStore::save(original, path, error));

    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::Ok;
    const std::string truncated = pathFor("truncated");
    copyFile(path, truncated);
    {
        std::ifstream in(truncated, std::ios::binary | std::ios::ate);
        const std::streamoff size = in.tellg();
        in.close();
        std::filesystem::resize_file(truncated, static_cast<std::uintmax_t>(size) - 1);
    }
    CHECK(sim::SnapshotStore::load(truncated, false, 0, error, status) == nullptr);
    CHECK(status == sim::snapshot::LoadStatus::FormatError);

    const std::string badMagic = pathFor("badmagic");
    copyFile(path, badMagic);
    {
        std::fstream file(badMagic, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(0);
        file.put('X');
    }
    CHECK(sim::SnapshotStore::load(badMagic, false, 0, error, status) == nullptr);
    CHECK(status == sim::snapshot::LoadStatus::FormatError);

    // A missing path is an I/O problem, not a format problem.
    CHECK(sim::SnapshotStore::load(pathFor("absent"), false, 0, error, status) == nullptr);
    CHECK(status == sim::snapshot::LoadStatus::IoError);

    std::filesystem::remove(path);
    std::filesystem::remove(truncated);
    std::filesystem::remove(badMagic);
}

TEST("snapshot load can override max ticks") {
    const core::SimConfig config = testConfig();
    sim::Simulation original(config);
    for (int i = 0; i < 30; ++i) original.step();
    const std::string path = pathFor("ticks");
    std::string error;
    CHECK(sim::SnapshotStore::save(original, path, error));
    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::FormatError;
    std::unique_ptr<sim::Simulation> loaded =
        sim::SnapshotStore::load(path, true, 4321, error, status);
    CHECK(loaded != nullptr);
    if (loaded) {
        CHECK(loaded->config().world.maxTicks == 4321);
        CHECK(loaded->tick() == 30);
    }
    std::filesystem::remove(path);
}

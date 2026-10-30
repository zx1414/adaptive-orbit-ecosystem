#include "TestHarness.h"

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
    config.producer.initial = 30;
    config.agent.initial = 12;
    config.producer.max = 1000.0;
    config.agent.max = 1000.0;
    config.producer.competition = 0.2;
    config.producer.interactionRadius = 400.0;
    config.producer.densityRepro = 0.02;
    config.agent.harvest = 0.5;
    config.agent.harvestReserve = 10.0;
    config.agent.harvestHalfSaturation = 20.0;
    config.agent.densityRepro = 0.01;
    config.agent.densityRadius = 200.0;
    config.agent.moveK = 0.00002;
    config.agent.lifespan = 3000;
    return config;
}
std::string pathFor(const char* name) {
    std::error_code ignored;
    std::filesystem::create_directories("build", ignored);
    return (std::filesystem::path("build") / (std::string("m3_test_") + name + ".snap")).string();
}
void copyFile(const std::string& from, const std::string& to) {
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
}
std::vector<std::uint8_t> readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    std::vector<std::uint8_t> file;
    if (!in) return file;
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    file.resize(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return file;
}
void writeAll(const std::string& path, const std::vector<std::uint8_t>& file) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
}
std::uint64_t readU64(const std::vector<std::uint8_t>& file, std::size_t offset) {
    std::uint64_t value = 0;
    for (int b = 0; b < 8; ++b) value |= static_cast<std::uint64_t>(file[offset + std::size_t(b)]) << (8 * b);
    return value;
}
void writeU64(std::vector<std::uint8_t>& file, std::size_t offset, std::uint64_t value) {
    for (int b = 0; b < 8; ++b) {
        file[offset + std::size_t(b)] = static_cast<std::uint8_t>((value >> (8 * b)) & 0xFFU);
    }
}
// Re-stamp the header checksum so a patched payload stays structurally valid.
void repairChecksum(std::vector<std::uint8_t>& file) {
    const std::uint64_t checksum = sim::snapshot::fnv1a(file.data() + 32, file.size() - 32);
    writeU64(file, 24, checksum);
}
} // namespace

TEST("snapshot save/load preserves complete simulation state") {
    const core::SimConfig config = testConfig();
    sim::Simulation original(config);
    for (int i = 0; i < 500; ++i) original.step();
    const std::string path = pathFor("roundtrip");
    std::string error;
    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::FormatError;
    CHECK(sim::SnapshotStore::save(original, path, error));
    CHECK(error.empty());

    std::unique_ptr<sim::Simulation> loaded =
        sim::SnapshotStore::load(path, false, 0, error, status);
    CHECK(loaded != nullptr);
    CHECK(status == sim::snapshot::LoadStatus::Ok);
    CHECK(error.empty());
    if (!loaded) return;
    CHECK(loaded->tick() == original.tick());
    CHECK(loaded->stateHash() == original.stateHash());
    CHECK(loaded->auditOk());
    CHECK(loaded->view().stats.producers == original.view().stats.producers);
    CHECK(loaded->view().stats.agents == original.view().stats.agents);
    CHECK_NEAR(loaded->totalEnergy(), original.totalEnergy(), 0.0);
    CHECK_NEAR(loaded->ledger().stats().injected, original.ledger().stats().injected, 0.0);
    CHECK_NEAR(loaded->ledger().stats().dissipated, original.ledger().stats().dissipated, 0.0);
    CHECK_NEAR(loaded->ledger().stats().transferred, original.ledger().stats().transferred, 0.0);
    CHECK(loaded->producerSystem().nextId() == original.producerSystem().nextId());
    CHECK(loaded->agentSystem().nextId() == original.agentSystem().nextId());
    CHECK(loaded->producerSystem().births() == original.producerSystem().births());
    CHECK(loaded->agentSystem().deaths() == original.agentSystem().deaths());

    for (int i = 0; i < 100; ++i) {
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
    for (int i = 0; i < 1000; ++i) direct.step();
    const std::uint64_t directHash = direct.stateHash();

    sim::Simulation split(config);
    for (int i = 0; i < 500; ++i) split.step();
    const std::string path = pathFor("split");
    std::string error;
    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::FormatError;
    CHECK(sim::SnapshotStore::save(split, path, error));
    std::unique_ptr<sim::Simulation> resumed =
        sim::SnapshotStore::load(path, false, 0, error, status);
    CHECK(resumed != nullptr);
    CHECK(status == sim::snapshot::LoadStatus::Ok);
    if (!resumed) return;
    for (int i = 0; i < 500; ++i) resumed->step();

    CHECK(resumed->tick() == direct.tick());
    CHECK(resumed->stateHash() == direct.stateHash());
    CHECK(resumed->stateHash() == directHash);
    CHECK(resumed->auditOk());
    CHECK(resumed->view().stats.producers == direct.view().stats.producers);
    CHECK(resumed->view().stats.agents == direct.view().stats.agents);
    CHECK_NEAR(resumed->totalEnergy(), direct.totalEnergy(), 0.0);
    CHECK_NEAR(resumed->ledger().stats().injected, direct.ledger().stats().injected, 0.0);
    CHECK_NEAR(resumed->ledger().stats().dissipated, direct.ledger().stats().dissipated, 0.0);
    std::filesystem::remove(path);
}

TEST("snapshot at tick zero loads without injecting initial energy") {
    const core::SimConfig config = testConfig();
    sim::Simulation original(config);
    const std::string path = pathFor("tick0");
    std::string error;
    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::FormatError;
    CHECK(sim::SnapshotStore::save(original, path, error));
    const double injected = original.ledger().stats().injected;
    std::unique_ptr<sim::Simulation> loaded =
        sim::SnapshotStore::load(path, false, 0, error, status);
    CHECK(loaded != nullptr);
    CHECK(status == sim::snapshot::LoadStatus::Ok);
    if (loaded) {
        CHECK(loaded->tick() == 0);
        CHECK_NEAR(loaded->ledger().stats().injected, injected, 0.0);
        CHECK(loaded->ledger().stats().injectedInitial() == injected);
        CHECK(loaded->stateHash() == original.stateHash());
        CHECK(loaded->auditOk());
    }
    std::filesystem::remove(path);
}

TEST("snapshot rejects truncated and corrupted files") {
    const core::SimConfig config = testConfig();
    sim::Simulation original(config);
    for (int i = 0; i < 50; ++i) original.step();
    const std::string path = pathFor("corrupt");
    std::string error;
    CHECK(sim::SnapshotStore::save(original, path, error));

    const std::string truncated = pathFor("truncated");
    copyFile(path, truncated);
    {
        std::ifstream in(truncated, std::ios::binary | std::ios::ate);
        const std::streamoff size = in.tellg();
        in.close();
        std::filesystem::resize_file(truncated, static_cast<std::uintmax_t>(size) - 1);
    }
    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::Ok;
    CHECK(sim::SnapshotStore::load(truncated, false, 0, error, status) == nullptr);
    CHECK(status == sim::snapshot::LoadStatus::FormatError);
    CHECK(!error.empty());

    const std::string badMagic = pathFor("badmagic");
    copyFile(path, badMagic);
    {
        std::fstream file(badMagic, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(0);
        file.put('X');
    }
    CHECK(sim::SnapshotStore::load(badMagic, false, 0, error, status) == nullptr);
    CHECK(status == sim::snapshot::LoadStatus::FormatError);

    const std::string badVersion = pathFor("badversion");
    copyFile(path, badVersion);
    {
        std::fstream file(badVersion, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(8);
        file.put(static_cast<char>(2));
    }
    CHECK(sim::SnapshotStore::load(badVersion, false, 0, error, status) == nullptr);
    CHECK(status == sim::snapshot::LoadStatus::FormatError);

    // A path that does not exist is an I/O failure, and the CLI maps that to exit code 4.
    CHECK(sim::SnapshotStore::load(pathFor("does_not_exist"), false, 0, error, status) == nullptr);
    CHECK(status == sim::snapshot::LoadStatus::IoError);

    std::filesystem::remove(path);
    std::filesystem::remove(truncated);
    std::filesystem::remove(badMagic);
    std::filesystem::remove(badVersion);
}

TEST("snapshot rejects a checksum-valid file with an even RNG increment") {
    const core::SimConfig config = testConfig();
    sim::Simulation original(config);
    for (int i = 0; i < 20; ++i) original.step();
    const std::string path = pathFor("even_increment");
    std::string error;
    CHECK(sim::SnapshotStore::save(original, path, error));

    std::vector<std::uint8_t> file = readAll(path);
    CHECK(file.size() > 32);
    if (file.size() <= 32) return;

    // PCG32 default sequence 54 gives increment (54 << 1) | 1. Patch it to an even value and
    // repair the checksum: only semantic validation can reject this file now.
    const std::uint64_t increment = (54ULL << 1U) | 1U;
    std::size_t found = 0;
    bool patched = false;
    for (std::size_t i = 32; i + 8 <= file.size(); ++i) {
        if (readU64(file, i) == increment) { found = i; patched = true; break; }
    }
    CHECK(patched);
    if (!patched) { std::filesystem::remove(path); return; }
    writeU64(file, found, 2ULL);
    repairChecksum(file);
    writeAll(path, file);

    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::Ok;
    std::unique_ptr<sim::Simulation> loaded =
        sim::SnapshotStore::load(path, false, 0, error, status);
    // A bad RNG state must be reported as a snapshot error, never thrown as an exception.
    CHECK(loaded == nullptr);
    CHECK(status == sim::snapshot::LoadStatus::FormatError);
    CHECK(error.find("increment") != std::string::npos);
    std::filesystem::remove(path);
}

TEST("state hash covers configuration that changes resumed dynamics") {
    core::SimConfig a = testConfig();
    core::SimConfig b = testConfig();
    b.agent.metab = a.agent.metab + 0.001;
    b.agent.harvestHalfSaturation = a.agent.harvestHalfSaturation + 5.0;
    b.producer.competition = a.producer.competition + 0.1;
    sim::Simulation left(a);
    sim::Simulation right(b);
    // At tick 0 the entities are identical, so only the configuration can distinguish them.
    CHECK(left.stateHash() != right.stateHash());
}

TEST("zero tick budget performs no step") {
    core::SimConfig config = testConfig();
    config.world.maxTicks = 0;
    sim::Simulation zero(config);
    CHECK(zero.finished());
    CHECK(zero.tick() == 0);
    CHECK(zero.view().stats.tick == 0);
    const double injected = zero.ledger().stats().injected;
    CHECK_NEAR(zero.ledger().stats().dissipated, 0.0, 0.0);
    zero.step();
    CHECK(zero.tick() == 0);
    CHECK_NEAR(zero.ledger().stats().injected, injected, 0.0);
}

TEST("snapshot at max ticks stays finished unless overridden") {
    core::SimConfig config = testConfig();
    config.world.maxTicks = 50;
    sim::Simulation original(config);
    while (!original.finished()) original.step();
    const std::string path = pathFor("finished");
    std::string error;
    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::FormatError;
    CHECK(sim::SnapshotStore::save(original, path, error));
    std::unique_ptr<sim::Simulation> loaded =
        sim::SnapshotStore::load(path, false, 0, error, status);
    CHECK(loaded != nullptr);
    if (loaded) {
        CHECK(loaded->tick() == 50);
        CHECK(loaded->finished());
    }
    std::unique_ptr<sim::Simulation> reopened =
        sim::SnapshotStore::load(path, true, 100, error, status);
    CHECK(reopened != nullptr);
    if (reopened) {
        CHECK(!reopened->finished());
        reopened->step();
        CHECK(reopened->tick() == 51);
    }
    std::filesystem::remove(path);
}

TEST("snapshot load can override max ticks") {
    const core::SimConfig config = testConfig();
    sim::Simulation original(config);
    for (int i = 0; i < 20; ++i) original.step();
    const std::string path = pathFor("ticks");
    std::string error;
    sim::snapshot::LoadStatus status = sim::snapshot::LoadStatus::FormatError;
    CHECK(sim::SnapshotStore::save(original, path, error));
    std::unique_ptr<sim::Simulation> loaded =
        sim::SnapshotStore::load(path, true, 1234, error, status);
    CHECK(loaded != nullptr);
    if (loaded) {
        CHECK(loaded->config().world.maxTicks == 1234);
        CHECK(loaded->tick() == 20);
    }
    std::filesystem::remove(path);
}

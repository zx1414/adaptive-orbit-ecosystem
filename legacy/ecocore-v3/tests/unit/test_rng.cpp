// agentsim - tests/unit/test_rng.cpp
// S2 基础：PCG32 固定种子复现、不同种子序列不同、快照恢复。
#include "test_framework.h"

#include <cstdint>

#include "../../src/core/PCG32.h"

namespace {
bool sameSeq(agentsim::PCG32 a, agentsim::PCG32 b, int n) {
    for (int i = 0; i < n; ++i) {
        if (a.nextUint32() != b.nextUint32()) return false;
    }
    return true;
}
} // namespace

TEST(rng_fixed_seed_reproducible) {
    agentsim::PCG32 a(42), b(42);
    for (int i = 0; i < 1000; ++i) {
        CHECK(a.nextUint32() == b.nextUint32());
    }
    agentsim::PCG32 c(42), d(42);
    for (int i = 0; i < 500; ++i) {
        CHECK_NEAR(c.nextDouble(), d.nextDouble(), 0.0); // 逐位一致
    }
}

TEST(rng_different_seeds_differ) {
    agentsim::PCG32 a(42), b(43);
    int diffs = 0;
    for (int i = 0; i < 64; ++i) {
        if (a.nextUint32() != b.nextUint32()) ++diffs;
    }
    CHECK(diffs > 60); // 不同种子序列应显著不同
    agentsim::PCG32 s1(7), s2(99);
    diffs = 0;
    for (int i = 0; i < 64; ++i) {
        if (s1.nextDouble() != s2.nextDouble()) ++diffs;
    }
    CHECK(diffs > 60);
}

TEST(rng_snapshot_state_roundtrip) {
    agentsim::PCG32 a(42);
    for (int i = 0; i < 10; ++i) a.nextUint32();
    std::uint64_t st, inc;
    a.getState(st, inc);
    agentsim::PCG32 b;
    b.setState(st, inc);
    CHECK(sameSeq(a, b, 100)); // 快照恢复后序列完全一致
}

TEST(rng_double_in_unit_range) {
    agentsim::PCG32 r(2026);
    for (int i = 0; i < 500; ++i) {
        const double v = r.nextDouble();
        CHECK(v >= 0.0);
        CHECK(v < 1.0);
    }
}

TEST(rng_set_state_resumes_midstream) {
    // 双机同行 250 步后快照/恢复，剩余 250 步与参照机完全一致。
    agentsim::PCG32 r(2026), ref(2026);
    for (int i = 0; i < 250; ++i) {
        r.nextUint32();
        ref.nextUint32();
    }
    std::uint64_t st, inc;
    r.getState(st, inc);
    agentsim::PCG32 half;
    half.setState(st, inc);
    for (int i = 0; i < 250; ++i) {
        CHECK(half.nextUint32() == ref.nextUint32());
    }
}

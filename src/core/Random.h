#pragma once

#include <cstdint>
#include <limits>

namespace core {

struct RandomState {
    std::uint64_t state = 0;
    std::uint64_t increment = 0;
    double cachedNormal = 0.0;
    bool hasCachedNormal = false;
};

class PCG32 {
public:
    using result_type = std::uint32_t;
    using State = RandomState;

    explicit PCG32(std::uint64_t seed = 42, std::uint64_t sequence = 54);

    static constexpr result_type min() { return std::numeric_limits<result_type>::min(); }
    static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }

    result_type operator()() { return next(); }
    result_type next();
    double uniformReal(double minimum = 0.0, double maximum = 1.0);
    int uniformInt(int minimum, int maximum);
    std::size_t uniformIndex(std::size_t size);
    double normal(double mean = 0.0, double standardDeviation = 1.0);

    void seed(std::uint64_t seedValue, std::uint64_t sequence = 54);
    State state() const { return state_; }
    State exportState() const { return state_; }
    void setState(const State& state);
    void importState(const State& state) { setState(state); }

private:
    std::uint32_t bounded(std::uint32_t bound);
    State state_{};
};

using Random = PCG32;
using RandomGenerator = PCG32;

} // namespace core

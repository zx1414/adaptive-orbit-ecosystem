#include "core/Random.h"
#include <cmath>
#include <stdexcept>

namespace core {
namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

PCG32::PCG32(std::uint64_t seedValue, std::uint64_t sequence) {
    seed(seedValue, sequence);
}

void PCG32::seed(std::uint64_t seedValue, std::uint64_t sequence) {
    state_ = {};
    state_.increment = (sequence << 1u) | 1u;
    state_.state = 0u;
    next();
    state_.state += seedValue;
    next();
}

std::uint32_t PCG32::next() {
    const std::uint64_t oldState = state_.state;
    state_.state = oldState * 6364136223846793005ULL + state_.increment;
    const std::uint32_t xorshifted = static_cast<std::uint32_t>(
        ((oldState >> 18u) ^ oldState) >> 27u);
    const std::uint32_t rot = static_cast<std::uint32_t>(oldState >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-static_cast<int>(rot)) & 31));
}

std::uint32_t PCG32::bounded(std::uint32_t bound) {
    if (bound == 0u) return 0u;
    const std::uint32_t threshold = (0u - bound) % bound;
    for (;;) {
        const std::uint32_t value = next();
        if (value >= threshold) return value % bound;
    }
}

double PCG32::uniformReal(double minimum, double maximum) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) return 0.0;
    if (maximum < minimum) std::swap(minimum, maximum);
    const std::uint64_t high = static_cast<std::uint64_t>(next()) << 32u;
    const std::uint64_t value = high | static_cast<std::uint64_t>(next());
    const double unit = std::ldexp(static_cast<double>(value >> 11u), -53);
    return minimum + (maximum - minimum) * unit;
}

int PCG32::uniformInt(int minimum, int maximum) {
    if (maximum < minimum) std::swap(minimum, maximum);
    const std::uint64_t span = static_cast<std::uint64_t>(maximum) -
                               static_cast<std::uint64_t>(minimum) + 1u;
    if (span == 0u) return static_cast<int>(next());
    const std::uint32_t bound = static_cast<std::uint32_t>(
        span > 0xffffffffULL ? 0xffffffffULL : span);
    return minimum + static_cast<int>(bounded(bound));
}

std::size_t PCG32::uniformIndex(std::size_t size) {
    if (size == 0) return 0;
    return static_cast<std::size_t>(bounded(static_cast<std::uint32_t>(size)));
}

double PCG32::normal(double mean, double standardDeviation) {
    if (!std::isfinite(mean) || !std::isfinite(standardDeviation)) return mean;
    if (standardDeviation < 0.0) standardDeviation = -standardDeviation;
    if (state_.hasCachedNormal) {
        state_.hasCachedNormal = false;
        return mean + standardDeviation * state_.cachedNormal;
    }
    double u1 = 0.0;
    double u2 = 0.0;
    do { u1 = uniformReal(0.0, 1.0); } while (u1 <= 0.0);
    do { u2 = uniformReal(0.0, 1.0); } while (u2 <= 0.0);
    const double radius = std::sqrt(-2.0 * std::log(u1));
    const double angle = kTwoPi * u2;
    state_.cachedNormal = radius * std::sin(angle);
    state_.hasCachedNormal = true;
    return mean + standardDeviation * radius * std::cos(angle);
}

void PCG32::setState(const State& state) {
    if (state.increment == 0u || (state.increment & 1u) == 0u) {
        throw std::invalid_argument("PCG32 increment must be odd");
    }
    if (!std::isfinite(state.cachedNormal)) {
        throw std::invalid_argument("PCG32 cached normal must be finite");
    }
    state_ = state;
}

} // namespace core

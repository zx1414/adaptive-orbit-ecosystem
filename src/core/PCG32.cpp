// agentsim - core/PCG32.cpp
#include "PCG32.h"

namespace agentsim {

PCG32::PCG32(std::uint64_t seed, std::uint64_t seq)
    : state_(0), inc_((seq << 1u) | 1u) {
    nextUint32();
    state_ += seed;
    nextUint32();
}

std::uint32_t PCG32::nextUint32() {
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + inc_;
    const std::uint32_t xorshifted =
        static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const std::uint32_t rot = static_cast<std::uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

double PCG32::nextDouble() {
    const std::uint64_t a = static_cast<std::uint64_t>(nextUint32()) >> 5u;  // 27 位
    const std::uint64_t b = static_cast<std::uint64_t>(nextUint32()) >> 6u;  // 26 位
    return static_cast<double>((a << 26u) | b) * (1.0 / 9007199254740992.0); // 2^53
}

void PCG32::setState(std::uint64_t state, std::uint64_t inc) {
    state_ = state;
    inc_ = inc;
}

void PCG32::getState(std::uint64_t& state, std::uint64_t& inc) const {
    state = state_;
    inc = inc_;
}

} // namespace agentsim

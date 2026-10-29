#pragma once
#include "core/Random.h"
#include "decision/Action.h"
#include "decision/Observation.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace decision {
struct PolicySpec {
    std::string id = "rule";
    std::uint32_t observationVersion = 1;
    std::uint32_t observationDim = Observation::kDim;
    std::uint32_t actionVersion = 1;
    std::uint32_t genomeDim = 0;
};

class IPolicy {
public:
    virtual ~IPolicy() = default;
    virtual PolicySpec spec() const = 0;
    virtual Action decide(const Observation& observation) = 0;
    virtual std::unique_ptr<IPolicy> clone() const = 0;
    virtual void mutate(core::Random& random, float rate) = 0;
    virtual std::vector<float> genome() const = 0;
    virtual void setGenome(const float* data, std::size_t size) = 0;
    // Optional opaque state for policies with recurrent/normalization state.
    // The default accepts only an empty blob; NNetPolicy can override.
    virtual void serializeState(std::vector<unsigned char>& out) const { out.clear(); }
    virtual bool deserializeState(const unsigned char* data, std::size_t size) {
        (void)data;
        return size == 0;
    }
};
} // namespace decision
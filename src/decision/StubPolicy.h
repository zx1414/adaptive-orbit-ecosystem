#pragma once
#include "decision/IPolicy.h"

namespace decision {
class StubPolicy final : public IPolicy {
public:
    PolicySpec spec() const override;
    Action decide(const Observation& observation) override;
    std::unique_ptr<IPolicy> clone() const override;
    void mutate(core::Random& random, float rate) override;
    std::vector<float> genome() const override;
    void setGenome(const float* data, std::size_t size) override;
};
} // namespace decision

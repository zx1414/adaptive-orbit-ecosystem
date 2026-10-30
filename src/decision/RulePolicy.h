#pragma once
#include "decision/IPolicy.h"
#include <array>

namespace decision {
class RulePolicy final : public IPolicy {
public:
    enum Gene : std::size_t {
        RestBelow = 0, SpeedPreference, HarvestBias,
        WanderBias, StoreBias, RivalAvoid, GeneCount
    };
    RulePolicy();
    PolicySpec spec() const override;
    Action decide(const Observation& observation) override;
    std::unique_ptr<IPolicy> clone() const override;
    void mutate(core::Random& random, float rate) override;
    std::vector<float> genome() const override;
    void setGenome(const float* data, std::size_t size) override;
    float gene(std::size_t index) const { return genes_[index]; }
    void setGene(std::size_t index, float value);
private:
    std::array<float, GeneCount> genes_{};
};
} // namespace decision

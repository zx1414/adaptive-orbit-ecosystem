#include "decision/PolicyRegistry.h"

#include "decision/BanditPolicy.h"
#include "decision/ForageBankPolicy.h"
#include "decision/HysteresisPolicy.h"
#include "decision/PerceptronPolicy.h"
#include "decision/RiskAversePolicy.h"
#include "decision/RulePolicy.h"
#include "decision/ThermalPolicy.h"

namespace agentsim {
namespace {

template<typename Policy>
std::unique_ptr<IDecisionPolicy> plain(const PolicyConfig&) {
    return std::make_unique<Policy>();
}

std::unique_ptr<IDecisionPolicy> thermal(const PolicyConfig& cfg) {
    ThermalPolicy::Params params;
    params.preferredTemp = cfg.thermalPreferredTemp;
    params.wThermal = cfg.thermalWThermal;
    params.wCaution = cfg.thermalWCaution;
    return std::make_unique<ThermalPolicy>(params);
}

std::unique_ptr<IDecisionPolicy> perceptron(const PolicyConfig& cfg) {
    if (!cfg.modelWeights.empty()) {
        return std::make_unique<PerceptronPolicy>(PerceptronPolicy::deserialize(cfg.modelWeights));
    }
    return std::make_unique<PerceptronPolicy>(cfg.perceptronSeed, cfg.perceptronScale,
                                              cfg.legacyPerceptron);
}

const PolicyDef* findPolicy(std::string_view id) {
    for (const auto& policy : policies()) {
        if (id == policy.id) return &policy;
    }
    return nullptr;
}

} // namespace

const std::vector<PolicyDef>& policies() {
    static const std::vector<PolicyDef> entries{
        {"grazer", u8"孢子巡游与慢速摄食", true, plain<GrazerPolicy>},
        {"rule", u8"规则", true, plain<RulePolicy>},
        {"thermal", u8"热感知", true, thermal},
        {"hysteresis", u8"迟滞", true, plain<HysteresisPolicy>},
        {"foragebank", u8"觅食储蓄", true, plain<ForageBankPolicy>},
        {"bandit", u8"多臂老虎机", true, plain<BanditPolicy>},
        {"riskaverse", u8"风险规避", true, plain<RiskAversePolicy>},
        {"perceptron", u8"感知机", true, perceptron},
        {"stub", "Stub", false, plain<StubPolicy>},
    };
    return entries;
}

bool selectPolicy(std::string_view id, PolicyConfig& cfg) {
    const auto* policy = findPolicy(id);
    if (!policy) return false;
    cfg.name = policy->id;
    cfg.usePerceptron = id == "perceptron";
    cfg.useThermal = id == "thermal";
    cfg.mutation = id == "rule" ? 0.05 : 0.0;
    cfg.modelId.clear();
    cfg.modelWeights.clear();
    cfg.legacyPerceptron = false;
    return true;
}

std::unique_ptr<IDecisionPolicy> makePolicy(const PolicyConfig& cfg) {
    // These flags appeared in saved configurations before the registry.
    if (cfg.usePerceptron) return perceptron(cfg);
    if (cfg.name == "stub") return plain<StubPolicy>(cfg);
    if (cfg.useThermal) return thermal(cfg);
    const auto* policy = findPolicy(cfg.name);
    return policy ? policy->create(cfg) : plain<RulePolicy>(cfg);
}

} // namespace agentsim

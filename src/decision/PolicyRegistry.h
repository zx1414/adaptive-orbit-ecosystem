#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "core/Config.h"
#include "decision/IDecisionPolicy.h"

namespace agentsim {

struct PolicyDef {
    const char* id;
    const char* label;
    bool visible;
    std::unique_ptr<IDecisionPolicy> (*create)(const PolicyConfig&);
};

const std::vector<PolicyDef>& policies();
bool selectPolicy(std::string_view id, PolicyConfig& cfg);
std::unique_ptr<IDecisionPolicy> makePolicy(const PolicyConfig& cfg);

} // namespace agentsim

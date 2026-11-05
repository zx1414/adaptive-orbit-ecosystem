#pragma once
#include "decision/IPolicy.h"
#include <functional>
#include <string>
#include <vector>

namespace decision {
class PolicyRegistry {
public:
    using Factory = std::function<std::unique_ptr<IPolicy>()>;
    static bool add(const std::string& id, Factory factory);
    static std::unique_ptr<IPolicy> create(const std::string& id);
    static std::vector<std::string> available();
};
} // namespace decision

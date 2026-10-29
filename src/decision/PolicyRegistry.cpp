#include "decision/PolicyRegistry.h"
#include "decision/RulePolicy.h"
#include "decision/StubPolicy.h"
#include <map>

namespace decision {
namespace {
std::map<std::string, PolicyRegistry::Factory>& factories() {
    static std::map<std::string, PolicyRegistry::Factory> table;
    return table;
}
bool ensureBuiltins() {
    auto& table = factories();
    if (table.empty()) {
        table["rule"] = [] { return std::make_unique<RulePolicy>(); };
        table["stub"] = [] { return std::make_unique<StubPolicy>(); };
    }
    return true;
}
}
bool PolicyRegistry::add(const std::string& id, Factory factory) {
    if (id.empty() || !factory) return false;
    ensureBuiltins();
    return factories().emplace(id, std::move(factory)).second;
}
std::unique_ptr<IPolicy> PolicyRegistry::create(const std::string& id) {
    ensureBuiltins();
    const auto it = factories().find(id);
    return it == factories().end() ? nullptr : it->second();
}
std::vector<std::string> PolicyRegistry::available() {
    ensureBuiltins();
    std::vector<std::string> ids;
    for (const auto& entry : factories()) ids.push_back(entry.first);
    return ids;
}
} // namespace decision

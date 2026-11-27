#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/Config.h"

namespace agentsim::app {

struct ScenarioEntry {
    ScenarioConfig config;
    std::string label;
};

struct ModelEntry {
    std::string id;
    std::string weights;
};

struct ExtensionCatalog {
    std::vector<ScenarioEntry> scenarios;
    std::vector<ModelEntry> models;
    std::vector<std::string> errors;

    static ExtensionCatalog load(const std::filesystem::path& root);
    bool selectScenario(const std::string& id, ScenarioConfig& config) const;
    bool selectDecision(const std::string& id, PolicyConfig& config) const;
};

inline bool validExtensionId(const std::string& id) {
    return !id.empty() && id.size() <= 48 &&
           id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos;
}

} // namespace agentsim::app

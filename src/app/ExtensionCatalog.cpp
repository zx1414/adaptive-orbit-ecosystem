#include "app/ExtensionCatalog.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <locale>
#include <sstream>
#include <string_view>
#include <unordered_set>

#include "decision/PerceptronPolicy.h"
#include "decision/PolicyRegistry.h"

namespace agentsim::app {
namespace {

constexpr std::size_t kMaxFileBytes = 16384;

std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return std::string(value.substr(first, value.find_last_not_of(" \t\r") - first + 1));
}

bool readText(const std::filesystem::path& path, std::string& result) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string text(kMaxFileBytes + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!in.eof() || in.bad()) return false;
    text.resize(static_cast<std::size_t>(in.gcount()));
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    result = std::move(text);
    return true;
}

bool number(const std::string& text, double min, double max, double& result) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    double value = 0.0;
    if (!(in >> value) || !std::isfinite(value) || value < min || value > max) return false;
    in >> std::ws;
    if (!in.eof()) return false;
    result = value;
    return true;
}

bool readScenario(const std::filesystem::path& path, ScenarioEntry& scenario) {
    std::string text;
    if (!readText(path, text)) return false;
    std::istringstream in(text);
    std::unordered_set<std::string> seen;
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) return false;
        const auto key = trim(std::string_view(line).substr(0, equals));
        const auto value = trim(std::string_view(line).substr(equals + 1));
        if (!seen.insert(key).second) return false;
        if (key == "version" && value == "1") continue;
        if (key == "name" && !value.empty() && value.size() <= 120) {
            scenario.label = value;
        } else if (key == "day_cycle" && (value == "on" || value == "off")) {
            scenario.config.dayCycle = value == "on";
        } else if (key == "initial_nutrient_multiplier") {
            if (!number(value, 0.0, 20.0, scenario.config.initialNutrientMultiplier)) return false;
        } else if (key == "nutrient_diffusion") {
            if (!number(value, 0.0, 1.0, scenario.config.nutrientDiffusion)) return false;
        } else {
            return false;
        }
    }
    return seen.count("version") != 0 && !scenario.label.empty();
}

std::vector<std::filesystem::path> files(const std::filesystem::path& dir, const char* extension) {
    std::error_code error;
    std::vector<std::filesystem::path> paths;
    const std::filesystem::directory_iterator end;
    for (std::filesystem::directory_iterator it(dir, error); !error && it != end;
         it.increment(error)) {
        std::error_code typeError;
        if (it->path().extension() == extension && it->is_regular_file(typeError))
            paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

} // namespace

ExtensionCatalog ExtensionCatalog::load(const std::filesystem::path& root) {
    ExtensionCatalog catalog;
    catalog.scenarios.push_back({{}, u8"标准生态"});
    for (const auto& path : files(root / "mods", ".txt")) {
        ScenarioEntry entry;
        entry.config.id = path.stem().u8string();
        if (!validExtensionId(entry.config.id) || entry.config.id == "standard" ||
            !readScenario(path, entry)) {
            catalog.errors.push_back(path.filename().u8string() + ": invalid scenario");
            continue;
        }
        catalog.scenarios.push_back(std::move(entry));
    }

    for (const auto& path : files(root / "models", ".txt")) {
        ModelEntry entry;
        entry.id = path.stem().u8string();
        PerceptronPolicy::Params params;
        if (!validExtensionId(entry.id) || !readText(path, entry.weights) ||
            !PerceptronPolicy::readWeights(entry.weights, params) ||
            params.kDim != SensorVector::kDim || params.kOutputs != 2) {
            catalog.errors.push_back(path.filename().u8string() + ": invalid model");
            continue;
        }
        catalog.models.push_back(std::move(entry));
    }
    return catalog;
}

bool ExtensionCatalog::selectScenario(const std::string& id, ScenarioConfig& config) const {
    for (const auto& scenario : scenarios) {
        if (scenario.config.id == id) {
            config = scenario.config;
            return true;
        }
    }
    return false;
}

bool ExtensionCatalog::selectDecision(const std::string& id, PolicyConfig& config) const {
    if (selectPolicy(id, config)) return true;
    for (const auto& model : models) {
        if ("model." + model.id != id) continue;
        selectPolicy("perceptron", config);
        config.modelId = model.id;
        config.modelWeights = model.weights;
        return true;
    }
    return false;
}

} // namespace agentsim::app

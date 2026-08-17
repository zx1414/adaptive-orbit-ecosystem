#include "mods_registry.h"
#include <fstream>
#include <iostream>

// mod 注册函数声明（实现见 mods/ 目录）。
void registerPeriodicDrain(ModAPI&);
void registerPlagueStrike(ModAPI&);

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}  // namespace

const std::vector<ModInfo>& allMods() {
    static const std::vector<ModInfo> mods = {
        {"periodic_drain", "周期性瘟疫",
         "每 100 帧对所有存活核扣 10 能量（示例 mod）",
         "PLAGUE", "", registerPeriodicDrain},
        {"plague_strike", "瘟疫打击",
         "每 50 帧对能量超过 100 的核扣 8 能量（示例 mod）",
         "PLAGUE", "", registerPlagueStrike},
    };
    return mods;
}

std::vector<std::string> readModsList(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream f(path);
    if (!f.is_open()) return out;
    std::string line;
    while (std::getline(f, line)) {
        std::string id = trim(line);
        if (id.empty() || id[0] == '#') continue;
        bool known = false;
        for (const ModInfo& m : allMods()) {
            if (m.id == id) {
                known = true;
                break;
            }
        }
        if (known) {
            out.push_back(id);
        } else {
            std::cerr << "[mods] 未知 mod，已忽略: " << id << "\n";
        }
    }
    return out;
}

void registerAllMods(ModAPI& api, const std::vector<std::string>& enabledOrdered) {
    for (const std::string& id : enabledOrdered) {
        for (const ModInfo& m : allMods()) {
            if (id == m.id) {
                std::cout << "[mods] 注册 " << m.name << "\n";
                m.reg(api);
                break;
            }
        }
    }
}

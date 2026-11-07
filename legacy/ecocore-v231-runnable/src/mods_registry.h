#pragma once
#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include "mod_api.h"

// mod 描述符：名称/说明/影响的机制（touches）/互斥 mod（incompatible）/
// 依赖或前置条件（requires，供启动器显示"需要 XX 才生效"徽章）。
// touches 与 incompatible 为逗号分隔列表；touches 用 EnergyReason 名或配置键名。
// requires 为人类可读短标签（不参与注册逻辑，仅 UI 展示），空串 = 无前置条件。
// saveState / loadState：可选状态存档钩子（占位接口，默认空 = 无内部状态）。
struct ModInfo {
    const char* id;
    const char* name;
    const char* desc;
    const char* touches;
    const char* incompatible;
    const char* requires;
    void (*reg)(ModAPI&);
    void (*saveState)(std::ostream&) = nullptr;
    void (*loadState)(std::istream&) = nullptr;
};

// 全部已编译 mod 的注册表（新增 mod = 这里加一行 + mods/xxx.cpp）。
const std::vector<ModInfo>& allMods();

// 读取 mods.list（每行一个 mod id，顺序 = 优先级）；文件不存在返回空。
std::vector<std::string> readModsList(const std::string& path);

// 按给定顺序注册 mod：顺序 = 事件派发顺序 = 优先级；未知 id 忽略。
void registerAllMods(ModAPI& api, const std::vector<std::string>& enabledOrdered);

// 便捷重载：不启用任何 mod（默认行为与旧版一致）。
inline void registerAllMods(ModAPI& api) { registerAllMods(api, {}); }

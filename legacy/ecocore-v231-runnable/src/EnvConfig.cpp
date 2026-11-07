#include "EnvConfig.h"
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ParamSchema.h"

// ---------------------------------------------------------------------------
// 为什么 EnvConfig::load 不再手写逐键 if-else 链（**不要"顺手"改回去**）
//
// 原实现是一条 `if (key == "width") ... else if (key == "...") ...` 长链。
// 单条 if-else 链的**块嵌套深度**受 MSVC 硬限制：实测 80 个分支可编译，
// 122 个分支即 `fatal error C1061: 编译器限制: 块嵌套太深`（/W4 /O2 直接编译不过）。
// 本轮新增 43 个配置键后越过了阈值。
//
// 现在改为委托 `ParamSchema::applyLine()` —— schema 表（ParamSchema.cpp 的
// buildTable）已经是全部键的**单一事实来源**，且其解析语义与本文件原先的手写链
// 完全一致（Int/Double/Bool/Pair、"lo, hi"、内联 # 注释、on/true/1/yes）。
//
// 收益：
//   1. 根除 C1061（不再有深嵌套）；
//   2. 新增配置键只需登记 2 处（schema 表 + env_config.txt），
//      不再需要同步维护本文件的解析分支 —— 「漏登记导致运行时打未知键」这一
//      整类 bug 从根上消失。
//
// 忠实保留原有**两条不同的诊断信息**（这是刻意的，别合并）：
//   - 键不在 schema 表里 → "[EnvConfig] 未知键，已忽略"
//   - 键在表里但值非法   → "[EnvConfig] 第 N 行值解析失败"
// 做法：先用 `ParamSchema::find()` 判断键是否存在，再调用 `applyLine()` 写值。
// 若直接 `if (!applyLine(...)) 打印未知键`，会把"值非法"误报成"未知键"
// （applyLine 内部捕获异常并返回 false，无法区分这两种失败）。
// ---------------------------------------------------------------------------

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}  // namespace

bool EnvConfig::load(const std::string& path, WorldConfig& cfg) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;  // 空行/注释

        size_t eq = t.find('=');
        if (eq == std::string::npos) {
            std::cerr << "[EnvConfig] 第 " << lineNo << " 行缺少 '='，已跳过: " << t << "\n";
            continue;
        }
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));

        // 先判键是否存在：这样才能把"未知键"与"值非法"分成两条诊断。
        // 传给 applyLine 的是**原始值**（含内联注释），由它自己去注释、去空白 trim，
        // 与手写链的语义一致（本文件不再重复处理，避免双重去注释语义漂移）。
        if (!ParamSchema::find(key)) {
            std::cerr << "[EnvConfig] 未知键，已忽略: " << key << "\n";
            continue;
        }
        try {
            if (!ParamSchema::applyLine(cfg, key, val)) {
                std::cerr << "[EnvConfig] 第 " << lineNo << " 行值解析失败: "
                          << key << " = " << val << "\n";
            }
        } catch (const std::exception&) {
            std::cerr << "[EnvConfig] 第 " << lineNo << " 行值解析失败: "
                      << key << " = " << val << "\n";
        }
    }
    return true;
}

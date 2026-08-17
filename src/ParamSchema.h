#pragma once
#include <map>
#include <string>
#include <vector>
#include "World.h"

// 参数 schema 单一来源：WorldConfig/ParamRanges 每个字段的元数据。
// 驱动三件事：启动器参数页生成（GET /schema）、env_config.txt 写出（保存默认）、
// 键覆盖测试（--schema-check：schema 键集合 ⊇ EnvConfig 识别的键集合）。
struct ParamDef {
    enum class Type { Int, Double, Bool, Pair };
    const char* key;
    const char* group;   // 参数页分组
    const char* desc;    // 中文说明
    Type type;
    size_t offA;         // 字段偏移（Pair 为下限字段）
    size_t offB;         // 仅 Pair：上限字段偏移
    double min, max;     // UI 范围
    double dfltA, dfltB; // 默认值（Pair 为下限/上限）
};

class ParamSchema {
public:
    static const std::vector<ParamDef>& all();
    static const ParamDef* find(const std::string& key);

    // 从 cfg 读出该键当前值的文本（"42" / "0.5" / "1" / "0.1, 1.0"）。
    static std::string valueString(const WorldConfig& cfg, const ParamDef& d);

    // 按表解析 key=value 文本，写入 cfg（与 EnvConfig 语义一致；未知键跳过）。
    static bool applyLine(WorldConfig& cfg, const std::string& key, const std::string& value);

    // 用 schema 生成完整 env_config.txt（默认值 + overrides 覆盖），带分组注释。
    static bool writeEnvFile(const std::string& path, const WorldConfig& base,
                             const std::map<std::string, std::string>& overrides);

    // 键覆盖测试：EnvConfig 能识别的每个键都必须在 schema 表中。
    static bool coverageCheck();
};

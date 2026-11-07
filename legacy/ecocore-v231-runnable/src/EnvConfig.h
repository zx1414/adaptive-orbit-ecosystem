#pragma once
#include <string>
#include "World.h"

// 环境配置文件解析：key = value 形式的自然环境参数。
// 用于在程序运行时读取世界的物理/资源/运行参数，覆盖 WorldConfig 的默认值。
class EnvConfig {
public:
    // 解析环境配置文件，将识别到的键写入 cfg（覆盖默认值）。
    // 返回 false 表示无法打开文件；未知键或解析失败输出警告并跳过。
    static bool load(const std::string& path, WorldConfig& cfg);
};

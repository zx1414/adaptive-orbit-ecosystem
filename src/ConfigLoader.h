#pragma once
#include <string>
#include <vector>
#include "Nucleus.h"
#include "Vec2.h"

// 配置文件中一行对应的一个待注入核。
struct NucleusSeed {
    Vec2 pos;
    double energy = 0.0;
    NucleusParams params;
};

// 解析纯文本配置文件，返回成功解析的核；错误行输出警告并跳过。
class ConfigLoader {
public:
    static std::vector<NucleusSeed> load(const std::string& path);
};

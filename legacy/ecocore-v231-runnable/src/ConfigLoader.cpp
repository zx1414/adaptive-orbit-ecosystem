#include "ConfigLoader.h"
#include <fstream>
#include <iostream>
#include <sstream>

std::vector<NucleusSeed> ConfigLoader::load(const std::string& path) {
    std::vector<NucleusSeed> result;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[ConfigLoader] 无法打开配置文件: " << path << "\n";
        return result;
    }

    // 字段总数：x y energy + 4*3 类型感知参数 + 7 自身行为参数 = 22。
    constexpr size_t EXPECTED_FIELDS = 2 + 1 + 4 * BALL_TYPE_COUNT + 7;

    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;  // 空行
        if (line[start] == '#') continue;           // 注释

        std::istringstream ss(line);
        std::vector<double> vals;
        double v;
        while (ss >> v) vals.push_back(v);

        if (vals.size() != EXPECTED_FIELDS) {
            std::cerr << "[ConfigLoader] 第 " << lineNo << " 行字段数量错误"
                      << "(期望 " << EXPECTED_FIELDS << ", 实际 " << vals.size()
                      << ")，已跳过\n";
            continue;
        }

        NucleusSeed seed;
        size_t k = 0;
        seed.pos.x = vals[k++];
        seed.pos.y = vals[k++];
        seed.energy = vals[k++];
        for (int i = 0; i < BALL_TYPE_COUNT; ++i) seed.params.affinity[i] = vals[k++];
        for (int i = 0; i < BALL_TYPE_COUNT; ++i) seed.params.orbitRadius[i] = vals[k++];
        for (int i = 0; i < BALL_TYPE_COUNT; ++i) seed.params.absorbPreference[i] = vals[k++];
        for (int i = 0; i < BALL_TYPE_COUNT; ++i) seed.params.repelStrength[i] = vals[k++];
        seed.params.attackRange = vals[k++];
        seed.params.attackStrength = vals[k++];
        seed.params.avoidRange = vals[k++];
        seed.params.avoidStrength = vals[k++];
        seed.params.maxSpeed = vals[k++];
        seed.params.energyThreshold = vals[k++];
        seed.params.mutationRate = vals[k++];
        result.push_back(seed);
    }
    return result;
}

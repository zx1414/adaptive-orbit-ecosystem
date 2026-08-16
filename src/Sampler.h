#pragma once
#include <fstream>
#include <string>
#include "World.h"

// 演化过程采样：按帧导出全场状态为 CSV 文件。
class Sampler {
public:
    explicit Sampler(std::string dir = ".");

    // 采样当前帧，写入 balls_<frame>.csv 与 nuclei_<frame>.csv。
    void sample(const World& world);

    // 模拟结束时导出存活核到 survivors.csv（格式同核采样文件）。
    void writeSurvivors(const World& world);

    // 打开趋势 CSV（写入表头），返回是否成功。每帧数据由 sampleTrend 追加。
    bool openTrend(const std::string& path);
    // 将当前帧所有存活核的参数追加一行（frame, 核谱系 ID, 能量, 22 个参数）。
    void sampleTrend(const World& world);
    // 关闭趋势 CSV。
    void closeTrend();

private:
    std::string dir_;
    std::ofstream trendOut_;
};

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include "ConfigLoader.h"
#include "EnvConfig.h"
#include "Renderer.h"
#include "Sampler.h"
#include "World.h"

namespace {
struct Args {
    double width = 2000.0;
    double height = 2000.0;
    int seed = 42;
    int balls = 1000;
    int nuclei = 20;
    int frames = 100000;
    int renderInterval = 10;
    int sampleInterval = 0;
    int maxNuclei = 0;
    double maxFps = 0.0;
    std::string config;
    std::string trendCsv;
    int trendInterval = 100;
    bool visualization = true;
    bool clearScreen = true;
    std::string env;
    std::set<std::string> provided;  // 记录被显式指定的参数，用于覆盖配置文件
};

Args parseArgs(int argc, char** argv) {
    Args a;
    auto next = [&](int& i, const char* name) -> std::string {
        if (i + 1 >= argc) {
            std::cerr << "缺少参数值: " << name << "\n";
            std::exit(2);
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "--width") { a.width = std::stod(next(i, "--width")); a.provided.insert(s); }
        else if (s == "--height") { a.height = std::stod(next(i, "--height")); a.provided.insert(s); }
        else if (s == "--seed") { a.seed = std::stoi(next(i, "--seed")); a.provided.insert(s); }
        else if (s == "--balls") { a.balls = std::stoi(next(i, "--balls")); a.provided.insert(s); }
        else if (s == "--nuclei") { a.nuclei = std::stoi(next(i, "--nuclei")); a.provided.insert(s); }
        else if (s == "--frames") { a.frames = std::stoi(next(i, "--frames")); a.provided.insert(s); }
        else if (s == "--render-interval") { a.renderInterval = std::stoi(next(i, "--render-interval")); a.provided.insert(s); }
        else if (s == "--sample-interval") { a.sampleInterval = std::stoi(next(i, "--sample-interval")); a.provided.insert(s); }
        else if (s == "--max-nuclei") { a.maxNuclei = std::stoi(next(i, "--max-nuclei")); a.provided.insert(s); }
        else if (s == "--max-fps") { a.maxFps = std::stod(next(i, "--max-fps")); a.provided.insert(s); }
        else if (s == "--config") a.config = next(i, "--config");
        else if (s == "--trend-csv") a.trendCsv = next(i, "--trend-csv");
        else if (s == "--trend-interval") { a.trendInterval = std::stoi(next(i, "--trend-interval")); a.provided.insert(s); }
        else if (s == "--render") { a.visualization = true; a.provided.insert(s); }
        else if (s == "--no-render") { a.visualization = false; a.provided.insert(s); }
        else if (s == "--clear") { a.clearScreen = true; a.provided.insert(s); }
        else if (s == "--no-clear") { a.clearScreen = false; a.provided.insert(s); }
        else if (s == "--env") a.env = next(i, "--env");
        else std::cerr << "未知参数，已忽略: " << s << "\n";
    }
    return a;
}

bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

// 从 argv[0] 提取可执行文件所在目录。
std::string exeDir(const std::string& argv0) {
    size_t pos = argv0.find_last_of("\\/");
    if (pos == std::string::npos) return ".";
    std::string d = argv0.substr(0, pos);
    return d.empty() ? "." : d;
}

#ifdef _WIN32
// 锁定控制台窗口为 cols x rows（字符为单位）。
void lockConsole(int cols, int rows) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
    COORD buf = {(SHORT)cols, (SHORT)rows};
    SetConsoleScreenBufferSize(h, buf);
    SMALL_RECT rect;
    rect.Left = 0;
    rect.Top = 0;
    rect.Right = (SHORT)(cols - 1);
    rect.Bottom = (SHORT)(rows - 1);
    SetConsoleWindowInfo(h, TRUE, &rect);
    SetConsoleScreenBufferSize(h, buf);
}
#endif
}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // 控制台按 UTF-8 解释本程序输出，避免中文乱码。
    SetConsoleOutputCP(CP_UTF8);
#endif

    Args a = parseArgs(argc, argv);

    WorldConfig cfg;  // 默认值

    // 1) 读取自然环境配置文件（覆盖默认值）。
    //    优先级：显式 --env > exe 同目录 env_config.txt > 当前目录 env_config.txt
    std::string envPath = a.env;
    if (envPath.empty()) {
        std::string dir = exeDir(argv[0]);
        if (dir != ".") {
            std::string c = dir + "/env_config.txt";
            if (fileExists(c)) envPath = c;
        }
        if (envPath.empty() && fileExists("env_config.txt")) envPath = "env_config.txt";
    }
    if (!envPath.empty()) {
        if (!EnvConfig::load(envPath, cfg)) {
            std::cerr << "无法打开环境配置文件: " << envPath << "\n";
            return 1;
        }
        std::cout << "已加载环境配置: " << envPath << "\n";
    }

    // 2) 显式命令行参数覆盖配置文件
    if (a.provided.count("--width")) cfg.width = a.width;
    if (a.provided.count("--height")) cfg.height = a.height;
    if (a.provided.count("--seed")) cfg.seed = a.seed;
    if (a.provided.count("--balls")) cfg.initialBalls = a.balls;
    if (a.provided.count("--nuclei")) cfg.initialNuclei = a.nuclei;
    if (a.provided.count("--frames")) cfg.maxFrames = a.frames;
    if (a.provided.count("--render-interval")) cfg.renderInterval = a.renderInterval;
    if (a.provided.count("--sample-interval")) cfg.sampleInterval = a.sampleInterval;
    if (a.provided.count("--max-nuclei")) cfg.maxNuclei = a.maxNuclei;
    if (a.provided.count("--max-fps")) cfg.maxFps = a.maxFps;
    if (a.provided.count("--trend-interval")) cfg.trendInterval = a.trendInterval;
    if (a.provided.count("--render") || a.provided.count("--no-render")) cfg.visualization = a.visualization;
    if (a.provided.count("--clear") || a.provided.count("--no-clear")) cfg.clearScreen = a.clearScreen;

    World world(cfg);
    world.initialize();

    // 手动注入配置核（在随机初始化之后加入，数量不受随机核数限制）。
    int injected = 0;
    if (!a.config.empty()) {
        auto seeds = ConfigLoader::load(a.config);
        for (const auto& s : seeds) {
            world.addNucleus(s.pos, s.energy, s.params);
            ++injected;
        }
    }

    std::cout << "核-小球生态演化模拟器\n"
              << "世界 " << cfg.width << "x" << cfg.height
              << " | 小球 " << cfg.initialBalls
              << " | 核 " << cfg.initialNuclei
              << " | 帧数 " << cfg.maxFrames
              << " | 种子 " << cfg.seed
              << " | 承载力 " << world.config().maxNuclei
              << " | 可视化 " << (cfg.visualization ? "开" : "关");
    if (injected > 0) std::cout << " | 注入核 " << injected;
    std::cout << "\n" << std::flush;

#ifdef _WIN32
    // 锁定控制台窗口为合适大小：网格 + 统计行 + 边距。
    if (cfg.lockConsole) {
        lockConsole(cfg.gridCols + 2, cfg.gridRows + 2);
    }
#endif

    Renderer renderer(cfg.gridCols, cfg.gridRows);
    renderer.setClear(cfg.clearScreen);
    Sampler sampler(".");

    // 趋势 CSV：按采样间隔追加每帧存活核的完整参数（时间 + 参数的时间序列）。
    bool trendEnabled = !a.trendCsv.empty();
    if (trendEnabled) {
        if (!sampler.openTrend(a.trendCsv)) {
            std::cerr << "无法打开趋势 CSV: " << a.trendCsv << "\n";
            return 1;
        }
    }

    // 每秒模拟帧数上限：每帧结束后按剩余时间休眠，使模拟节奏稳定（0 = 不限速）。
    using clock = std::chrono::steady_clock;
    const auto frameDur = std::chrono::duration<double>(cfg.maxFps > 0.0 ? 1.0 / cfg.maxFps : 0.0);

    while (true) {
        auto t0 = clock::now();
        world.step();
        bool done = world.finished();

        if (cfg.visualization && cfg.renderInterval > 0 && world.frame() % cfg.renderInterval == 0) {
            renderer.render(world);
        }
        if (cfg.sampleInterval > 0 && world.frame() % cfg.sampleInterval == 0) {
            sampler.sample(world);
        }
        if (trendEnabled && cfg.trendInterval > 0 && world.frame() % cfg.trendInterval == 0) {
            sampler.sampleTrend(world);
        }

        if (done) break;

        if (cfg.maxFps > 0.0) {
            auto elapsed = clock::now() - t0;
            if (elapsed < frameDur) {
                std::this_thread::sleep_for(frameDur - elapsed);
            }
        }
    }

    // 最终结果输出。
    std::cout << "\n===== 模拟结束 =====\n";
    std::cout << "结束原因: " << world.finishReason() << "\n";
    std::cout << "最终帧号: " << world.frame() << "\n";
    std::cout << "存活核数: " << world.aliveNucleusCount() << "\n";
    std::cout << "小球总数: " << world.balls().size() << "\n";
    std::cout << "平均核能量: " << std::fixed << std::setprecision(2)
              << world.averageNucleusEnergy() << "\n";
    if (world.aliveNucleusCount() > 0) {
        sampler.writeSurvivors(world);
        std::cout << "存活核参数已导出到 survivors.csv\n";
    }
    if (trendEnabled) {
        sampler.closeTrend();
        std::cout << "核参数时间序列已导出到 " << a.trendCsv << "\n";
    }

    if (cfg.pauseOnExit) {
        std::cout << "\n按回车键退出..." << std::flush;
        std::cin.get();
    }
    return 0;
}

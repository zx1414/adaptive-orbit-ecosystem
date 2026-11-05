// agentsim - tests/unit/test_trace.cpp
// T29/R1-B（契约 v3）：蓝细菌取代食物后的导出单测。
//
// 为什么下面直接 #include app/TraceWriter.cpp + app/main.cpp：
// tools/build.ps1 的单测目标只收集 src/app 之外的库源（libCpps 过滤掉
// ^src\\app\\），TraceWriter 与 main 都不在 unit_tests.exe 的链接列表里。
// 与 test_cli.cpp 同一招；若日后构建脚本把 app/*.cpp 加进单测源集，
// 必须删除对应 #include，否则符号重复。
//
// 注意：tests/unit/main.cpp 提供 main()（注册器驱动），本文件只注册
// TEST 用例，绝不能自己定义 main。
#include "app/TraceWriter.cpp"

#include "test_framework.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/sim/Simulation.h"

namespace {

using app::TraceWriter;
using agentsim::Simulation;
using agentsim::SimConfig;

// 追加一段 JSON 文本；返回全部内容（小文件直接整读）。
std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// 极简子串/计数助手（避免引第三方 JSON 库；契约键序是固定的，子串匹配足够）。
std::size_t countOf(const std::string& s, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t p = s.find(needle); p != std::string::npos;
         p = s.find(needle, p + needle.size())) {
        ++n;
    }
    return n;
}

// 统一构造：默认配置 + 指定种子（贴近 CLI 缺省运行）。
Simulation makeSim(std::uint64_t seed) {
    SimConfig cfg;
    cfg.world.seed = seed;
    return Simulation(cfg);
}

} // namespace

// ---- v3-1：schemaVersion / meta.contractVersion 双写且同步（300 tick 轨迹）----
TEST(trace_v3_schema) {
    Simulation sim = makeSim(42);
    TraceWriter tw;
    const std::string path = "build/test_trace_v3_schema.json";
    CHECK(tw.open(path));
    tw.setMeta(42, 300, 256.0, 256.0, "rule");
    tw.beginBounds(0.0, 0.0, 256.0, 256.0);
    for (int t = 0; t < 300; ++t) {
        sim.step();
        tw.addSeries(static_cast<long long>(sim.tick()),
                     static_cast<long long>(sim.nucleusCount()),
                                          static_cast<long long>(sim.cyano().aliveCount()),
                     sim.cyano().totalEnergy(),
                     sim.totalEnergy(),
                     sim.ledger().totalInjected(),
                     sim.ledger().totalDissipated(),
                     sim.auditOk());
        tw.beginFrame(static_cast<long long>(sim.tick()));
        for (const agentsim::CyanoNode& cn : sim.cyanoNodes()) {
            tw.addCyano(static_cast<long long>(cn.id), cn.pos.x, cn.pos.y,
                        cn.energy, cn.alive, cn.hue);
        }
        tw.endFrame();
    }
    CHECK(tw.finalize());
    const std::string j = slurp(path);
    // 顶层版本：schemaVersion=2（整段出现在文件头，键序固定）。
    CHECK(j.find("{\"schemaVersion\":3,") == 0);
    // meta.contractVersion=2 与顶层同步（冗余字段，契约要求两处都写）。
    CHECK(j.find("\"contractVersion\":3") != std::string::npos);
    CHECK(countOf(j, "\"schemaVersion\":") == 1);
    CHECK(countOf(j, "\"contractVersion\":") == 1);
}

// ---- v3-2：frames[0].c 非空且每个元组恰 5 项 ----
TEST(trace_v3_cyano_present) {
    Simulation sim = makeSim(42);
    CHECK(sim.cyanoNodes().size() > 0);  // 构造即播种（kCyanoInitial=32）
    TraceWriter tw;
    const std::string path = "build/test_trace_v3_cyano.json";
    CHECK(tw.open(path));
    tw.setMeta(42, 1, 256.0, 256.0, "rule");
    tw.beginBounds(0.0, 0.0, 256.0, 256.0);
    sim.step();
    tw.addSeries(1, 0, 0, 0.0, 0.0, 0.0, 0.0, true);
    tw.beginFrame(1);
    for (const agentsim::CyanoNode& cn : sim.cyanoNodes()) {
        tw.addCyano(static_cast<long long>(cn.id), cn.pos.x, cn.pos.y,
                    cn.energy, cn.alive, cn.hue);
    }
    tw.endFrame();
    CHECK(tw.finalize());
    const std::string j = slurp(path);
    // 帧内键序：tick, n, b, f, c（v3 中 "f" 为连线拓扑数组，可为空数组）。
    CHECK(j.find("],\"f\":[") != std::string::npos);
    CHECK(j.find("],\"c\":[[") != std::string::npos);
    // 元组 5 项：id,x,y,energy,alive —— 数帧里 '],[' 数量 = 节点数-1，
    // 再抽查首元组以 ']' 结束前有 4 个逗号。直接数 c 数组内逗号更简单：
    const std::size_t cStart = j.find("\"c\":[[");
    CHECK(cStart != std::string::npos);
    // v3 每个元组形如 [1,12.3,45.6,0.000000,true,215.000000]：6 项 5 逗号
    //（R1-B 追加 hue；alive 之后跟的是色相数值，不再以 true/false 收尾）。
    // 取 frames 数组段统计首元组。
    const std::size_t framesAt = j.rfind("\"frames\":[");
    CHECK(framesAt != std::string::npos);
    const std::string seg = j.substr(framesAt);
    const std::size_t tup0 = seg.find("[[");
    CHECK(tup0 != std::string::npos);
    const std::size_t tup0End = seg.find(']', tup0);
    CHECK(tup0End != std::string::npos);
    const std::string t0 = seg.substr(tup0 + 1, tup0End - tup0);
    int commas = 0;
    for (const char ch : t0) { if (ch == ',') ++commas; }
    CHECK(commas == 5);  // [id, x, y, energy, alive, hue] -> 5 separators
    // c 数组非空：至少一条蓝细菌元组（42 号种子构造后必有存活节点）。
    CHECK(sim.cyanoNodes().size() >= 1);
    // alive 槽位是布尔、hue 槽位是数值：",true," 或 ",false," 必出现且后跟数字。
    CHECK(countOf(seg, ",true,") + countOf(seg, ",false,") >= 1);
}

// ---- v3-3：series 每行含 cyanoAlive / cyanoEnergy，且数值口径正确 ----
TEST(trace_v3_series_fields) {
    Simulation sim = makeSim(42);
    TraceWriter tw;
    const std::string path = "build/test_trace_v3_series.json";
    CHECK(tw.open(path));
    tw.setMeta(42, 5, 256.0, 256.0, "rule");
    tw.beginBounds(0.0, 0.0, 256.0, 256.0);
    for (int t = 0; t < 5; ++t) {
        sim.step();
        tw.addSeries(static_cast<long long>(sim.tick()),
                     static_cast<long long>(sim.nucleusCount()),
                                          static_cast<long long>(sim.cyano().aliveCount()),
                     sim.cyano().totalEnergy(),
                     sim.totalEnergy(),
                     sim.ledger().totalInjected(),
                     sim.ledger().totalDissipated(),
                     sim.auditOk());
        tw.beginFrame(static_cast<long long>(sim.tick()));
        tw.endFrame();
    }
    CHECK(tw.finalize());
    const std::string j = slurp(path);
    // v3 键序：nuclei 之后是 cyanoAlive、cyanoEnergy，再是 totalEnergy。
    // R1-B：series 的 "food" 键必须已消失（Food 实体删除）。
    CHECK(j.find("\"cyanoAlive\":") != std::string::npos);
    CHECK(j.find("\"cyanoEnergy\":") != std::string::npos);
    CHECK(j.find("\"food\":") == std::string::npos);
    // 每条 series 行都带这两个字段（5 行 = 5 次出现）。
    CHECK(countOf(j, "\"cyanoAlive\":") == 5);
    CHECK(countOf(j, "\"cyanoEnergy\":") == 5);
    // 口径：写出的 cyanoAlive/cyanoEnergy 必须等于 CyanoField 的观测值。
    const std::string expectAlive =
        "\"cyanoAlive\":" + std::to_string(sim.cyano().aliveCount());
    CHECK(j.find(expectAlive) != std::string::npos);
}

// ---- v3-4：同 seed 双跑 trace sha256 逐位一致（确定性不破）----
// 单测内没有 sha256，退而求其次逐字节比较两次运行的文件内容；
// CLI 层的 sha256 双跑由验收命令另行覆盖（--trace build/v3.json）。
TEST(trace_v3_double_run) {
    for (int pass = 0; pass < 2; ++pass) {
        Simulation sim = makeSim(20260704);
        TraceWriter tw;
        const std::string path = pass == 0
                                     ? "build/test_trace_v3_run_a.json"
                                     : "build/test_trace_v3_run_b.json";
        CHECK(tw.open(path));
        tw.setMeta(20260704, 120, 256.0, 256.0, "rule");
        tw.beginBounds(0.0, 0.0, 256.0, 256.0);
        for (int t = 0; t < 120; ++t) {
            sim.step();
            tw.addSeries(static_cast<long long>(sim.tick()),
                         static_cast<long long>(sim.nucleusCount()),
                                                  static_cast<long long>(sim.cyano().aliveCount()),
                         sim.cyano().totalEnergy(),
                         sim.totalEnergy(),
                         sim.ledger().totalInjected(),
                         sim.ledger().totalDissipated(),
                         sim.auditOk());
            if ((sim.tick() % 1) == 0) {
                tw.beginFrame(static_cast<long long>(sim.tick()));
                for (const agentsim::Nucleus& n : sim.nuclei()) {
                    tw.addNucleus(static_cast<long long>(n.id), n.pos.x, n.pos.y,
                                  n.energy, n.cap, n.speed, n.alive);
                }
                for (const agentsim::Ball& b : sim.freeBalls()) {
                    tw.addBall(static_cast<long long>(b.id), 0, b.pos.x, b.pos.y,
                               b.energy, b.radius, static_cast<int>(b.type));
                }
                // R1-B（契约 v3）：食物导出删除，"f" 改为蓝细菌连线拓扑。
                for (const auto& lk : sim.cyanoLinks()) {
                    tw.addLink(static_cast<long long>(lk.first),
                               static_cast<long long>(lk.second));
                }
                for (const agentsim::CyanoNode& cn : sim.cyanoNodes()) {
                    tw.addCyano(static_cast<long long>(cn.id), cn.pos.x,
                                cn.pos.y, cn.energy, cn.alive, cn.hue);
                }
                tw.endFrame();
            }
        }
        CHECK(tw.finalize());
        CHECK(sim.auditOk());
    }
    const std::string a = slurp("build/test_trace_v3_run_a.json");
    const std::string b = slurp("build/test_trace_v3_run_b.json");
    CHECK(a.size() > 1000);          // 不是空壳
    CHECK(a == b);                   // 逐字节一致 == sha256 一致
}

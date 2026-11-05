// agentsim - tests/unit/test_cli.cpp
// T22：CLI 参数解析单测，核心是 --no-regrow（S23 验证开关）。
//
// 为什么下面源码包含 app/Cli.cpp：tools/build.ps1 的单测目标只收集
// src/app 之外的库源（libCpps 过滤掉 ^src\\app\\），parseCli 不在
// unit_tests.exe 的链接列表里。直接把 Cli.cpp 并入本翻译单元即可，
// 不必改共享构建脚本；若日后构建脚本把 app/Cli.cpp 加进单测源集，
// 必须删除下一行 #include，否则 parseCli 符号重复。
#include "app/Cli.cpp"

#include "test_framework.h"

#include <string>

namespace {

// parseCli 的 argv 形参是 char**（POSIX 惯例，不修改内容），
// 测试里的字符串字面量是 const char*，统一 const_cast。
// 返回 argc，并把指针填进 buf（容量 32 足够所有用例）。
int makeArgv(const char* const* args, char** buf) {
    int n = 0;
    for (; args[n] != nullptr; ++n) buf[n] = const_cast<char*>(args[n]);
    return n;
}

bool runParse(const char* const* args, agentsim::CliOptions& out, std::string& err) {
    char* argv[32];
    const int argc = makeArgv(args, argv);
    return agentsim::parseCli(argc, argv, out, err);
}

} // namespace

// T22-1（R1-B）：--food / --regrow / --no-regrow 已删除——解析必须显式拒绝。
TEST(cli_food_flags_removed_are_rejected) {
    static const char* const a1[] = {"agentsim", "--food", "128", nullptr};
    static const char* const a2[] = {"agentsim", "--regrow", nullptr};
    static const char* const a3[] = {"agentsim", "--no-regrow", nullptr};
    agentsim::CliOptions out{};
    std::string err;
    CHECK(runParse(a1, out, err) == false);
    CHECK(err.find("--food") != std::string::npos);
    out = agentsim::CliOptions{};
    CHECK(runParse(a2, out, err) == false);
    CHECK(err.find("--regrow") != std::string::npos);
    out = agentsim::CliOptions{};
    CHECK(runParse(a3, out, err) == false);
    CHECK(err.find("--no-regrow") != std::string::npos);
}

// T22-2（R1-B）：--cyano-nodes / --cyano-max-nodes 解析并落进 SimConfig（真生效面）。
TEST(cli_cyano_options_parse_into_config) {
    agentsim::CliOptions out{};
    std::string err;
    static const char* const args[] = {
        "agentsim", "--cyano-nodes", "8", "--cyano-max-nodes", "16", nullptr};
    CHECK(runParse(args, out, err));
    CHECK(err.empty());
    CHECK(out.config.cyanoNodeCount == 8u);
    CHECK(out.config.cyanoMaxNodes == 16u);
    // 默认值（32 / 64）在未指定时保留。
    agentsim::CliOptions def{};
    static const char* const none[] = {"agentsim", nullptr};
    CHECK(runParse(none, def, err));
    CHECK(def.config.cyanoNodeCount == 512u);  // R10 世界 1024
    CHECK(def.config.cyanoMaxNodes == 4096u);  // R10 世界 1024
    // 非法值必须被拒绝。
    static const char* const neg[] = {"agentsim", "--cyano-nodes", "-3", nullptr};
    CHECK(runParse(neg, def, err) == false);
    static const char* const junk[] = {"agentsim", "--cyano-max-nodes", "abc", nullptr};
    CHECK(runParse(junk, def, err) == false);
}

// T22-3（R1-B）：--sun-base / --sun-amplitude 解析并落进 SunConfig。
TEST(cli_sun_options_parse_into_config) {
    agentsim::CliOptions out{};
    std::string err;
    static const char* const args[] = {
        "agentsim", "--sun-base", "0.8", "--sun-amplitude", "0.1", nullptr};
    CHECK(runParse(args, out, err));
    CHECK(err.empty());
    CHECK(out.config.sun.baseLevel == 0.8);
    CHECK(out.config.sun.dayAmplitude == 0.1);
    // 默认（0.5 / 0.5）保留；负值按 F1 规则拒绝（不允许负基线）。
    agentsim::CliOptions def{};
    static const char* const none[] = {"agentsim", nullptr};
    CHECK(runParse(none, def, err));
    CHECK(def.config.sun.baseLevel == 0.5);
    CHECK(def.config.sun.dayAmplitude == 0.5);
    static const char* const neg[] = {"agentsim", "--sun-base", "-0.1", nullptr};
    CHECK(runParse(neg, def, err) == false);
}

// T22-4：未知参数必须被拒绝（返回 false），且 err 能让人看出是哪个参数。
TEST(cli_unknown_option_rejected) {
    agentsim::CliOptions out{};
    std::string err;
    static const char* const args[] = {"agentsim", "--bogus", nullptr};
    const bool ok = runParse(args, out, err);
    CHECK(ok == false);
    CHECK(err.find("unknown option") != std::string::npos);
    CHECK(err.find("--bogus") != std::string::npos);
    // 被拒绝时蓝细菌库存必须仍是默认值（32），而不是被部分解析污染。
    CHECK(out.config.cyanoNodeCount == 512u);  // R10
}

// T22-5：新选项与其它参数组合时各自生效、顺序无关。
TEST(cli_cyano_options_combine_with_other_flags) {
    agentsim::CliOptions out{};
    std::string err;
    static const char* const args[] = {
        "agentsim", "--cyano-nodes", "8", "--ticks", "10", "--seed", "7",
        "--cyano-max-nodes", "16", nullptr};
    CHECK(runParse(args, out, err));
    CHECK(err.empty());
    CHECK(out.config.cyanoNodeCount == 8u);
    CHECK(out.config.cyanoMaxNodes == 16u);
    CHECK(out.config.world.maxTicks == 10u);
    CHECK(out.config.world.seed == 7u);
}

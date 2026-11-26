// agentsim - tests/unit/main.cpp
// 极简单测框架实现 + 入口：注册收集与结果汇报。
#include "test_framework.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace agentsim_test {
namespace {
struct Case {
    const char* name;
    void (*fn)();
};
std::vector<Case>& cases() {
    static std::vector<Case> v;
    return v;
}
int g_failures = 0;
int g_checks = 0;
const char* g_current = "<unknown>";
} // namespace

int& failureCount() { return g_failures; }
int& checkCount() { return g_checks; }
const char*& currentTestName() { return g_current; }

void reportFailure(const char* expr, const char* file, int line) {
    ++g_failures;
    std::printf("  FAIL [%s] %s:%d : CHECK(%s)\n", g_current, file, line, expr);
}

void reportNearFailure(double a, double b, double eps, const char* ea, const char* eb,
                       const char* file, int line) {
    ++g_failures;
    std::printf("  FAIL [%s] %s:%d : CHECK_NEAR(%s, %s, %g) got %g vs %g\n",
                g_current, file, line, ea, eb, eps, a, b);
}

Registrar::Registrar(const char* name, Fn fn) { cases().push_back(Case{name, fn}); }
} // namespace agentsim_test

namespace {
// 用例定位开关（本机存在与代码无关的环境崩溃：完整套件会在某个用例处
// 0xC0000409 异常终止，且终止点随工作目录变化，见 CONTRIBUTING.md 末段）。
// 没有这些开关时，崩溃只能表现为"日志停在中间"，无法区分"卡住"和"崩了哪一条"。
//   --list            只打印用例名（注册序）
//   --from N          从第 N 条开始跑（0 起）
//   --only 子串       只跑名字包含子串的用例（可多次）
//   --skip 子串       跳过名字包含子串的用例（可多次）
// 每次执行用例前先打印 "[RUN] 名字" 并 flush：崩溃时日志最后一行就是肇事用例。
class Filter {
public:
    std::vector<std::string> only;
    std::vector<std::string> skip;
    int from = 0;
    bool matches(const char* name) const {
        const std::string n{name};
        for (const std::string& s : only) {
            if (n.find(s) != std::string::npos) return true;
        }
        if (only.empty()) {
            for (const std::string& s : skip) {
                if (n.find(s) != std::string::npos) return false;
            }
            return true;
        }
        return false;
    }
};
} // namespace

int runAllTests(int argc, char** argv) {
    using namespace agentsim_test;
    bool listOnly = false;
    Filter filter;
    for (int i = 1; i < argc; ++i) {
        const std::string a{argv[i]};
        if (a == "--list") listOnly = true;
        else if (a == "--from" && i + 1 < argc) filter.from = std::atoi(argv[++i]);
        else if (a == "--only" && i + 1 < argc) filter.only.push_back(argv[++i]);
        else if (a == "--skip" && i + 1 < argc) filter.skip.push_back(argv[++i]);
    }
    if (listOnly) {
        int idx = 0;
        for (const Case& c : cases()) std::printf("%d\t%s\n", idx++, c.name);
        std::fflush(stdout);
        return 0;
    }
    int ran = 0;
    int idx = -1;
    for (const Case& c : cases()) {
        ++idx;
        if (idx < filter.from) continue;
        if (!filter.matches(c.name)) continue;
        const int before = failureCount();
        g_current = c.name;
        std::printf("[RUN] %s\n", c.name);
        std::fflush(stdout);
        c.fn();
        const int after = failureCount();
        std::printf("%s %s\n", after == before ? "[PASS]" : "[FAIL]", c.name);
        std::fflush(stdout);
        ++ran;
    }
    std::printf("\n%d/%d tests, %d checks, %d failures\n",
                ran, static_cast<int>(cases().size()), checkCount(), failureCount());
    std::fflush(stdout);
    return failureCount() == 0 ? 0 : 1;
}

int main(int argc, char** argv) { return runAllTests(argc, argv); }

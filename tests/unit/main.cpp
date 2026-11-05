// agentsim - tests/unit/main.cpp
// 极简单测框架实现 + 入口：注册收集与结果汇报。
#include "test_framework.h"

#include <cstring>
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

int runAllTests() {
    using namespace agentsim_test;
    for (const Case& c : cases()) {
        const int before = failureCount();
        g_current = c.name;
        c.fn();
        const int after = failureCount();
        std::printf("%s %s\n", after == before ? "[PASS]" : "[FAIL]", c.name);
    }
    std::printf("\n%d tests, %d checks, %d failures\n",
                static_cast<int>(cases().size()), checkCount(), failureCount());
    return failureCount() == 0 ? 0 : 1;
}

int main() { return runAllTests(); }

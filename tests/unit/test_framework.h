// agentsim - tests/unit/test_framework.h
// 零第三方极简单测框架：TEST / CHECK / CHECK_NEAR。
// main() 由 tests/unit/main.cpp 提供，此处只声明计数器接口。
#pragma once
#include <cmath>
#include <cstdio>

namespace agentsim_test {
int& failureCount();
int& checkCount();
const char*& currentTestName();
void reportFailure(const char* expr, const char* file, int line);
void reportNearFailure(double a, double b, double eps, const char* ea, const char* eb,
                       const char* file, int line);
} // namespace agentsim_test

#define AGENTSIM_TEST_CAT_(a, b) a##b
#define AGENTSIM_TEST_CAT(a, b) AGENTSIM_TEST_CAT_(a, b)

// 注册一个测试用例（构造静态注册器对象实现自动收集）。
#define TEST(name)                                                             \
    static void AGENTSIM_TEST_CAT(agentsim_test_fn_, name)();                  \
    static agentsim_test::Registrar AGENTSIM_TEST_CAT(agentsim_test_reg_, name)(\
        #name, &AGENTSIM_TEST_CAT(agentsim_test_fn_, name));                   \
    static void AGENTSIM_TEST_CAT(agentsim_test_fn_, name)()

namespace agentsim_test {
struct Registrar {
    using Fn = void (*)();
    Registrar(const char* name, Fn fn);
};
} // namespace agentsim_test

#define ASSERT(x) CHECK(x)

#define CHECK(x)                                                               \
    do {                                                                       \
        ++agentsim_test::checkCount();                                         \
        if (!(x)) {                                                            \
            agentsim_test::reportFailure(#x, __FILE__, __LINE__);              \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                  \
    do {                                                                       \
        ++agentsim_test::checkCount();                                         \
        const double av_ = static_cast<double>(a);                             \
        const double bv_ = static_cast<double>(b);                             \
        const double ev_ = static_cast<double>(eps);                           \
        if (!(av_ >= bv_ - ev_ && av_ <= bv_ + ev_)) {                         \
            agentsim_test::reportNearFailure(av_, bv_, ev_, #a, #b, __FILE__,  \
                                             __LINE__);                        \
        }                                                                      \
    } while (0)

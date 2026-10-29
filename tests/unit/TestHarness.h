#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*run)();
};

inline std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, void (*run)()) { cases().push_back({name, run}); }
};

inline void fail(const char* file, int line, const std::string& message) {
    ++failures();
    std::cerr << file << ':' << line << ": " << message << '\n';
}

inline int runAll() {
    int failedCases = 0;
    for (const Case& testCase : cases()) {
        const int before = failures();
        std::cout << "[ RUN      ] " << testCase.name << '\n';
        try {
            testCase.run();
        } catch (const std::exception& e) {
            fail(__FILE__, __LINE__, std::string("unhandled exception: ") + e.what());
        } catch (...) {
            fail(__FILE__, __LINE__, "unhandled non-standard exception");
        }
        if (failures() == before) {
            std::cout << "[       OK ] " << testCase.name << '\n';
        } else {
            ++failedCases;
            std::cout << "[  FAILED  ] " << testCase.name << '\n';
        }
    }
    std::cout << cases().size() - static_cast<std::size_t>(failedCases) << " passed, "
              << failedCases << " failed\n";
    return failedCases == 0 ? 0 : 1;
}

} // namespace test

#define TEST_CONCAT_INNER(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_INNER(a, b)
#define TEST(name) \
    static void TEST_CONCAT(testRun_, __LINE__)(); \
    static ::test::Registrar TEST_CONCAT(testRegistrar_, __LINE__)(name, &TEST_CONCAT(testRun_, __LINE__)); \
    static void TEST_CONCAT(testRun_, __LINE__)()

#define CHECK(expr) do { if (!(expr)) ::test::fail(__FILE__, __LINE__, "CHECK failed: " #expr); } while (false)
#define CHECK_NEAR(a, b, eps) do { \
    const double testA = static_cast<double>(a); \
    const double testB = static_cast<double>(b); \
    if (!std::isfinite(testA) || !std::isfinite(testB) || std::fabs(testA - testB) > (eps)) { \
        std::ostringstream testMessage; \
        testMessage << "CHECK_NEAR failed: " #a " = " << testA << ", " #b " = " << testB; \
        ::test::fail(__FILE__, __LINE__, testMessage.str()); \
    } \
} while (false)

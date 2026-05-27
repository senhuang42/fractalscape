// test_util.h — tiny dependency-free unit test harness.
#pragma once

#include <cmath>
#include <cstdio>

namespace test {
extern int g_checks;
extern int g_failures;
extern const char* g_current; // current test name, for context
}

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++::test::g_checks;                                                    \
        if (!(cond)) {                                                         \
            ++::test::g_failures;                                              \
            std::printf("  FAIL [%s] %s:%d: %s\n", ::test::g_current,          \
                        __FILE__, __LINE__, #cond);                            \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                  \
    do {                                                                       \
        ++::test::g_checks;                                                    \
        double _a = (a), _b = (b);                                             \
        if (std::fabs(_a - _b) > (eps)) {                                      \
            ++::test::g_failures;                                              \
            std::printf("  FAIL [%s] %s:%d: |%g - %g| > %g  (%s ~ %s)\n",      \
                        ::test::g_current, __FILE__, __LINE__, _a, _b,         \
                        (double)(eps), #a, #b);                               \
        }                                                                      \
    } while (0)

// Each test file provides one entry point with this signature.
void test_palette();
void test_fractal_math();
void test_cli();
void test_buddhabrot();
void test_periodic();

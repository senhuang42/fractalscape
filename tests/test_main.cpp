// test_main.cpp — runs every test group and reports a summary.
#include "test_util.h"

namespace test {
int g_checks = 0;
int g_failures = 0;
const char* g_current = "";
}

int main() {
    std::printf("running fractal unit tests\n");

    test::g_current = "palette";       test_palette();
    test::g_current = "fractal_math";  test_fractal_math();
    test::g_current = "cli";           test_cli();
    test::g_current = "buddhabrot";    test_buddhabrot();
    test::g_current = "periodic";      test_periodic();

    std::printf("\n%d checks, %d failures\n", test::g_checks, test::g_failures);
    if (test::g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("FAILED\n");
    return 1;
}

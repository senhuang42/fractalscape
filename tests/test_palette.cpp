// test_palette.cpp — hex parsing, palette resolution, gradient generation.
#include "test_util.h"
#include "palette.h"

#include <algorithm>

using namespace fractal;

void test_palette() {
    // ---- parseHexColor ----
    Color c;
    CHECK(parseHexColor("#ff0000", c)); CHECK_NEAR(c.r, 1.0, 1e-6); CHECK_NEAR(c.g, 0.0, 1e-6); CHECK_NEAR(c.b, 0.0, 1e-6);
    CHECK(parseHexColor("00ff00", c));  CHECK_NEAR(c.g, 1.0, 1e-6);
    CHECK(parseHexColor("#abc", c));    // shorthand expands: aa bb cc
    CHECK_NEAR(c.r, 0xaa / 255.0, 1e-6);
    CHECK_NEAR(c.b, 0xcc / 255.0, 1e-6);
    CHECK(!parseHexColor("xyz", c));    // non-hex
    CHECK(!parseHexColor("#12345", c)); // wrong length
    CHECK(!parseHexColor("", c));

    // ---- parsePalette: named ----
    std::vector<Color> stops;
    CHECK(parsePalette("aurora", stops));
    CHECK(stops.size() >= 2);
    CHECK(parsePalette("mono", stops));
    CHECK(stops.size() == 2);

    // ---- parsePalette: custom hex list ----
    CHECK(parsePalette("#000000,#ffffff", stops));
    CHECK(stops.size() == 2);
    CHECK(parsePalette(" #05010d , #ff7b54 ,#3fd0c9 ", stops)); // whitespace tolerant
    CHECK(stops.size() == 3);

    // ---- parsePalette: failures ----
    CHECK(!parsePalette("not_a_palette", stops)); // unknown name, not hex
    CHECK(!parsePalette("#000000", stops));       // only one stop
    CHECK(!parsePalette("#000000,xyz", stops));   // a non-hex token ("bad" is valid hex!)

    // built-in names list is non-empty and contains the default
    auto names = builtinPaletteNames();
    CHECK(!names.empty());
    CHECK(std::find(names.begin(), names.end(), "aurora") != names.end());

    // neon-dark resolves and is anchored dark at BOTH ends (the broad dark band
    // that keeps deep-zoom lakes from going olive, and loops cleanly when cycled).
    std::vector<Color> nd;
    CHECK(parsePalette("neon-dark", nd));
    CHECK(nd.size() >= 4);
    CHECK(nd.front().r < 0.1f && nd.front().g < 0.1f && nd.front().b < 0.1f);
    CHECK(nd.back().r < 0.1f && nd.back().g < 0.1f && nd.back().b < 0.25f);

    // ---- buildGradient ----
    std::vector<Color> two = {{0,0,0}, {1,1,1}};
    auto g = buildGradient(two, 256, /*cyclic=*/false);
    CHECK(g.size() == 256u * 3);
    CHECK(g[0] == 0);                    // first texel = first stop (black)
    CHECK(g[(256 - 1) * 3] == 255);      // last texel = last stop (white)
    CHECK(g[128 * 3] > 100 && g[128 * 3] < 160); // midpoint ~ gray

    // cyclic closes the loop: last texel returns near the first stop.
    auto gc = buildGradient(two, 256, /*cyclic=*/true);
    CHECK(gc.size() == 256u * 3);
    CHECK(gc[0] == 0);
    CHECK(gc[(256 - 1) * 3] < 30);       // wrapped back toward black

    // empty / degenerate inputs don't crash.
    CHECK(buildGradient({}, 256, false).empty());
    auto single = buildGradient({{1,0,0}}, 16, false);
    CHECK(single.size() == 16u * 3);
    CHECK(single[0] == 255 && single[1] == 0);
}

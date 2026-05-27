// test_buddhabrot.cpp — the GL-free Buddhabrot density renderer.
#include "test_util.h"

#include "buddhabrot.h"
#include "palette.h"

using namespace fractal;

void test_buddhabrot() {
    // Small, cheap render of the canonical full Mandelbrot view. We only assert
    // structural invariants (size, that escaping orbits actually deposited
    // density, symmetry about the real axis) rather than exact pixels, since the
    // sampling is stochastic.
    RenderConfig c;
    c.type = FractalType::Mandelbrot;
    c.center_x = -0.5; c.center_y = 0.0; c.scale = 1.4;
    c.width = 64; c.height = 64;
    c.max_iter = 200;
    c.samples = 1.0; // 1M seeds -> plenty for a 64x64 grid
    parsePalette("gold", c.palette);

    auto img = renderBuddhabrot(c);
    CHECK(img.size() == static_cast<size_t>(c.width) * c.height * 3);

    // Some pixels must be brighter than the palette's dark anchor.
    int bright = 0;
    for (size_t i = 0; i < img.size(); i += 3)
        if (img[i] > 40 || img[i + 1] > 40 || img[i + 2] > 40) ++bright;
    CHECK(bright > 20);

    // center_y == 0 -> the density is mirrored across the horizontal midline.
    // Compare a band of rows to their reflection; most should match closely.
    const int W = c.width, H = c.height;
    int sampled = 0, symmetric = 0;
    for (int y = 0; y < H / 2; ++y) {
        for (int x = 0; x < W; ++x) {
            int a = img[(y * W + x) * 3];
            int b = img[((H - 1 - y) * W + x) * 3];
            ++sampled;
            if (std::abs(a - b) <= 8) ++symmetric;
        }
    }
    CHECK(symmetric > sampled * 9 / 10); // mirror symmetry holds

    // Nebula mode produces a same-sized buffer too.
    RenderConfig n = c;
    n.nebula = true; n.nebula_r = 200; n.nebula_g = 60; n.nebula_b = 20;
    auto neb = renderBuddhabrot(n);
    CHECK(neb.size() == static_cast<size_t>(W) * H * 3);
}

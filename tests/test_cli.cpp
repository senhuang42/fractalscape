// test_cli.cpp — argument parsing into RenderConfig / VideoConfig.
#include "test_util.h"
#include "cli.h"

#include <cmath>

using namespace fractal;

static ParsedArgs parse(std::vector<std::string> a) { return parseArgs(a); }

void test_cli() {
    // ---- dispatch ----
    CHECK(parse({}).kind == CommandKind::Help);
    CHECK(parse({"help"}).kind == CommandKind::Help);
    CHECK(parse({"-h"}).kind == CommandKind::Help);
    CHECK(!parse({"bogus"}).error.empty()); // unknown command

    // ---- render defaults ----
    {
        auto p = parse({"render"});
        CHECK(p.error.empty());
        CHECK(p.kind == CommandKind::Render);
        CHECK(p.render.type == FractalType::Julia);
        CHECK(p.render.output == "fractal.png");
        CHECK(p.render.ssaa == 4);
        CHECK(p.render.max_iter == 400);
        CHECK(p.render.palette.size() >= 2); // resolved from "aurora"
    }

    // ---- video defaults ----
    {
        auto p = parse({"video"});
        CHECK(p.error.empty());
        CHECK(p.kind == CommandKind::Video);
        CHECK(p.video.output == "fractal.mp4");
        CHECK(p.video.mode == AnimMode::Rotate);
        CHECK(p.video.ssaa == 2); // lowered for video
        CHECK(p.video.total_frames() == 600); // 20s * 30fps
    }

    // ---- common options ----
    {
        auto p = parse({"render", "-t", "mandelbrot", "--zoom", "2",
                        "-i", "100", "-o", "x.png", "--size", "800x600"});
        CHECK(p.error.empty());
        CHECK(p.render.type == FractalType::Mandelbrot);
        CHECK_NEAR(p.render.scale, 1.35 / 2.0, 1e-9);
        CHECK(p.render.max_iter == 100);
        CHECK(p.render.output == "x.png");
        CHECK(p.render.width == 800 && p.render.height == 600);
    }

    // ---- custom palette + fidelity knobs ----
    {
        auto p = parse({"render", "-p", "#000000,#ff8800,#00ffcc",
                        "--shading", "0.8", "--glow", "0.3"});
        CHECK(p.error.empty());
        CHECK(p.render.palette.size() == 3);
        CHECK_NEAR(p.render.shading, 0.8, 1e-9);
        CHECK_NEAR(p.render.glow, 0.3, 1e-9);
    }

    // ---- video options ----
    {
        auto p = parse({"video", "--mode", "zoom", "--fps", "24", "-d", "10",
                        "--zoom-end", "0.001"});
        CHECK(p.error.empty());
        CHECK(p.video.mode == AnimMode::Zoom);
        CHECK(p.video.fps == 24);
        CHECK(p.video.total_frames() == 240);
        CHECK_NEAR(p.video.zoom_end, 0.001, 1e-12);
    }
    {
        // spin mode (kaleidoscope rotation loop)
        auto p = parse({"video", "--mode", "spin"});
        CHECK(p.error.empty());
        CHECK(p.video.mode == AnimMode::Spin);
    }
    CHECK(!parse({"video", "--mode", "bogus"}).error.empty());

    // ---- presets ----
    {
        auto p = parse({"render", "-P", "frostbite"});
        CHECK(p.error.empty());
        CHECK(p.render.type == FractalType::Julia);
        CHECK_NEAR(p.render.julia_cre, -0.7269, 1e-9);
        CHECK(p.render.max_iter == 3000);
        CHECK(p.render.palette.size() >= 2); // frost resolved
    }
    {
        auto p = parse({"render", "--preset", "ember-seahorse"});
        CHECK(p.error.empty());
        CHECK(p.render.type == FractalType::Mandelbrot);
    }
    {
        // acid-swirl: trippy fuchsia/teal on the default spiral, cyclic prism
        auto p = parse({"render", "-P", "acid-swirl"});
        CHECK(p.error.empty());
        CHECK(p.render.type == FractalType::Julia);
        CHECK_NEAR(p.render.julia_cre, -0.512511, 1e-9);
        CHECK(p.render.cyclic);
        CHECK_NEAR(p.render.stripe_freq, 9.0, 1e-9);
        CHECK(p.render.palette.size() >= 2); // prism resolved
    }
    {
        // explicit flags override the preset regardless of order
        auto p = parse({"render", "-P", "frostbite", "--cre", "-0.8"});
        CHECK(p.error.empty());
        CHECK_NEAR(p.render.julia_cre, -0.8, 1e-9);
    }
    {
        // album-art design flags parse into the config
        auto p = parse({"render", "--kaleido", "8", "--aberration", "4.5",
                        "--vignette", "0.4", "--grain", "0.03", "--scanlines", "0.2"});
        CHECK(p.error.empty());
        CHECK_NEAR(p.render.kaleido, 8.0, 1e-9);
        CHECK_NEAR(p.render.aberration, 4.5, 1e-9);
        CHECK_NEAR(p.render.vignette, 0.4, 1e-9);
        CHECK_NEAR(p.render.grain, 0.03, 1e-9);
        CHECK_NEAR(p.render.scanlines, 0.2, 1e-9);
    }
    {
        // cover-mandala preset wires up the kaleidoscope mode
        auto p = parse({"render", "-P", "cover-mandala"});
        CHECK(p.error.empty());
        CHECK_NEAR(p.render.kaleido, 8.0, 1e-9);
        CHECK(p.render.cyclic);
        CHECK(p.render.vignette > 0.0);
        CHECK(p.render.palette.size() >= 2); // vice resolved
    }
    {
        // --formula axis + family presets
        auto p = parse({"render", "--formula", "burningship"});
        CHECK(p.error.empty());
        CHECK(p.render.formula == Formula::BurningShip);
        CHECK(parse({"render", "--formula", "newton"}).render.formula == Formula::Newton);
        CHECK(parse({"render", "--formula", "tricorn"}).render.formula == Formula::Tricorn);
        auto ph = parse({"render", "--formula", "phoenix", "--phoenix-p", "-0.4"});
        CHECK(ph.render.formula == Formula::Phoenix);
        CHECK_NEAR(ph.render.phoenix_pre, -0.4, 1e-9);
        CHECK(!parse({"render", "--formula", "bogus"}).error.empty());
        CHECK(parse({"render", "-P", "burning-ship"}).render.formula == Formula::BurningShip);
        CHECK(parse({"render", "-P", "newton"}).render.formula == Formula::Newton);
        CHECK(parse({"render", "-P", "phoenix"}).render.formula == Formula::Phoenix);
    }
    {
        // deep zoom flag
        CHECK(!parse({"render"}).render.deep);
        CHECK(parse({"render", "--deep"}).render.deep);
    }
    CHECK(!parse({"render", "-P", "bogus"}).error.empty());

    // ---- error handling ----
    CHECK(!parse({"render", "--type", "bogus"}).error.empty());
    CHECK(!parse({"render", "-i", "abc"}).error.empty());     // non-integer
    CHECK(!parse({"render", "--palette", "nope"}).error.empty());
    CHECK(!parse({"render", "--ssaa", "99"}).error.empty());  // out of range
    CHECK(!parse({"render", "--zoom", "-1"}).error.empty());  // non-positive
    CHECK(!parse({"render", "--unknownflag"}).error.empty());
    CHECK(!parse({"render", "--scale"}).error.empty());       // missing value
}

// main.cpp — CLI entry point. Ties together argument parsing, gradient
// generation, the GPU renderer, PNG output, and MP4 export via ffmpeg.

#include "buddhabrot.h"
#include "cli.h"
#include "explorer.h"
#include "palette.h"
#include "periodic.h"
#include "renderer.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kGradientResolution = 1024;

const char* kShaderDirEnv = "FRACTAL_SHADER_DIR"; // optional override

std::string shaderDirFromEnv() {
    const char* e = std::getenv(kShaderDirEnv);
    return e ? std::string(e) : std::string();
}

int runRender(const fractal::RenderConfig& cfg) {
    fractal::Renderer r;
    std::string err;
    if (!r.init(cfg.width, cfg.height, cfg.ssaa, shaderDirFromEnv(), err)) {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    // Gradient stays SMOOTH here; posterization is applied at sample time in
    // the shader (uPosterize). Applying it twice would double-quantize and the
    // shader path also keeps banding consistent under color_offset animation.
    auto grad = fractal::buildGradient(cfg.palette, kGradientResolution, cfg.cyclic,
                                       cfg.interp, 0);
    r.setPalette(grad, kGradientResolution);
    // Optional secondary palettes (empty -> renderer falls back to main).
    if (!cfg.stripe_palette.empty()) {
        auto sg = fractal::buildGradient(cfg.stripe_palette, kGradientResolution, cfg.cyclic,
                                         cfg.interp, 0);
        r.setStripePalette(sg, kGradientResolution);
    } else {
        r.setStripePalette({}, 0);
    }
    if (!cfg.inside_palette.empty()) {
        auto ig = fractal::buildGradient(cfg.inside_palette, kGradientResolution, cfg.cyclic,
                                         cfg.interp, 0);
        r.setInsidePalette(ig, kGradientResolution);
    } else {
        r.setInsidePalette({}, 0);
    }
    // Hybrid nebula accent / hue-shift / bloom-mask: pre-render the Buddhabrot
    // at the same view as the fractal so it overlays in alignment. The shader
    // sample interprets the texture differently depending on uNebulaRgb:
    //   mono (default) -> single-channel density tinted by uNebulaColor
    //   rgb            -> 3-channel R/G/B from three iter thresholds (lifetime)
    // Skip when --deep (CPU buddhabrot is float-precision, no df64 alignment).
    auto smoothBuffer = [](std::vector<uint8_t>& rgb, int W, int H) {
        // Separable 3-tap box blur on the nebula RGB buffer. Buddhabrot density
        // is inherently smooth (it's a marginal of a continuous distribution),
        // so the high-frequency speckle in the rendered buffer is purely shot
        // noise from sparse sampling -- especially in RGB mode where each
        // channel splits the sample budget further. A tiny box blur absorbs
        // that noise without softening the underlying density structure.
        if ((int)rgb.size() != W * H * 3) return;
        std::vector<uint8_t> tmp(rgb.size());
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                int xm = std::max(0, x - 1), xp = std::min(W - 1, x + 1);
                for (int c = 0; c < 3; ++c) {
                    int s = rgb[(y * W + xm) * 3 + c]
                          + rgb[(y * W + x ) * 3 + c]
                          + rgb[(y * W + xp) * 3 + c];
                    tmp[(y * W + x) * 3 + c] = (uint8_t)(s / 3);
                }
            }
        }
        for (int y = 0; y < H; ++y) {
            int ym = std::max(0, y - 1), yp = std::min(H - 1, y + 1);
            for (int x = 0; x < W; ++x) {
                for (int c = 0; c < 3; ++c) {
                    int s = tmp[(ym * W + x) * 3 + c]
                          + tmp[(y  * W + x) * 3 + c]
                          + tmp[(yp * W + x) * 3 + c];
                    rgb[(y * W + x) * 3 + c] = (uint8_t)(s / 3);
                }
            }
        }
    };

    bool need_nebula = !cfg.deep &&
                       (cfg.nebula_accent > 0.0 || cfg.nebula_hue_shift != 0.0 ||
                        cfg.nebula_bloom > 0.0);
    if (need_nebula) {
        fractal::RenderConfig nb = cfg;
        nb.samples = cfg.nebula_accent_samples;
        nb.nebula = cfg.nebula_rgb;     // 3-channel only when explicitly asked
        if (!cfg.nebula_rgb) {
            // Mono path: R = density, untouched by palette tint.
            if (!fractal::parsePalette("mono", nb.palette)) nb.palette = {{0,0,0},{1,1,1}};
            nb.cyclic = false;
        }
        std::cerr << "nebula pre-pass: " << cfg.nebula_accent_samples
                  << "M samples @ " << cfg.width << "x" << cfg.height
                  << (cfg.nebula_rgb ? " (RGB lifetime mode)" : " (mono density)")
                  << "...\n";
        auto density = fractal::renderBuddhabrot(nb);
        // RGB mode splits the sample budget across three channels, so per-
        // channel shot noise is much worse -- especially at zoomed views
        // where only a fraction of seeds land in the viewport. Stack passes
        // (each is a 1x3 separable box; 4 passes -> ~gaussian sigma ~2.5
        // pixels) when in RGB mode to absorb the chromatic speckle. The
        // buddhabrot density is smooth at the pixel scale anyway so detail
        // loss from blurring is invisible.
        int passes = cfg.nebula_rgb ? 4 : 1;
        for (int i = 0; i < passes; ++i) smoothBuffer(density, cfg.width, cfg.height);
        r.setNebulaTexture(density, cfg.width, cfg.height);
    } else {
        r.setNebulaTexture({}, 0, 0);
    }
    r.render(cfg);
    auto pixels = r.readPixels();

    if (!stbi_write_png(cfg.output.c_str(), cfg.width, cfg.height, 3,
                        pixels.data(), cfg.width * 3)) {
        std::cerr << "error: failed to write " << cfg.output << "\n";
        return 1;
    }
    std::cout << "wrote " << cfg.output << " (" << cfg.width << "x" << cfg.height
              << ", ssaa " << cfg.ssaa << ")\n";
    return 0;
}

int runBuddhabrot(const fractal::RenderConfig& cfg) {
    std::cout << "buddhabrot: " << (cfg.nebula ? "nebula " : "")
              << cfg.samples << "M samples @ " << cfg.width << "x" << cfg.height
              << " (" << std::thread::hardware_concurrency() << " threads)\n";
    auto progress = [](float f) {
        std::printf("\r  %.0f%%", 100.0f * f);
        std::fflush(stdout);
    };
    auto pixels = fractal::renderBuddhabrot(cfg, progress);
    std::printf("\n");

    if (!stbi_write_png(cfg.output.c_str(), cfg.width, cfg.height, 3,
                        pixels.data(), cfg.width * 3)) {
        std::cerr << "error: failed to write " << cfg.output << "\n";
        return 1;
    }
    std::cout << "wrote " << cfg.output << "\n";
    return 0;
}

int runExplore(const fractal::RenderConfig& cfg) {
    return fractal::runExplorer(cfg, shaderDirFromEnv());
}

int runCenter(const fractal::ParsedArgs& a) {
    const fractal::RenderConfig& cfg = a.render;
    const bool mandel = cfg.type == fractal::FractalType::Mandelbrot;
    const bool misiu  = mandel && a.misiurewicz;
    const fractal::cdouble guess(cfg.center_x, cfg.center_y);

    fractal::PeriodicPoint best;
    if (misiu) {
        best = fractal::findMisiurewicz(guess, a.max_preperiod, a.max_period, cfg.scale);
    } else if (a.find_period > 0) {
        best = mandel ? fractal::mandelNucleusNewton(guess, a.find_period)
                      : fractal::juliaPeriodicNewton(
                            fractal::cdouble(cfg.julia_cre, cfg.julia_cim),
                            guess, a.find_period);
        best.dist = std::abs(best.point - guess);
        if (!best.converged) best.period = 0; // signal failure below
    } else {
        best = fractal::findCenter(cfg, guess, a.max_period, cfg.scale);
    }

    const char* kind = misiu ? "Misiurewicz point"
                      : mandel ? "nucleus" : "repelling periodic point";
    if (best.period == 0) {
        std::cerr << "no " << kind << " found within radius " << cfg.scale
                  << " of the guess (try a larger --max-period"
                  << (misiu ? "/--max-preperiod" : "") << " or --scale, "
                  << "or re-aim the guess)\n";
        return 1;
    }

    if (misiu) std::printf("%s, preperiod %d period %d\n", kind, best.preperiod, best.period);
    else       std::printf("%s, period %d\n", kind, best.period);
    std::printf("  center-x  %.16g\n", best.point.real());
    std::printf("  center-y  %.16g\n", best.point.imag());
    std::printf("  |multiplier|  %.6g", best.abs_lambda);
    if (!mandel || misiu) {
        // For a logarithmic spiral the multiplier's argument is the rotation per
        // period and |multiplier| the zoom factor per period -- handy context.
        std::printf("   (spiral: x%.4g and %.1f deg per period %d)",
                    best.abs_lambda,
                    std::arg(best.lambda) * 180.0 / M_PI, best.period);
    }
    std::printf("\n  distance from guess  %.3g\n", best.dist);

    // A ready-to-run deep zoom into the refined center.
    char cmd[512];
    if (mandel) {
        std::snprintf(cmd, sizeof cmd,
            "fractal video -t mandelbrot --mode zoom --deep -i 40000 "
            "--center-x %.16g --center-y %.16g "
            "--zoom-target-x %.16g --zoom-target-y %.16g "
            "--scale 0.2 --zoom-end %s -o zoom.mp4",
            best.point.real(), best.point.imag(),
            best.point.real(), best.point.imag(),
            misiu ? "5e-13" : "1e-7");
    } else {
        std::snprintf(cmd, sizeof cmd,
            "fractal video --mode zoom --deep -i 30000 --cre %.10g --cim %.10g "
            "--center-x %.16g --center-y %.16g "
            "--zoom-target-x %.16g --zoom-target-y %.16g "
            "--scale 0.1 --zoom-end 1e-12 -o zoom.mp4",
            cfg.julia_cre, cfg.julia_cim,
            best.point.real(), best.point.imag(),
            best.point.real(), best.point.imag());
    }
    std::printf("\n%s\n", cmd);
    return 0;
}

// Compute the per-frame config for a given animation frame in [0, total).
fractal::RenderConfig frameConfig(const fractal::VideoConfig& v, int frame) {
    fractal::RenderConfig c = static_cast<fractal::RenderConfig>(v);
    const int   N = v.total_frames();
    const double u = (N > 0) ? static_cast<double>(frame) / N : 0.0; // [0,1)

    switch (v.mode) {
        case fractal::AnimMode::Rotate: {
            // Julia constant orbits the origin -> seamless loop.
            const double theta = 2.0 * M_PI * u;
            c.type = fractal::FractalType::Julia;
            c.julia_cre = v.rotate_radius * std::cos(theta);
            c.julia_cim = v.rotate_radius * std::sin(theta);
            break;
        }
        case fractal::AnimMode::Zoom: {
            // Exponential zoom from start scale to zoom_end, easing the center.
            const double f = (N > 1) ? static_cast<double>(frame) / (N - 1) : 0.0;
            c.scale = v.scale * std::pow(v.zoom_end / v.scale, f);
            c.center_x = v.center_x + (v.zoom_target_x - v.center_x) * f;
            c.center_y = v.center_y + (v.zoom_target_y - v.center_y) * f;
            break;
        }
        case fractal::AnimMode::Cycle: {
            // Sweep the palette one full cycle -> seamless loop.
            c.color_offset = v.color_offset + u;
            break;
        }
        case fractal::AnimMode::Spin: {
            // Rotate the kaleidoscope axis a full turn. A full 360 wraps atan()
            // and every sample returns to its start, so the loop is seamless.
            // (One *segment* is NOT enough: the fractal has no rotational
            // symmetry, so each wedge samples a different slice as the axis
            // turns -- the mandala spins AND morphs, returning only after 360.)
            c.kaleido_angle = v.kaleido_angle + u * 360.0;
            break;
        }
    }

    // Optional palette sweep layered on top of whatever the mode did. Lets a
    // zoom (or any mode) cycle colors as it runs; an integer count keeps a loop
    // seamless since color_offset is taken mod 1 when sampling the gradient.
    if (v.color_cycles != 0.0) c.color_offset += v.color_cycles * u;

    return c;
}

int runVideo(const fractal::VideoConfig& cfg) {
    // x264 + yuv420p needs even dimensions.
    fractal::VideoConfig v = cfg;
    if (v.width  % 2) { v.width--;  std::cerr << "note: width rounded to "  << v.width  << " (even required)\n"; }
    if (v.height % 2) { v.height--; std::cerr << "note: height rounded to " << v.height << " (even required)\n"; }

    fractal::Renderer r;
    std::string err;
    if (!r.init(v.width, v.height, v.ssaa, shaderDirFromEnv(), err)) {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    auto grad = fractal::buildGradient(v.palette, kGradientResolution, v.cyclic, v.interp, 0);
    r.setPalette(grad, kGradientResolution);
    if (!v.stripe_palette.empty()) {
        auto sg = fractal::buildGradient(v.stripe_palette, kGradientResolution, v.cyclic, v.interp, 0);
        r.setStripePalette(sg, kGradientResolution);
    } else {
        r.setStripePalette({}, 0);
    }
    if (!v.inside_palette.empty()) {
        auto ig = fractal::buildGradient(v.inside_palette, kGradientResolution, v.cyclic, v.interp, 0);
        r.setInsidePalette(ig, kGradientResolution);
    } else {
        r.setInsidePalette({}, 0);
    }

    const int N = v.total_frames();
    if (N < 1) { std::cerr << "error: nothing to render (duration/fps too small)\n"; return 1; }

    // Pipe raw RGB frames straight into ffmpeg.
    std::string cmd =
        "ffmpeg -y -hide_banner -loglevel error "
        "-f rawvideo -pixel_format rgb24 "
        "-video_size " + std::to_string(v.width) + "x" + std::to_string(v.height) + " "
        "-framerate " + std::to_string(v.fps) + " -i - "
        "-c:v libx264 -pix_fmt yuv420p -crf " + std::to_string(v.crf) + " "
        "-movflags +faststart \"" + v.output + "\"";

    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) { std::cerr << "error: failed to launch ffmpeg (is it installed?)\n"; return 1; }

    // Producer/consumer split: this thread does all the OpenGL (render +
    // readback) and pushes finished RGB frames into a small bounded queue; a
    // writer thread drains the queue into the ffmpeg pipe. That overlaps the
    // CPU-side encode/I/O of frame N with the GPU render of frame N+1. (Heavy
    // frames stay GPU-bound — there's one GPU — but on light frames the encode
    // side is a real slice of wall time, and this hides it.) GL stays on this
    // thread; only fwrite happens on the writer.
    std::deque<std::vector<uint8_t>> queue;
    std::mutex mtx;
    std::condition_variable cv_space, cv_data;
    bool producer_done = false;
    std::atomic<bool> write_failed{false};
    constexpr size_t kMaxQueued = 3; // ~3 frames of backpressure

    std::thread writer([&] {
        for (;;) {
            std::vector<uint8_t> frame;
            {
                std::unique_lock<std::mutex> lk(mtx);
                cv_data.wait(lk, [&] { return !queue.empty() || producer_done; });
                if (queue.empty()) break; // drained and producer finished
                frame = std::move(queue.front());
                queue.pop_front();
            }
            cv_space.notify_one();
            if (!write_failed.load() &&
                fwrite(frame.data(), 1, frame.size(), pipe) != frame.size()) {
                write_failed = true;
            }
        }
    });

    std::cout << "rendering " << N << " frames @ " << v.fps << " fps -> " << v.output << "\n";
    for (int frame = 0; frame < N && !write_failed.load(); ++frame) {
        fractal::RenderConfig fc = frameConfig(v, frame);
        r.render(fc);
        auto pixels = r.readPixels();
        {
            std::unique_lock<std::mutex> lk(mtx);
            cv_space.wait(lk, [&] { return queue.size() < kMaxQueued || write_failed.load(); });
            if (write_failed.load()) break;
            queue.push_back(std::move(pixels));
        }
        cv_data.notify_one();
        if (frame % 10 == 0 || frame == N - 1) {
            std::printf("\r  frame %d/%d (%.0f%%)", frame + 1, N, 100.0 * (frame + 1) / N);
            std::fflush(stdout);
        }
    }
    {
        std::lock_guard<std::mutex> lk(mtx);
        producer_done = true;
    }
    cv_data.notify_one();
    writer.join();
    std::printf("\n");

    if (write_failed.load()) {
        std::cerr << "error: ffmpeg pipe closed early (encode failed)\n";
        pclose(pipe);
        return 1;
    }

    int status = pclose(pipe);
    if (status != 0) {
        std::cerr << "error: ffmpeg exited with status " << status << "\n";
        return 1;
    }
    std::cout << "wrote " << v.output << " (" << v.width << "x" << v.height
              << ", " << v.duration << "s, ssaa " << v.ssaa << ")\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    fractal::ParsedArgs parsed = fractal::parseArgs(args);

    if (!parsed.error.empty()) {
        std::cerr << "error: " << parsed.error << "\n\n"
                  << "Run 'fractal help' for usage.\n";
        return 2;
    }
    switch (parsed.kind) {
        case fractal::CommandKind::Help:
            std::cout << fractal::helpText();
            return 0;
        case fractal::CommandKind::Render:
            return runRender(parsed.render);
        case fractal::CommandKind::Video:
            return runVideo(parsed.video);
        case fractal::CommandKind::Buddha:
            return runBuddhabrot(parsed.render);
        case fractal::CommandKind::Explore:
            return runExplore(parsed.render);
        case fractal::CommandKind::Center:
            return runCenter(parsed);
        default:
            std::cout << fractal::helpText();
            return 0;
    }
}

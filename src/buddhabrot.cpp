#include "buddhabrot.h"
#include "palette.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>

namespace fractal {

namespace {

// A point on the complex plane mapped to a pixel in a top-down (row 0 = top)
// buffer, mirroring the shader's plane mapping: uv = (frag - 0.5*res)/res.y,
// plane = center + uv * 2*scale. Returns false if the point falls outside.
struct Mapper {
    int W, H;
    double cx, cy, scale;
    bool toPixel(double x, double y, int& col, int& rowTop) const {
        const double inv = 1.0 / (2.0 * scale);
        const double fragx = (x - cx) * inv * H + 0.5 * W;
        const double fragy = (y - cy) * inv * H + 0.5 * H; // GL bottom-up
        col = static_cast<int>(std::floor(fragx));
        const int fyb = static_cast<int>(std::floor(fragy));
        if (col < 0 || col >= W || fyb < 0 || fyb >= H) return false;
        rowTop = H - 1 - fyb; // flip to top-down
        return true;
    }
};

// Skip the two big interior components of the Mandelbrot set analytically:
// the main cardioid and the period-2 bulb. These never escape, so they'd be
// discarded anyway -- but catching them up front avoids burning max_iter
// iterations on the most-sampled region of the plane.
inline bool inMandelInterior(double cx, double cy) {
    const double xc = cx - 0.25;
    const double q  = xc * xc + cy * cy;
    if (q * (q + xc) < 0.25 * cy * cy) return true;          // main cardioid
    if ((cx + 1.0) * (cx + 1.0) + cy * cy < 0.0625) return true; // period-2 bulb
    return false;
}

// One worker: scatter `budget` random orbits into its private channel buffers.
// `chan[k]` is a W*H accumulation buffer; nChan is 1 (density) or 3 (nebula).
void worker(const RenderConfig& cfg, long budget, uint32_t seed,
            const Mapper& map, int nChan, std::vector<std::vector<uint32_t>>& chan,
            std::atomic<long>& done) {
    const int W = cfg.width;
    const bool mandel = (cfg.type == FractalType::Mandelbrot);
    const bool mirror = mandel && cfg.center_y == 0.0; // set is symmetric about Im=0
    const double bail2 = cfg.bailout * cfg.bailout;

    const int thr[3] = {cfg.nebula_b, cfg.nebula_g, cfg.nebula_r}; // B,G,R order
    const int cap = cfg.nebula ? std::max({cfg.nebula_r, cfg.nebula_g, cfg.nebula_b})
                               : cfg.max_iter;

    // Sampling region. Mandelbrot: the set's bounding box (upper half only when
    // mirroring). Julia: the |z|<=2 box that contains every filled Julia set.
    double sx0, sx1, sy0, sy1;
    if (mandel) {
        sx0 = -2.2; sx1 = 0.8;
        sy0 = mirror ? 0.0 : -1.3; sy1 = 1.3;
    } else {
        sx0 = -2.0; sx1 = 2.0; sy0 = -2.0; sy1 = 2.0;
    }

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> ux(sx0, sx1), uy(sy0, sy1);

    std::vector<float> tx, ty; // reused trajectory buffer
    tx.reserve(static_cast<size_t>(cap));
    ty.reserve(static_cast<size_t>(cap));

    const double jcx = cfg.julia_cre, jcy = cfg.julia_cim;
    long since = 0;
    for (long s = 0; s < budget; ++s) {
        const double a = ux(rng), b = uy(rng);
        double zx, zy, cx, cy;
        if (mandel) {
            if (inMandelInterior(a, b)) { if (++since >= 65536) { done += since; since = 0; } continue; }
            cx = a; cy = b; zx = 0.0; zy = 0.0;
        } else {
            cx = jcx; cy = jcy; zx = a; zy = b;
        }

        tx.clear(); ty.clear();
        bool escaped = false;
        int escIter = 0;
        for (int i = 0; i < cap; ++i) {
            tx.push_back(static_cast<float>(zx));
            ty.push_back(static_cast<float>(zy));
            const double nx = zx * zx - zy * zy + cx;
            const double ny = 2.0 * zx * zy + cy;
            zx = nx; zy = ny;
            if (zx * zx + zy * zy > bail2) { escaped = true; escIter = i + 1; break; }
        }

        if (escaped) {
            if (cfg.nebula) {
                // Splat into every channel whose threshold this orbit beat.
                for (int k = 0; k < 3; ++k) {
                    if (escIter > thr[k]) continue;
                    auto& buf = chan[k];
                    for (size_t j = 0; j < tx.size(); ++j) {
                        int col, row;
                        if (map.toPixel(tx[j], ty[j], col, row)) buf[row * W + col]++;
                        if (mirror && map.toPixel(tx[j], -ty[j], col, row)) buf[row * W + col]++;
                    }
                }
            } else {
                auto& buf = chan[0];
                for (size_t j = 0; j < tx.size(); ++j) {
                    int col, row;
                    if (map.toPixel(tx[j], ty[j], col, row)) buf[row * W + col]++;
                    if (mirror && map.toPixel(tx[j], -ty[j], col, row)) buf[row * W + col]++;
                }
            }
        }
        if (++since >= 65536) { done += since; since = 0; }
    }
    (void)nChan;
    done += since;
}

} // namespace

std::vector<uint8_t> renderBuddhabrot(const RenderConfig& cfg,
                                      const std::function<void(float)>& progress) {
    const int W = cfg.width, H = cfg.height;
    const int nChan = cfg.nebula ? 3 : 1;
    const size_t N = static_cast<size_t>(W) * H;

    const Mapper map{W, H, cfg.center_x, cfg.center_y, cfg.scale};
    const long total = std::max<long>(1, static_cast<long>(cfg.samples * 1e6));

    unsigned T = std::max(1u, std::thread::hardware_concurrency());
    // Cap threads so per-thread buffers don't blow up memory on big nebulae.
    T = std::min<unsigned>(T, 12);

    std::vector<std::vector<std::vector<uint32_t>>> perThread(T);
    for (auto& pt : perThread) {
        pt.assign(nChan, std::vector<uint32_t>(N, 0u));
    }

    std::atomic<long> done{0};
    std::vector<std::thread> pool;
    const long per = (total + T - 1) / T;
    for (unsigned t = 0; t < T; ++t) {
        const long budget = std::min<long>(per, total - static_cast<long>(t) * per);
        if (budget <= 0) break;
        pool.emplace_back(worker, std::cref(cfg), budget, 0x9E3779B9u * (t + 1),
                          std::cref(map), nChan, std::ref(perThread[t]), std::ref(done));
    }

    // Poll for progress while the workers run.
    if (progress) {
        for (;;) {
            const long d = done.load();
            progress(std::min(1.0, static_cast<double>(d) / total));
            if (d >= total) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
    }
    for (auto& th : pool) th.join();
    if (progress) progress(1.0f);

    // Merge per-thread channel buffers.
    std::vector<std::vector<uint32_t>> chan(nChan, std::vector<uint32_t>(N, 0u));
    for (auto& pt : perThread)
        for (int k = 0; k < nChan; ++k)
            for (size_t i = 0; i < N; ++i) chan[k][i] += pt[k][i];

    // Tone map. Normalize against a high percentile rather than the raw max:
    // Buddhabrot density has a long tail (a handful of pixels in attractor
    // regions can be orders of magnitude hotter than everything else), and
    // normalizing by the true max crushes the whole image to black. The 99.9th
    // percentile clips just those few outliers to white and exposes the body.
    auto whitePoint = [&](const std::vector<uint32_t>& buf) -> double {
        std::vector<uint32_t> tmp(buf);
        size_t idx = static_cast<size_t>(0.999 * (N - 1));
        std::nth_element(tmp.begin(), tmp.begin() + idx, tmp.end());
        return std::max<double>(1.0, tmp[idx]);
    };
    const double invg = 1.0 / std::max(0.1, cfg.buddha_gamma);
    std::vector<uint8_t> out(N * 3, 0);

    if (cfg.nebula) {
        double wp[3] = {whitePoint(chan[0]), whitePoint(chan[1]), whitePoint(chan[2])};
        for (size_t i = 0; i < N; ++i) {
            for (int k = 0; k < 3; ++k) {
                const double v = std::pow(std::clamp(chan[k][i] / wp[k], 0.0, 1.0), invg);
                // chan order is B,G,R -> RGB out is [R,G,B] = [k2,k1,k0].
                out[i * 3 + (2 - k)] = static_cast<uint8_t>(std::lround(v * 255.0));
            }
        }
    } else {
        const double wp = whitePoint(chan[0]);
        const int gres = 1024;
        std::vector<uint8_t> grad = buildGradient(cfg.palette, gres, cfg.cyclic);
        for (size_t i = 0; i < N; ++i) {
            const double v = std::pow(std::clamp(chan[0][i] / wp, 0.0, 1.0), invg);
            int gi = static_cast<int>(std::lround(v * (gres - 1)));
            out[i * 3 + 0] = grad[gi * 3 + 0];
            out[i * 3 + 1] = grad[gi * 3 + 1];
            out[i * 3 + 2] = grad[gi * 3 + 2];
        }
    }
    return out;
}

} // namespace fractal

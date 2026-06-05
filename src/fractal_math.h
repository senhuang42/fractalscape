// fractal_math.h — CPU reference implementation of the escape-time iteration.
//
// This mirrors the math in shaders/fractal.frag exactly. The GPU is the
// production path; this CPU version exists so the core algorithm can be unit
// tested deterministically without a GL context, and so render output can be
// spot-checked against a known-good reference.
#pragma once

#include <cmath>
#include <complex>

#include "config.h" // Formula

namespace fractal {

struct EscapeResult {
    int    iter;      // iterations taken (== max_iter if it never escaped)
    bool   escaped;   // true if |z| exceeded the bailout radius
    double smooth;    // continuous (fractional) iteration count; only meaningful when escaped
};

// Iterate z -> z^exponent + c starting from z0, until |z| > bailout or
// max_iter is reached. Returns the (smoothed) escape time.
//
// The smooth iteration count uses the standard normalized formula for the
// quadratic map; for non-quadratic exponents it uses the generalized
// log-of-log form. Both produce continuous bands suitable for gradient mapping.
inline EscapeResult escapeTime(std::complex<double> z,
                               std::complex<double> c,
                               int    max_iter,
                               double bailout,
                               double exponent = 2.0,
                               Formula formula = Formula::Quadratic) {
    const double bail2 = bailout * bailout;
    int i = 0;
    double mag2 = std::norm(z); // |z|^2
    for (; i < max_iter; ++i) {
        // Formula transform before squaring (mirrors the shader). Phoenix and
        // Newton are GPU-only and not modelled here.
        std::complex<double> zt = z;
        if      (formula == Formula::BurningShip) zt = {std::abs(z.real()), std::abs(z.imag())};
        else if (formula == Formula::Tricorn)     zt = std::conj(z);
        if (exponent == 2.0) {
            // Fast path matching the shader's z*z.
            const double zr = zt.real(), zi = zt.imag();
            z = std::complex<double>(zr * zr - zi * zi, 2.0 * zr * zi) + c;
        } else {
            z = std::pow(zt, exponent) + c;
        }
        mag2 = std::norm(z);
        if (mag2 > bail2) {
            // Continuous escape time. For power p:
            //   nu = log_p( log|z| / log(bailout) )
            //   smooth = i + 1 - nu
            const double log_zn = 0.5 * std::log(mag2); // = log|z|
            const double nu     = std::log(log_zn / std::log(bailout)) / std::log(exponent);
            return EscapeResult{i + 1, true, static_cast<double>(i + 1) - nu};
        }
    }
    return EscapeResult{max_iter, false, static_cast<double>(max_iter)};
}

// Convenience wrappers that pick z0/c per fractal family.
inline EscapeResult mandelbrot(double px, double py, int max_iter,
                               double bailout, double exponent = 2.0) {
    return escapeTime({0.0, 0.0}, {px, py}, max_iter, bailout, exponent);
}

inline EscapeResult julia(double px, double py, double cre, double cim,
                          int max_iter, double bailout, double exponent = 2.0) {
    return escapeTime({px, py}, {cre, cim}, max_iter, bailout, exponent);
}

// Orbit statistics that drive the literature coloring modes, mirroring the
// accumulators in shaders/fractal.frag:
//   * min_mag / min_iter — the orbit's closest approach to the origin and the
//     iteration it happened at (bof60 / bof61 "atom domain" interior modes).
//   * exp_sum — sum of exp(-1/min(|z_n - z_{n-1}|, |z_n - z_{n-2}|))
//     (exponential smoothing, convergent form; grows while the orbit is still
//     moving — the z_{n-2} term lets period-2 cycles read as converged).
//   * curv_avg — average turning angle |arg((z_n - z_{n-1})/(z_{n-1} -
//     z_{n-2}))|/pi over the orbit (curvature average relief, Haerkoenen 2007
//     eq 4.8; the shader skips the first two iterations the same way).
struct OrbitStats {
    bool   escaped;
    int    iter;
    double min_mag;   // min |z_n| over the orbit
    int    min_iter;  // iteration index of that minimum
    double exp_sum;
    double curv_avg;
};

inline OrbitStats orbitStats(std::complex<double> z,
                             std::complex<double> c,
                             int    max_iter,
                             double bailout) {
    const double bail2 = bailout * bailout;
    OrbitStats st{false, max_iter, 1e20, 0, 0.0, 0.0};
    std::complex<double> zprev = 0.0, zprev2 = 0.0;
    double curv_sum = 0.0;
    int    curv_n   = 0;
    double last     = 0.0;
    for (int i = 0; i < max_iter; ++i) {
        zprev2 = zprev;
        zprev  = z;
        z      = z * z + c;
        const double m2 = std::norm(z);
        if (m2 < st.min_mag * st.min_mag) { st.min_mag = std::sqrt(m2); st.min_iter = i; }
        if (i >= 2) st.exp_sum += std::exp(-1.0 / std::max(std::min(std::abs(z - zprev),
                                                                    std::abs(z - zprev2)), 1e-12));
        if (i >= 2) { // mirrors the shader's kStripeSkip + 1 for curvature
            const std::complex<double> den = zprev - zprev2;
            double term = last;
            if (std::norm(den) > 1e-30)
                term = std::abs(std::arg((z - zprev) / den)) / M_PI;
            last = term;
            curv_sum += term;
            curv_n++;
        }
        if (m2 > bail2) { st.escaped = true; st.iter = i + 1; break; }
    }
    st.curv_avg = curv_n > 0 ? curv_sum / curv_n : 0.0;
    return st;
}

// Distance estimate to the fractal boundary, mirroring shaders/fractal.frag.
// Tracks the derivative dz alongside z (z'_{n+1} = 2 z_n z'_n [+1 for the
// Mandelbrot dz/dc]). Returns a negative value for points that never escaped
// (treated as interior). Formula: d = sqrt(|z|^2/|dz|^2) * 0.5 * log(|z|^2).
// (iquilezles.org/articles/distancefractals)
inline double distanceEstimate(std::complex<double> z0,
                               std::complex<double> c,
                               bool   is_mandelbrot,
                               int    max_iter,
                               double bailout) {
    std::complex<double> z  = z0;
    std::complex<double> dz = is_mandelbrot ? std::complex<double>(0, 0)
                                            : std::complex<double>(1, 0);
    const std::complex<double> one = is_mandelbrot ? std::complex<double>(1, 0)
                                                   : std::complex<double>(0, 0);
    const double bail2 = bailout * bailout;
    double m2 = std::norm(z);
    for (int i = 0; i < max_iter; ++i) {
        dz = 2.0 * z * dz + one;
        z  = z * z + c;
        m2 = std::norm(z);
        if (m2 > bail2) {
            const double d2 = std::norm(dz);
            return std::sqrt(m2 / std::max(d2, 1e-20)) * 0.5 * std::log(m2);
        }
    }
    return -1.0; // interior
}

} // namespace fractal

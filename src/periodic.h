// periodic.h — Newton solver for exact deep-zoom centers.
//
// A good deep zoom must keep the frame centered on real structure at every
// scale, which means centering on a point that is self-similar to infinite
// depth. Those points are not arbitrary: the spiral "eyes" of a Julia set are
// REPELLING PERIODIC POINTS (z with f_c^p(z) = z), and the miniature copies in
// the Mandelbrot set are NUCLEI (parameters c whose critical orbit is periodic,
// f_c^p(0) = 0). Eyeballing a spiral gets you ~7 digits before it drifts
// off-frame; Newton's method refines that guess to an exact point.
//
// Precision subtlety: evaluating f^p amplifies rounding by the multiplier (which
// is ~1e5 for a period-79 spiral), so a plain `double` Newton pins a high-period
// center to only ~8 digits -- not enough for the df64 renderer's ~1e-13 wall. So
// the search runs in double-double (doubledouble.h, ~32 digits); the result is
// then accurate to full `double` (~16 digits), which is what RenderConfig holds
// and the renderer needs. The core is templated on the complex scalar type so
// the same code serves both precisions (and the double path stays unit-testable
// against known points). Quadratic map z -> z^2 + c only.
#pragma once

#include <complex>
#include <vector>

#include "config.h"
#include "doubledouble.h"

namespace fractal {

using cdouble = std::complex<double>;

struct PeriodicPoint {
    cdouble point;          // Julia: the periodic z*; Mandelbrot: the nucleus c
    int     period   = 0;   // the (true, minimal) period
    int     preperiod = 0;  // Misiurewicz preperiod k (0 = periodic / nucleus)
    cdouble lambda   = 0.0; // multiplier (f^p)'(z*); |.|<1 attracting, >1 repelling
    double  abs_lambda = 0.0;
    bool    converged = false;
    double  dist     = 0.0; // distance from the initial guess
};

namespace detail {

// Uniform scalar helpers so the templated core works for both complex<double>
// and cdd. (cdd's own absval/norm2/finite live in doubledouble.h.)
inline double cAbs(const cdouble& z)  { return std::abs(z); }
inline double cNorm2(const cdouble& z){ return std::norm(z); }
inline bool   cFinite(const cdouble& z){ return std::isfinite(z.real()) && std::isfinite(z.imag()); }
inline double cAbs(const cdd& z)   { return absval(z); }
inline double cNorm2(const cdd& z) { return norm2(z); }
inline bool   cFinite(const cdd& z){ return finite(z); }

// f^p(z) accumulating the derivative (f^p)'(z) = prod 2 z_k. Returns false if the
// orbit escapes (a stray Newton iterate), making the derivative meaningless.
template <class C>
bool juliaOrbit(C c, C z, int p, C& fp, C& deriv) {
    C w = z, d = C(1.0);
    for (int k = 0; k < p; ++k) {
        d = C(2.0) * w * d;          // chain rule: f'(w_k) = 2 w_k
        w = w * w + c;
        if (!cFinite(w) || cNorm2(w) > 1e12) return false;
    }
    fp = w; deriv = d;
    return true;
}

// True (minimal) period: smallest q | p with f^q(z*) ~ z*.
template <class C>
int truePeriod(C c, C z, int p, double tol) {
    for (int q = 1; q < p; ++q) {
        if (p % q != 0) continue;
        C w = z;
        for (int k = 0; k < q; ++k) w = w * w + c;
        if (cAbs(w - z) < tol) return q;
    }
    return p;
}

template <class C>
struct NewtonT { C point; C lambda; double abs_lambda = 0; bool converged = false; };

// Newton on F(z) = f^p(z) - z for a Julia periodic point.
template <class C>
NewtonT<C> juliaNewtonT(C c, C guess, int p, int iters, double tol, double resid) {
    NewtonT<C> r;
    C z = guess;
    for (int n = 0; n < iters; ++n) {
        C fp, d;
        if (!juliaOrbit(c, z, p, fp, d)) return r;
        C Fp = d - C(1.0);
        if (cAbs(Fp) < 1e-300) break;
        C step = (fp - z) / Fp;
        z = z - step;
        if (cAbs(step) < tol) { r.converged = true; break; }
    }
    C fp, d;
    if (!juliaOrbit(c, z, p, fp, d)) return r;
    r.converged = r.converged && cAbs(fp - z) < resid;
    r.point = z; r.lambda = d; r.abs_lambda = cAbs(d);
    return r;
}

// Newton on G(c) = z_p(c) for a Mandelbrot period-p nucleus; the critical orbit
// is z_0=0, z_{k+1}=z_k^2+c, with derivative dz_{k+1}=2 z_k dz_k + 1.
template <class C>
NewtonT<C> mandelNewtonT(C guess, int p, int iters, double tol, double resid) {
    NewtonT<C> r;
    C c = guess;
    for (int n = 0; n < iters; ++n) {
        C z = C(0.0), dz = C(0.0);
        bool ok = true;
        for (int k = 0; k < p; ++k) {
            dz = C(2.0) * z * dz + C(1.0);
            z  = z * z + c;
            if (!cFinite(z) || cNorm2(z) > 1e12) { ok = false; break; }
        }
        if (!ok || cAbs(dz) < 1e-300) return r;
        C step = z / dz;
        c = c - step;
        if (cAbs(step) < tol) { r.converged = true; break; }
    }
    // Residual + multiplier (prod 2 z_k over the periodic critical orbit; ~0).
    C z = C(0.0), lam = C(1.0);
    for (int k = 0; k < p; ++k) { lam = C(2.0) * z * lam; z = z * z + c; }
    r.converged = r.converged && cAbs(z) < resid;
    r.point = c; r.lambda = lam; r.abs_lambda = cAbs(lam);
    return r;
}

// Newton for a Mandelbrot MISIUREWICZ point M_{k,n}: the critical orbit is
// preperiodic, landing on an n-cycle after k steps, i.e. z_{k+n} = z_k. These
// are the spiral centers of the filigree -- self-similar with NO interior, so a
// zoom toward one stays on structure all the way down (a minibrot nucleus, by
// contrast, has a solid interior you fall into). Newton on
// G(c) = z_{k+n}(c) - z_k(c), G'(c) = dz_{k+n} - dz_k.
template <class C>
NewtonT<C> misiurewiczNewtonT(C guess, int k, int n, int iters, double tol, double resid) {
    NewtonT<C> r;
    C c = guess;
    for (int it = 0; it < iters; ++it) {
        C z = C(0.0), dz = C(0.0), zk = C(0.0), dzk = C(0.0);
        bool ok = true;
        for (int j = 0; j < k + n; ++j) {
            if (j == k) { zk = z; dzk = dz; }
            dz = C(2.0) * z * dz + C(1.0);
            z  = z * z + c;
            if (!cFinite(z) || cNorm2(z) > 1e12) { ok = false; break; }
        }
        if (!ok) return r;
        C Gp = dz - dzk;
        if (cAbs(Gp) < 1e-300) break;
        C step = (z - zk) / Gp;
        c = c - step;
        if (cAbs(step) < tol) { r.converged = true; break; }
    }
    // Residual z_{k+n}-z_k, and the cycle multiplier prod_{j=k}^{k+n-1} 2 z_j.
    C z = C(0.0), zk = C(0.0), lam = C(1.0);
    for (int j = 0; j < k; ++j) z = z * z + c;
    zk = z;
    for (int j = 0; j < n; ++j) { lam = C(2.0) * z * lam; z = z * z + c; }
    r.converged = r.converged && cAbs(z - zk) < resid;
    r.point = c; r.lambda = lam; r.abs_lambda = cAbs(lam);
    return r;
}

inline cdouble toCd(const cdouble& z) { return z; }
inline cdouble toCd(const cdd& z) { return cdouble((double)z.re, (double)z.im); }

} // namespace detail

// --- double-precision wrappers (exact-enough for low period; unit-tested) ---
inline PeriodicPoint juliaPeriodicNewton(cdouble c, cdouble guess, int p,
                                         int iters = 80, double tol = 1e-14) {
    auto r = detail::juliaNewtonT<cdouble>(c, guess, p, iters, tol, 1e-10);
    PeriodicPoint pp;
    pp.point = r.point; pp.period = p; pp.lambda = r.lambda;
    pp.abs_lambda = r.abs_lambda; pp.converged = r.converged;
    return pp;
}
inline PeriodicPoint mandelNucleusNewton(cdouble guess, int p,
                                         int iters = 100, double tol = 1e-15) {
    auto r = detail::mandelNewtonT<cdouble>(guess, p, iters, tol, 1e-11);
    PeriodicPoint pp;
    pp.point = r.point; pp.period = p; pp.lambda = r.lambda;
    pp.abs_lambda = r.abs_lambda; pp.converged = r.converged;
    return pp;
}
inline PeriodicPoint misiurewiczNewton(cdouble guess, int k, int n,
                                       int iters = 100, double tol = 1e-14) {
    auto r = detail::misiurewiczNewtonT<cdouble>(guess, k, n, iters, tol, 1e-10);
    PeriodicPoint pp;
    pp.point = r.point; pp.preperiod = k; pp.period = n; pp.lambda = r.lambda;
    pp.abs_lambda = r.abs_lambda; pp.converged = r.converged;
    return pp;
}

// Search Misiurewicz points M_{k,n} (preperiod k, period n) near a guess and
// return the dominant one: smallest k+n that converges within `radius` and is
// repelling (genuine boundary spiral). The double-double precision keeps the
// result good to the renderer's df64 wall.
inline PeriodicPoint findMisiurewicz(cdouble guess, int max_pre, int max_period,
                                     double radius) {
    using detail::toCd;
    const cdd g(guess.real(), guess.imag());
    PeriodicPoint best;
    int best_score = 0;
    for (int k = 1; k <= max_pre; ++k) {
        for (int n = 1; n <= max_period; ++n) {
            auto r = detail::misiurewiczNewtonT<cdd>(g, k, n, 120, 1e-26, 1e-20);
            if (!r.converged || r.abs_lambda <= 1.0 + 1e-6) continue;
            cdouble pt = toCd(r.point);
            double d = std::abs(pt - guess);
            if (d > radius) continue;
            int score = k + n;
            if (best.period == 0 || score < best_score) {
                best.point = pt; best.preperiod = k; best.period = n;
                best.lambda = toCd(r.lambda); best.abs_lambda = r.abs_lambda;
                best.converged = true; best.dist = d;
                best_score = score;
            }
        }
    }
    return best;
}

// Search periods 1..max_period near `guess` and return the dominant center: the
// SMALLEST-period point that converges within `radius` of the guess (and, for a
// Julia set, is repelling -- Julia-set spiral centers always are). Periodic
// points are dense, so "nearest over all periods" would just pick max_period;
// smallest-period-within-radius picks the feature you actually aimed at. Runs in
// double-double so the result holds to the renderer's df64 wall. `all`, if
// non-null, collects every accepted candidate.
inline PeriodicPoint findCenter(const RenderConfig& cfg, cdouble guess,
                                int max_period, double radius,
                                std::vector<PeriodicPoint>* all = nullptr) {
    using detail::toCd;
    const bool mandel = (cfg.type == FractalType::Mandelbrot);
    const cdd c(cfg.julia_cre, cfg.julia_cim);
    const cdd g(guess.real(), guess.imag());
    PeriodicPoint best;

    for (int p = 1; p <= max_period; ++p) {
        detail::NewtonT<cdd> r = mandel
            ? detail::mandelNewtonT<cdd>(g, p, 120, 1e-26, 1e-22)
            : detail::juliaNewtonT<cdd>(c, g, p, 120, 1e-26, 1e-22);
        if (!r.converged) continue;
        if (!mandel && detail::truePeriod(c, r.point, p, 1e-18) != p) continue;
        if (!mandel && r.abs_lambda <= 1.0 + 1e-6) continue; // skip attracting/neutral

        PeriodicPoint pp;
        pp.point = toCd(r.point); pp.period = p; pp.lambda = toCd(r.lambda);
        pp.abs_lambda = r.abs_lambda; pp.converged = true;
        pp.dist = std::abs(pp.point - guess);
        if (pp.dist > radius) continue;
        if (all) all->push_back(pp);
        if (best.period == 0 || pp.period < best.period) best = pp;
    }
    return best;
}

} // namespace fractal

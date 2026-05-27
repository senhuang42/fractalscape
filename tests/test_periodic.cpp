// test_periodic.cpp — Newton solver for exact deep-zoom centers.
#include "test_util.h"

#include "doubledouble.h"
#include "periodic.h"

using namespace fractal;

static double cabs(cdouble a, cdouble b) { return std::abs(a - b); }

void test_periodic() {
    // ---- double-double arithmetic ----
    {
        // 0.1 has no exact double; (hi+lo) should track it far better than hi.
        dd a(0.1), b(0.1);
        for (int i = 0; i < 8; ++i) a = a + b; // 9 * 0.1 = 0.9
        CHECK(std::abs((double)a - 0.9) < 1e-15);

        // (1 + 2^-27)^2 = 1 + 2^-26 + 2^-54. The 2^-54 term is below a double's
        // last bit at magnitude 1, so plain multiplication drops it; dd keeps it.
        double x = 1.0 + std::ldexp(1.0, -27);
        dd p = dd(x) * dd(x);
        CHECK(p.hi == 1.0 + std::ldexp(1.0, -26));
        CHECK_NEAR(p.lo, std::ldexp(1.0, -54), std::ldexp(1.0, -68));
        CHECK(x * x == 1.0 + std::ldexp(1.0, -26)); // the naive double lost the term

        // Division round-trips: (1/3)*3 == 1 to ~dd precision.
        dd third = dd(1.0) / dd(3.0);
        dd one = third * dd(3.0);
        CHECK(std::abs((double)one - 1.0) < 1e-30);
    }

    // ---- Julia periodic points for c = 0 (map z -> z^2) ----
    // Fixed points solve z^2 = z -> z = 0 (attracting) or z = 1 (repelling, |λ|=2).
    {
        auto pp = juliaPeriodicNewton(cdouble(0, 0), cdouble(1.2, 0.1), 1);
        CHECK(pp.converged);
        CHECK(cabs(pp.point, cdouble(1, 0)) < 1e-10);
        CHECK_NEAR(pp.abs_lambda, 2.0, 1e-9); // λ = 2 z* = 2
    }
    // Period-2 cycle of z^2 is the primitive cube roots of unity {ω, ω²}.
    // ω = -1/2 + i*sqrt(3)/2; multiplier of the cycle is 4ω³ = 4.
    {
        cdouble omega(-0.5, 0.8660254037844386);
        auto pp = juliaPeriodicNewton(cdouble(0, 0), omega + cdouble(0.05, -0.03), 2);
        CHECK(pp.converged);
        CHECK(pp.period == 2);
        // converges to one of the two cycle points
        CHECK(cabs(pp.point, omega) < 1e-9 ||
              cabs(pp.point, std::conj(omega)) < 1e-9);
        CHECK_NEAR(pp.abs_lambda, 4.0, 1e-6);
    }

    // ---- Mandelbrot nuclei (critical orbit periodic) ----
    // Period 1 nucleus is c = 0; period 2 is c = -1.
    {
        auto p1 = mandelNucleusNewton(cdouble(0.05, 0.02), 1);
        CHECK(p1.converged);
        CHECK(cabs(p1.point, cdouble(0, 0)) < 1e-10);

        auto p2 = mandelNucleusNewton(cdouble(-1.1, 0.05), 2);
        CHECK(p2.converged);
        CHECK(cabs(p2.point, cdouble(-1, 0)) < 1e-10);

        // Period-3 real nucleus near c = -1.7548776662...
        auto p3 = mandelNucleusNewton(cdouble(-1.76, 0.0), 3);
        CHECK(p3.converged);
        CHECK_NEAR(p3.point.real(), -1.7548776662466927, 1e-9);
        CHECK_NEAR(p3.point.imag(), 0.0, 1e-9);
    }

    // ---- findCenter dispatch ----
    {
        // Julia: from a guess near z=1 (c=0), smallest repelling point is period 1.
        RenderConfig jc;
        jc.type = FractalType::Julia; jc.julia_cre = 0.0; jc.julia_cim = 0.0;
        auto best = findCenter(jc, cdouble(1.05, 0.02), 16, 0.5);
        CHECK(best.period == 1);
        CHECK(cabs(best.point, cdouble(1, 0)) < 1e-10);
        CHECK(best.abs_lambda > 1.0); // repelling

        // A too-small radius around a point with no nearby periodic point returns
        // nothing (period 0).
        auto none = findCenter(jc, cdouble(0.3, 0.3), 8, 1e-9);
        CHECK(none.period == 0);

        // Mandelbrot: guess near c=-1 finds the period-2 nucleus.
        RenderConfig mc; mc.type = FractalType::Mandelbrot;
        auto mb = findCenter(mc, cdouble(-1.02, 0.01), 16, 0.2);
        CHECK(mb.period == 2);
        CHECK(cabs(mb.point, cdouble(-1, 0)) < 1e-9);
    }
}

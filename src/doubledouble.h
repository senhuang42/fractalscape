// doubledouble.h — minimal double-double (df64) arithmetic for the CPU.
//
// The center solver in periodic.h Newton-refines a point by evaluating f^p(z).
// For a high-period spiral the multiplier is huge (~1e5 for period 79), so each
// orbit evaluation amplifies rounding error and plain `double` pins the center
// to only ~8 digits -- far short of the df64 renderer's ~1e-13 wall. Running the
// solver in double-double (a hi+lo pair, ~32 significant digits) removes that
// ceiling; the result is then good to full `double` (~16 digits) when stored in
// RenderConfig, which is what the renderer needs.
//
// This is the CPU twin of the GPU df64 in shaders/deep.frag. The error-free
// transforms require IEEE rounding with no reassociation; two_prod uses a true
// hardware FMA (exact on arm64), and two_sum uses only +/- (which clang does not
// contract), so this is safe under -O3 without special flags.
#pragma once

#include <cmath>

namespace fractal {

struct dd {
    double hi = 0.0, lo = 0.0;
    dd() = default;
    dd(double h) : hi(h), lo(0.0) {}              // NOLINT: implicit on purpose
    dd(double h, double l) : hi(h), lo(l) {}
    explicit operator double() const { return hi + lo; }
};

namespace ddmath {

inline dd quick_two_sum(double a, double b) { double s = a + b; double e = b - (s - a); return dd(s, e); }
inline dd two_sum(double a, double b) {
    double s = a + b, v = s - a;
    double e = (a - (s - v)) + (b - v);
    return dd(s, e);
}
inline dd two_prod(double a, double b) {
    double p = a * b;
    double e = std::fma(a, b, -p); // exact rounding error of a*b
    return dd(p, e);
}

} // namespace ddmath

inline dd operator+(dd a, dd b) {
    dd s = ddmath::two_sum(a.hi, b.hi);
    dd t = ddmath::two_sum(a.lo, b.lo);
    s.lo += t.hi;
    s = ddmath::quick_two_sum(s.hi, s.lo);
    s.lo += t.lo;
    return ddmath::quick_two_sum(s.hi, s.lo);
}
inline dd operator-(dd a) { return dd(-a.hi, -a.lo); }
inline dd operator-(dd a, dd b) { return a + (-b); }
inline dd operator*(dd a, dd b) {
    dd p = ddmath::two_prod(a.hi, b.hi);
    p.lo += a.hi * b.lo + a.lo * b.hi;
    return ddmath::quick_two_sum(p.hi, p.lo);
}
inline dd operator/(dd a, dd b) {
    double q1 = a.hi / b.hi;
    dd r = a - dd(q1) * b;
    double q2 = r.hi / b.hi;
    r = r - dd(q2) * b;
    double q3 = r.hi / b.hi;
    dd q = ddmath::quick_two_sum(q1, q2);
    return q + dd(q3);
}

inline bool operator<(dd a, dd b) { return (a.hi < b.hi) || (a.hi == b.hi && a.lo < b.lo); }

// Complex over double-double.
struct cdd {
    dd re, im;
    cdd() = default;
    cdd(dd r, dd i) : re(r), im(i) {}
    cdd(double r) : re(r), im(0.0) {}              // NOLINT: implicit on purpose
    cdd(double r, double i) : re(r), im(i) {}
};

inline cdd operator+(cdd a, cdd b) { return cdd(a.re + b.re, a.im + b.im); }
inline cdd operator-(cdd a, cdd b) { return cdd(a.re - b.re, a.im - b.im); }
inline cdd operator*(cdd a, cdd b) {
    return cdd(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}
inline cdd operator/(cdd a, cdd b) {
    dd d = b.re * b.re + b.im * b.im;
    return cdd((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}

// |z|^2 and |z| as plain doubles (precise enough for convergence thresholds).
inline double norm2(cdd z) { return (double)(z.re * z.re + z.im * z.im); }
inline double absval(cdd z) { return std::sqrt(norm2(z)); }
inline bool   finite(cdd z) { return std::isfinite(z.re.hi) && std::isfinite(z.im.hi); }

} // namespace fractal

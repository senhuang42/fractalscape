#version 410 core
// Extended-precision deep-zoom renderer. The plain path computes c and iterates
// z in 32-bit float, which pixelates past ~1e4 zoom. Here each real number is a
// double-float (df64): a hi+lo float pair carrying ~1e-13 resolution, so the z
// recurrence stays sharp far deeper. The coloring (SAC + the two-layer overlay)
// is read from a float view of the orbit, which is plenty precise for color.
// Quadratic only (Mandelbrot/Julia); other formulas/effects use fractal.frag.
//
// df64 relies on IEEE rounding with no fused multiply-add. If a future GL/driver
// fuses these, the error terms break; verify deep renders stay crisp.
out vec4 FragColor;

uniform vec2  uResolution;
uniform int   uType;          // 0 mandelbrot, 1 julia
uniform int   uMaxIter;
uniform float uBailout;
uniform vec2  uCenterDX;       // df: view center x (hi, lo)
uniform vec2  uCenterDY;       // df: view center y (hi, lo)
uniform vec2  uScaleD;         // df: half-height scale (hi, lo)
uniform vec2  uJuliaCX;        // df: Julia constant real part (hi, lo)
uniform vec2  uJuliaCY;        // df: Julia constant imag part (hi, lo)
uniform sampler2D uPalette;
uniform sampler2D uStripePalette; // separate gradient for the stripe (SAC) layer
uniform sampler2D uInsidePalette; // separate gradient for set-interior coloring
uniform bool      uHasStripePalette; // false -> reuse uPalette for stripe layer
uniform bool      uColorInside;      // color set interior by SAC instead of flat
uniform int       uPosterize;        // 0 = off, otherwise quantize sample pos
uniform float uColorDensity;
uniform float uColorOffset;
uniform float uStripeColor;
uniform float uStripeFreq;
uniform float uStripeContrast;
uniform vec3  uInsideColor;

const float PI    = 3.14159265358979;
const float SPLIT = 4097.0; // 2^12 + 1, the Veltkamp split for 24-bit mantissa

// --- double-float primitives (Dekker / Knuth two-sum, two-prod; no FMA) ---
// `precise` is essential: it forbids the driver from contracting these into
// fused multiply-adds or reordering them, which would zero the error terms and
// silently collapse df64 back to plain float (verified: without it, deep zooms
// pixelate identically to the float path).
vec2 dfQuickTwoSum(float a, float b){ precise float s = a + b; precise float e = b - (s - a); return vec2(s, e); }
vec2 dfTwoSum(float a, float b){ precise float s = a + b; precise float v = s - a; precise float e = (a - (s - v)) + (b - v); return vec2(s, e); }
vec2 dfTwoProd(float a, float b){
    precise float p  = a * b;
    float ca = SPLIT * a; precise float ah = ca - (ca - a); precise float al = a - ah;
    float cb = SPLIT * b; precise float bh = cb - (cb - b); precise float bl = b - bh;
    precise float e  = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
    return vec2(p, e);
}
vec2 dfAdd(vec2 a, vec2 b){ vec2 s = dfTwoSum(a.x, b.x); s.y += a.y + b.y; return dfQuickTwoSum(s.x, s.y); }
vec2 dfSub(vec2 a, vec2 b){ return dfAdd(a, vec2(-b.x, -b.y)); }
vec2 dfMul(vec2 a, vec2 b){ vec2 p = dfTwoProd(a.x, b.x); p.y += a.x * b.y + a.y * b.x; return dfQuickTwoSum(p.x, p.y); }
vec2 dfMulF(vec2 a, float b){ vec2 p = dfTwoProd(a.x, b); p.y += a.y * b; return dfQuickTwoSum(p.x, p.y); }

// Posterize gradient sample position to N flat bands; pass-through if off.
// Band index clamped to [0, N-1] before centering so s=1.0 doesn't overshoot.
float posterizeS(float s) {
    if (uPosterize <= 1) return s;
    float n = float(uPosterize);
    float band = min(floor(s * n), n - 1.0);
    return (band + 0.5) / n;
}
vec3 sampleIter(float s)   { return texture(uPalette,        vec2(posterizeS(s), 0.5)).rgb; }
vec3 sampleStripe(float s) {
    if (uHasStripePalette) return texture(uStripePalette, vec2(posterizeS(s), 0.5)).rgb;
    return texture(uPalette, vec2(posterizeS(s), 0.5)).rgb;
}
vec3 sampleInside(float s) { return texture(uInsidePalette, vec2(posterizeS(s), 0.5)).rgb; }

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;

    // Plane offset = uv * (2 * scale), in df64.
    vec2 twoScale = dfMulF(uScaleD, 2.0);
    vec2 ox = dfMulF(twoScale, uv.x);
    vec2 oy = dfMulF(twoScale, uv.y);

    vec2 cx, cy, x, y;
    if (uType == 0) {                 // Mandelbrot: c = center + offset, z0 = 0
        cx = dfAdd(uCenterDX, ox); cy = dfAdd(uCenterDY, oy);
        x = vec2(0.0); y = vec2(0.0);
    } else {                          // Julia: z0 = center + offset, c = const
        x = dfAdd(uCenterDX, ox); y = dfAdd(uCenterDY, oy);
        // c carried in df64 too: at deep zoom a float-precision c (~1e-7) would
        // be injected every iteration and cap Julia zooms at ~1e-7.
        cx = uJuliaCX; cy = uJuliaCY;
    }

    float bail2 = uBailout * uBailout;
    bool  useStripe = uStripeColor > 0.0;
    const int kStripeSkip = 1;

    int   i;
    float m2 = 0.0, stripeSum = 0.0, lastTerm = 0.0;
    int   stripeN = 0;
    for (i = 0; i < uMaxIter; i++) {
        vec2 x2 = dfMul(x, x);
        vec2 y2 = dfMul(y, y);
        vec2 xy = dfMul(x, y);
        x = dfAdd(dfSub(x2, y2), cx);   // x' = x^2 - y^2 + cx
        y = dfAdd(dfAdd(xy, xy), cy);   // y' = 2xy + cy
        float fzx = x.x, fzy = y.x;     // float view of z for coloring
        if (useStripe && i >= kStripeSkip) {
            lastTerm   = 0.5 + 0.5 * sin(uStripeFreq * atan(fzy, fzx));
            stripeSum += lastTerm;
            stripeN++;
        }
        m2 = fzx * fzx + fzy * fzy;
        if (m2 > bail2) break;
    }

    if (i >= uMaxIter) {
        if (uColorInside && useStripe && stripeN > 0) {
            float sacIn = stripeSum / float(stripeN);
            sacIn = (sacIn - 0.5) * uStripeContrast + 0.5;
            float sIn = fract(clamp(sacIn, 0.0, 1.0) + uColorOffset);
            FragColor = vec4(sampleInside(sIn), 1.0);
        } else {
            FragColor = vec4(uInsideColor, 1.0);
        }
        return;
    }

    float log_zn = 0.5 * log(m2);
    float nu     = log(log_zn / log(uBailout)) / log(2.0);
    float mu     = float(i) + 1.0 - nu;

    float sac = 0.0;
    if (useStripe && stripeN > 0) {
        float avgIncl = stripeSum / float(stripeN);
        float avgExcl = (stripeN > 1) ? (stripeSum - lastTerm) / float(stripeN - 1) : avgIncl;
        sac = mix(avgExcl, avgIncl, fract(mu));
        sac = (sac - 0.5) * uStripeContrast + 0.5;
    }
    float stripeS = fract(clamp(sac, 0.0, 1.0) + uColorOffset);

    vec3 col;
    if (uColorDensity <= 0.0) {
        col = sampleStripe(stripeS);
    } else {
        float iterS = 1.0 - exp(-mu * uColorDensity);
        if (uStripeColor <= 0.0) {
            col = sampleIter(iterS);
        } else {
            // Dual-palette ready: iter draws field from uPalette, stripe draws
            // structure from uStripePalette (or uPalette if not provided).
            vec3  stripeCol = sampleStripe(stripeS);
            vec3  iterCol   = sampleIter(iterS);
            float gate = smoothstep(0.20, 0.62, iterS);
            col = mix(iterCol, stripeCol * gate, uStripeColor);
        }
    }
    FragColor = vec4(col, 1.0);
}

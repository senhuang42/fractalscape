#version 330 core
// Escape-time fractal renderer with stripe-average coloring, smooth iteration,
// derivative normal-map shading, and distance estimation.
//
// Fidelity techniques (see README for sources):
//   * Smooth iteration count        -> bands with no stair-stepping.
//   * Stripe Average Coloring (SAC) -> averages sin(s*arg z) along the orbit
//     and interpolates by the fractional escape time. Fills smooth areas with
//     flowing detail and, crucially, has NO level-set banding (Haerkoenen 2007).
//   * Normal-map "fake 3D" shading  -> feathery embossed look from derivative.
//   * Distance estimation (iq)      -> exterior fade-to-void and filament glow.
//   * Gamma-correct supersampling   -> handled in downsample.frag.
//
// Mirrors the CPU reference in src/fractal_math.h.

out vec4 FragColor;

uniform vec2  uResolution;   // render-target size in pixels (post-SSAA)
uniform vec2  uCenter;       // view center in the complex plane
uniform float uScale;        // half vertical extent in complex units
uniform vec2  uJuliaC;       // Julia constant
uniform int   uType;         // 0 = Mandelbrot, 1 = Julia
uniform int   uMaxIter;
uniform float uBailout;      // escape radius
uniform float uExponent;     // z^exponent + c

uniform sampler2D uPalette;  // 1xN gradient (sampled along x) -- iter layer
uniform sampler2D uStripePalette; // gradient for the stripe (SAC) layer
uniform sampler2D uInsidePalette; // gradient for set-interior coloring
uniform sampler2D uNebulaTex;     // optional Buddhabrot density accent (R-channel)
uniform bool      uHasStripePalette; // false -> reuse uPalette for stripe layer
uniform int       uInteriorMode;     // 0 flat, 1 SAC, 2 bof60, 3 bof61, 4 expsmooth
uniform int       uPosterize;        // 0 = off, otherwise quantize sample pos
uniform bool      uLogIter;          // Maths Town-style cyclic log-iter coloring
uniform float     uSlopes;           // directional shading from log(mu) gradient (0 = off)
uniform float     uSlopesSpec;       // specular intensity for slopes (0 = none)
uniform int       uReliefMode;       // 0 = SAC stripe, 1 = TIA, 2 = curvature average
uniform int       uTrapShape;        // 0 point, 1 cross, 2 circle, 3 astroid,
                                     // 4 diamond, 5 hyperbola, 6 waves, 7 spiral
uniform float     uTrapRadius;       // trap shape size
uniform float     uTrapFreq;         // waves sine freq / spiral twist
uniform float     uStalkColor;       // Pickover stalks weight (0 = off)
uniform float     uStalkFreq;        // stalk falloff sharpness
uniform float     uGaussColor;       // Gaussian-integer lattice weight (0 = off)
uniform float     uGaussFreq;        // lattice-distance falloff sharpness
uniform int       uDecomp;           // binary decomposition: 2^k cells (0 = off)
uniform float     uDecompStrength;   // darkening of alternate decomposition cells
uniform float     uSheen;            // slope-angle iridescent hue shift (0 = off)
uniform float     uNebulaWeight;     // strength of the nebula accent (0 = off)
uniform vec3      uNebulaColor;      // wisp tint (multiplied by density); ignored in RGB mode
uniform float     uNebulaHueShift;   // density modulates stripe sample position
uniform bool      uNebulaRgb;        // sample full .rgb (3-channel nebula) vs .r (mono density)
uniform float uColorDensity; // palette cycles per iteration unit
uniform float uColorOffset;  // palette phase shift
uniform float uAngleColor;   // weight of escape-angle in the palette coord
uniform float uTrapColor;    // weight of orbit-trap distance in palette coord
uniform vec2  uTrapPoint;    // orbit-trap location in the complex plane
uniform float uStripeColor;   // gradient cycles the stripe value spans
uniform float uStripeFreq;    // stripe density s (integer 4/6/8 looks best)
uniform float uStripeContrast;// stretch stripe value around mid (the -mod knob)
uniform vec3  uInsideColor;  // color for points in the set (when not uColorInside)

uniform float uShading;      // diffuse light strength (0 = flat)
uniform float uLightAngle;   // light azimuth, degrees
uniform float uLightHeight;  // light elevation (z of the light vector)
uniform float uSpecular;     // specular highlight strength (0 = none)
uniform float uShininess;    // specular exponent (higher = tighter highlight)
uniform float uHeightScale;  // how strongly the relief tilts the surface normal
uniform float uGlow;         // distance-estimate filament glow strength
uniform float uFalloff;      // exterior fade-to-void by distance (0 = off)
uniform float uKaleido;      // N mirrored wedges (< 2 = off)
uniform float uKaleidoAngle; // rotate the symmetry, radians
uniform int   uFormula;      // 0 quad, 1 burning ship, 2 tricorn, 3 phoenix, 4 newton
uniform vec2  uPhoenixP;     // Phoenix z_prev coefficient

const float PI = 3.14159265358979;

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x*b.x - a.y*b.y, a.x*b.y + a.y*b.x); }
vec2 cdiv(vec2 a, vec2 b) { float d = dot(b, b); return vec2(a.x*b.x + a.y*b.y, a.y*b.x - a.x*b.y) / d; }

// Quantize a gradient sample position s in [0,1] to N flat bands. Band index
// is clamped to [0, N-1] before the +0.5 center offset, so s=1.0 lands in the
// last band rather than rolling over into a phantom Nth band.
float posterizeS(float s) {
    if (uPosterize <= 1) return s;
    float n = float(uPosterize);
    float band = min(floor(s * n), n - 1.0);
    return (band + 0.5) / n;
}
vec3 sampleIter(float s)   { return texture(uPalette,        vec2(posterizeS(s), 0.5)).rgb; }
vec3 sampleStripe(float s) {
    // GLSL 330: avoid ternary on sampler types; use a flat branch instead.
    if (uHasStripePalette) return texture(uStripePalette, vec2(posterizeS(s), 0.5)).rgb;
    return texture(uPalette, vec2(posterizeS(s), 0.5)).rgb;
}
vec3 sampleInside(float s) { return texture(uInsidePalette, vec2(posterizeS(s), 0.5)).rgb; }

// Newton's method on z^3 - 1 = 0, colored by which of the three cube roots of
// unity the orbit converges to (the basin) and how fast it got there. A
// separate path from escape-time: there is no bailout, we test for convergence.
vec3 newtonColor(vec2 z) {
    const vec2 r0 = vec2( 1.0,  0.0);
    const vec2 r1 = vec2(-0.5,  0.8660254);
    const vec2 r2 = vec2(-0.5, -0.8660254);
    int i; int root = -1;
    for (i = 0; i < uMaxIter; i++) {
        vec2 z2 = cmul(z, z);
        vec2 z3 = cmul(z2, z);
        z -= cdiv(z3 - vec2(1.0, 0.0), 3.0 * z2);   // z - f/f'
        if (distance(z, r0) < 1e-4) { root = 0; break; }
        if (distance(z, r1) < 1e-4) { root = 1; break; }
        if (distance(z, r2) < 1e-4) { root = 2; break; }
    }
    // Shade by how fast it converged: fast (few iters) bright, boundary dark.
    float bright = clamp(1.0 - float(i) / float(uMaxIter), 0.0, 1.0);
    bright = pow(bright, 0.6);
    if (root < 0) return uInsideColor;                 // never converged
    float palCoord = fract((float(root) + 0.5) / 3.0 + uColorOffset);
    vec3 hue = texture(uPalette, vec2(palCoord, 0.5)).rgb;
    return hue * (0.12 + 0.88 * bright);
}

// Complex power via polar form (used only when exponent != 2).
vec2 cpow(vec2 z, float p) {
    float r = length(z);
    if (r == 0.0) return vec2(0.0);
    float th = atan(z.y, z.x);
    return pow(r, p) * vec2(cos(p * th), sin(p * th));
}

void main() {
    // Map pixel -> complex plane, preserving aspect via the y axis.
    vec2 uv = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;

    // Kaleidoscope: fold uv into N mirrored wedges around the image center, so
    // the fractal repeats with radial symmetry into a mandala. Done in screen
    // space (before the plane map) so symmetry is about the frame center.
    if (uKaleido >= 2.0) {
        float seg = 2.0 * PI / uKaleido;
        float a   = atan(uv.y, uv.x) - uKaleidoAngle;
        float r   = length(uv);
        a = mod(a, seg);
        a = abs(a - 0.5 * seg);          // mirror within the wedge
        a += uKaleidoAngle;
        uv = r * vec2(cos(a), sin(a));
    }

    vec2 p  = uCenter + uv * (2.0 * uScale);

    // Newton fractals are convergence-based, not escape-time: own coloring path.
    // (Runs after the kaleidoscope fold, so --kaleido works on it too.)
    if (uFormula == 4) { FragColor = vec4(newtonColor(p), 1.0); return; }

    bool mandel = (uType == 0);
    vec2 z, c, dz;
    if (mandel) { c = p;       z = vec2(0.0); dz = vec2(0.0);      } // dz/dc
    else        { z = p;       c = uJuliaC;   dz = vec2(1.0, 0.0); } // dz/dz0

    float bail2 = uBailout * uBailout;
    bool  quad  = abs(uExponent - 2.0) < 1e-6;
    vec2  one   = mandel ? vec2(1.0, 0.0) : vec2(0.0);

    bool  useStripe = uStripeColor > 0.0;
    bool  useTrap   = uTrapColor   > 0.0;
    // The derivative dz is only needed for the distance estimate, which only
    // feeds the (off-by-default) glow/falloff. Skipping it drops a complex
    // multiply from every iteration on the common path — pure dead work
    // otherwise. Output is bit-identical when glow and falloff are both 0.
    bool  useDE     = (uGlow > 0.0) || (uFalloff > 0.0);
    const int kStripeSkip = 1; // skip first iterations (transient orbit points)

    int   i;
    float m2        = dot(z, z);
    float trap      = 1e20;  // closest the orbit comes to uTrapPoint
    float stripeSum = 0.0;   // running sum of stripe / TIA / curvature terms
    float lastTerm  = 0.0;   // most recent stripe term (for de-banding)
    int   stripeN   = 0;     // number of stripe terms summed
    float stalkMin  = 1e20;  // Pickover stalks: min |z.x| or |z.y| seen
    float gaussMin  = 1e20;  // Gaussian integer: min dist to round(z) lattice
    float minMag2   = 1e20;  // bof60/61: closest |z|^2 the orbit reaches
    int   minIter   = 0;     // bof61: iteration index of that closest approach
    float expSum    = 0.0;   // expsmooth: sum of exp(-1/|z_n - z_{n-1}|)
    vec2  zprev     = vec2(0.0); // z_{n-1}: Phoenix / curvature / expsmooth
    vec2  zprev2    = vec2(0.0); // z_{n-2}: curvature average
    for (i = 0; i < uMaxIter; i++) {
        // Formula transform applied to z before squaring: Burning Ship folds
        // both components positive; Tricorn conjugates. Quadratic/Phoenix leave
        // z as-is. (See Formula in config.h.)
        vec2 zt = z;
        if      (uFormula == 1) zt = vec2(abs(z.x), abs(z.y)); // Burning Ship
        else if (uFormula == 2) zt = vec2(z.x, -z.y);          // Tricorn
        // Update derivative using the current z, then advance z (shifting the
        // z_{n-1}/z_{n-2} history that Phoenix, curvature, and expsmooth use).
        if (quad) {
            if (useDE) dz = 2.0 * cmul(zt, dz) + one;
            vec2 znew = cmul(zt, zt) + c;
            if (uFormula == 3) znew += cmul(uPhoenixP, zprev); // Phoenix z_prev term
            zprev2 = zprev;
            zprev  = z;
            z = znew;
        } else {
            if (useDE) dz = uExponent * cmul(cpow(zt, uExponent - 1.0), dz) + one;
            zprev2 = zprev;
            zprev  = z;
            z  = cpow(zt, uExponent) + c;
        }
        if (useTrap) {
            vec2 dv = z - uTrapPoint;
            float d;
            if      (uTrapShape == 1) d = min(abs(dv.x), abs(dv.y));        // cross
            else if (uTrapShape == 2) d = abs(length(dv) - uTrapRadius);    // ring
            else if (uTrapShape == 3)                                       // astroid
                d = abs(pow(abs(dv.x), 2.0/3.0) + pow(abs(dv.y), 2.0/3.0)
                        - pow(uTrapRadius, 2.0/3.0));
            else if (uTrapShape == 4) d = abs(abs(dv.x) + abs(dv.y) - uTrapRadius); // diamond
            else if (uTrapShape == 5) d = abs(dv.x * dv.y - uTrapRadius);   // hyperbola
            else if (uTrapShape == 6)                                       // waves
                d = abs(abs(dv.y) - uTrapRadius
                        + sin(dv.x * uTrapFreq) * 0.5 * uTrapRadius);
            else if (uTrapShape == 7) {                                     // log-spiral
                float r = length(dv);
                float a = atan(dv.y, dv.x);
                d = abs(sin(a + uTrapFreq * log(max(r, 1e-9)))) * r;
            }
            else                      d = length(dv);                       // point
            trap = min(trap, d);
        }
        // Pickover stalks: closest the orbit ever came to either coordinate
        // axis. Independent of trap; combines with stripe sample below.
        if (uStalkColor > 0.0) stalkMin = min(stalkMin, min(abs(z.x), abs(z.y)));
        // Gaussian integer: closest the orbit ever came to ANY integer lattice
        // point. The lattice is dense everywhere (unlike one trap point), so
        // this lays a crystalline grid texture over the whole plane.
        if (uGaussColor > 0.0) {
            vec2 g = z - vec2(floor(z.x + 0.5), floor(z.y + 0.5));
            gaussMin = min(gaussMin, length(g));
        }
        // Interior statistics (bof60/61, expsmooth). Cheap, but gated so the
        // default path stays byte-identical.
        if (uInteriorMode >= 2) {
            float zm2 = dot(z, z);
            if (zm2 < minMag2) { minMag2 = zm2; minIter = i; }
            // Convergence step: min of the distances to the previous TWO
            // iterates, so orbits settling onto a period-2 cycle (whose
            // consecutive iterates stay far apart forever) still read as
            // converged via z_{n-2}.
            if (uInteriorMode == 4 && i >= 2)
                expSum += exp(-1.0 / max(min(distance(z, zprev),
                                             distance(z, zprev2)), 1e-12));
        }
        // Curvature needs two prior orbit points, so it skips one extra
        // leading iteration (the history is still zero-initialized at i=1).
        if (useStripe && i >= (uReliefMode == 2 ? kStripeSkip + 1 : kStripeSkip)) {
            // ReliefMode dispatch: SAC (sine of orbit angle), TIA (where
            // |z'+c| lies in the triangle-inequality window), or curvature
            // average (the orbit path's turning angle). TIA needs z BEFORE
            // this iteration's update (= zt, the formula-transformed z already
            // in scope); curvature needs the two PREVIOUS orbit points.
            float term;
            if (uReliefMode == 1) {
                float mz2  = dot(zt, zt);
                float mc   = length(c);
                float mnew = sqrt(m2);          // m2 will be updated below; recompute is cheap
                mnew = length(z);
                float lo   = abs(mz2 - mc);
                float hi   = mz2 + mc;
                term = clamp((mnew - lo) / max(hi - lo, 1e-9), 0.0, 1.0);
            } else if (uReliefMode == 2) {
                // Curvature average (Haerkoenen eq 4.8): |arg of the ratio of
                // successive orbit steps| / pi, i.e. how sharply the orbit
                // path bends at z_{n-1}. A degenerate (zero) step would blow
                // up the ratio; reuse the previous term so the average is
                // undisturbed.
                vec2 num = z     - zprev;
                vec2 den = zprev - zprev2;
                term = lastTerm;
                if (dot(den, den) > 1e-30) {
                    vec2 q = cdiv(num, den);
                    term = abs(atan(q.y, q.x)) / PI;
                }
            } else {
                term = 0.5 + 0.5 * sin(uStripeFreq * atan(z.y, z.x));
            }
            lastTerm   = term;
            stripeSum += term;
            stripeN++;
        }
        m2 = dot(z, z);
        if (m2 > bail2) break;
    }

    if (i >= uMaxIter) {
        // Set interior. Default is the flat uInsideColor; the other modes
        // color by an orbit statistic so the set body becomes its own
        // miniature scene instead of solid black. (See InteriorMode.)
        if (uInteriorMode == 1 && useStripe && stripeN > 0) {
            // SAC: the orbit's accumulated stripe value.
            float sacIn = stripeSum / float(stripeN);
            sacIn = (sacIn - 0.5) * uStripeContrast + 0.5;
            float sIn = fract(clamp(sacIn, 0.0, 1.0) + uColorOffset);
            FragColor = vec4(sampleInside(sIn), 1.0);
        } else if (uInteriorMode == 2) {
            // bof60: closest approach to the origin. Bounded orbits pass near
            // 0 periodically; how near maps to glowing nested "embryo" shapes
            // (Beauty of Fractals p.60 -- classically scaled by sqrt).
            float sIn = fract(clamp(pow(minMag2, 0.25), 0.0, 1.0) + uColorOffset);
            FragColor = vec4(sampleInside(sIn), 1.0);
        } else if (uInteriorMode == 3) {
            // bof61 / atom domains: WHICH iteration came closest to 0. Flat
            // cells keyed by period; golden-ratio spacing makes consecutive
            // indices land far apart in the gradient so domains read as
            // distinct color cells.
            float sIn = fract(float(minIter) * 0.61803398875 + uColorOffset);
            FragColor = vec4(sampleInside(sIn), 1.0);
        } else if (uInteriorMode == 4) {
            // Exponential smoothing (convergent form): orbits that take long
            // to settle accumulate a larger sum -> brighter. Mapped with the
            // same exp ramp as the iteration layer so --color-density tunes it.
            float sIn = 1.0 - exp(-expSum * max(uColorDensity, 1e-4) * 4.0);
            FragColor = vec4(sampleInside(fract(sIn * 0.98 + uColorOffset)), 1.0);
        } else {
            FragColor = vec4(uInsideColor, 1.0);
        }
        return;
    }

    // Continuous escape time -> smooth bands; frac(mu) drives SAC de-banding.
    float log_zn = 0.5 * log(m2);
    float nu     = log(log_zn / log(uBailout)) / log(uExponent);
    float mu     = float(i) + 1.0 - nu;

    // Stripe Average Coloring: average of sin-stripes along the orbit,
    // interpolated between including/excluding the last (overshot) orbit point
    // by the fractional escape time. This removes the level-set seams that a
    // raw orbit-trap min produces, and textures otherwise-flat regions.
    float sac = 0.0;
    if (useStripe && stripeN > 0) {
        float avgIncl = stripeSum / float(stripeN);
        float avgExcl = (stripeN > 1) ? (stripeSum - lastTerm) / float(stripeN - 1)
                                      : avgIncl;
        float d = fract(mu);
        sac = mix(avgExcl, avgIncl, d); // d*incl + (1-d)*excl
        // Stretch contrast around the midpoint so the value uses the full
        // gradient (analogous to the reference's large -mod parameter).
        sac = (sac - 0.5) * uStripeContrast + 0.5;
    }

    // Two layers (the SAC reference's two renders), combined here:
    //
    //   * Iteration layer -> structure, dendrite tendrils, dark gaps. Fast-
    //     escape exterior -> ~black; slow-escape filaments thread out as bright
    //     tendrils. This is the layer that populates the "black" with real
    //     detail from the equation.
    //   * Stripe layer (SAC) -> the smooth 3D relief texture/hue.
    //
    // The iteration layer GATES the stripe layer (multiply): gaps go black,
    // structure shows full stripe relief, and the bright tendrils carry the
    // relief out into the void. (A plain hard-light lets the bright fur leak
    // into the gaps; gating keeps them clean.)
    //
    // Layer selection (CLI): color_density==0 -> stripe only;
    // stripe_color==0 -> iteration only; both > 0 -> gated overlay.
    float stripeS = clamp(sac, 0.0, 1.0);
    float angle   = atan(z.y, z.x) / (2.0 * PI) + 0.5; // [0,1)
    // Pickover stalks: small stalkMin (orbit grazed an axis) -> stalkS near 1.
    // Adds another orbit-statistic contribution to stripeS, independent of SAC,
    // angle, and trap. Reveals cross/stalk patterns hidden to other techniques.
    float stalkS = (uStalkColor > 0.0) ? exp(-uStalkFreq * stalkMin) : 0.0;
    // Gaussian integer: same exp mapping, but against the orbit's closest pass
    // to the unit lattice -- a crystalline grid statistic.
    float gaussS = (uGaussColor > 0.0) ? exp(-uGaussFreq * gaussMin) : 0.0;
    // Slope-angle sheen: the DIRECTION the escape-time field falls off in,
    // routed to hue. (The atan2 of a gradient is scale-free, so no resolution
    // normalization is needed.) Iridescent oil-film look on cyclic palettes.
    float sheenS = 0.0;
    if (uSheen != 0.0) {
        float h = log(mu);
        sheenS = atan(dFdy(h), dFdx(h)) / (2.0 * PI) + 0.5;
    }
    stripeS = fract(stripeS + uAngleColor * angle + uTrapColor * trap +
                    uStalkColor * stalkS + uGaussColor * gaussS +
                    uSheen * sheenS + uColorOffset);

    // Nebula hybrid: sample density ONCE up front; reused by both the hue-
    // shift (modality 2, modifies stripeS before palette lookup) and the
    // additive accent (modality 1, applied at end of main).
    vec3  nebulaSample = vec3(0.0);
    float nebulaDensity = 0.0;
    if (uNebulaWeight > 0.0 || uNebulaHueShift != 0.0) {
        vec2 nuv = gl_FragCoord.xy / uResolution;
        // Buddhabrot output is top-down (PNG order); gl_FragCoord is bottom-up.
        nebulaSample  = texture(uNebulaTex, vec2(nuv.x, 1.0 - nuv.y)).rgb;
        nebulaDensity = uNebulaRgb ? max(max(nebulaSample.r, nebulaSample.g), nebulaSample.b)
                                   : nebulaSample.r;
        if (uNebulaHueShift != 0.0) {
            stripeS = fract(stripeS + nebulaDensity * uNebulaHueShift);
        }
    }

    vec3 col;
    if (uColorDensity <= 0.0) {
        col = sampleStripe(stripeS);                               // stripe alone
    } else {
        // Iteration coloring. Two modes:
        //   exp (default): iterS = 1 - exp(-mu * density). Compresses high mu
        //     to ~1 so deep structure saturates the palette top. A smoothstep
        //     gate keeps fast-escape gaps dark.
        //   log (Maths Town): iterS = fract(log(mu) * density + offset).
        //     Cyclic -- each natural-log unit of mu gets a palette band, so
        //     features at the same relative depth always land on the same
        //     color. No iter gate (every exterior pixel reads a band color).
        float iterS;
        float gate;
        if (uLogIter) {
            iterS = fract(log(mu) * uColorDensity + uColorOffset + uSheen * sheenS);
            gate  = 1.0; // bands all the way out; no fast-escape suppression
        } else {
            iterS = 1.0 - exp(-mu * uColorDensity);
            gate  = smoothstep(0.20, 0.62, iterS);
        }
        if (uStripeColor <= 0.0) {
            col = sampleIter(iterS);                               // iteration alone
        } else {
            // Dual-palette ready: iter draws field from uPalette, stripe draws
            // structure from uStripePalette (or uPalette if not provided).
            vec3 stripeCol = sampleStripe(stripeS);
            vec3 iterCol   = sampleIter(iterS);
            col = mix(iterCol, stripeCol * gate, uStripeColor);
        }
    }

    // Binary decomposition (Peitgen's "Beauty of Fractals" grid): split each
    // dwell band into 2^k angular cells by the escape argument and darken
    // alternate cells. The vertical cell edges trace external rays (field
    // lines), the horizontal ones equipotentials -- the classic B/W checkered
    // exterior, here as a multiplicative overlay on ANY coloring underneath.
    // (The large default bailout keeps cells near-square, like B = e^pi does
    // in the classic renders.)
    if (uDecomp > 0) {
        float cells  = exp2(float(uDecomp));
        float sector = floor(angle * cells);   // angle is the escape arg in [0,1)
        float band   = floor(mu);              // dwell band index
        float parity = mod(sector + band, 2.0);
        col *= 1.0 - uDecompStrength * parity;
    }

    // Blinn-Phong lighting of the relief as a height field. The stripe/iteration
    // brightness IS a smooth height map, so its screen-space gradient gives a
    // real surface normal: diffuse shading deepens the relief and a specular
    // highlight adds a lit, polished sheen. (Derivatives are resolution-
    // independent via *uResolution.y, and specular is scaled by luminance so it
    // never sparkles in the black gaps.)
    if (uShading > 0.0 || uSpecular > 0.0) {
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        float hx  = dFdx(lum) * uResolution.y;
        float hy  = dFdy(lum) * uResolution.y;
        vec3  N   = normalize(vec3(-hx * uHeightScale, -hy * uHeightScale, 1.0));
        float az  = radians(uLightAngle);
        vec3  L   = normalize(vec3(cos(az), sin(az), max(uLightHeight, 0.05)));
        vec3  H   = normalize(L + vec3(0.0, 0.0, 1.0)); // view = +z
        float diff = max(dot(N, L), 0.0);
        float spec = pow(max(dot(N, H), 0.0), uShininess);
        col *= (1.0 - uShading) + uShading * diff;        // diffuse emboss
        col += uSpecular * spec * lum * vec3(1.0);        // specular sheen
    }

    // Maths Town-style "Slopes": directional shading from the gradient of
    // log(mu). Unlike --shading (which uses luminance and breaks on banded /
    // cyclic palettes), this reads TRUE escape-time depth, so even a posterized
    // or cyclic palette gets clean 3D relief. The height field is log(mu) so
    // both shallow and deep regions contribute smoothly; the same height_scale
    // / light_angle / light_height / shininess uniforms apply.
    if (uSlopes > 0.0 || uSlopesSpec > 0.0) {
        float h   = log(mu);
        float hx  = dFdx(h) * uResolution.y;
        float hy  = dFdy(h) * uResolution.y;
        vec3  N   = normalize(vec3(-hx * uHeightScale, -hy * uHeightScale, 1.0));
        float az  = radians(uLightAngle);
        vec3  L   = normalize(vec3(cos(az), sin(az), max(uLightHeight, 0.05)));
        vec3  H   = normalize(L + vec3(0.0, 0.0, 1.0));
        float diff = max(dot(N, L), 0.0);
        float spec = pow(max(dot(N, H), 0.0), uShininess);
        col *= (1.0 - uSlopes) + uSlopes * diff;
        col += uSlopesSpec * spec * vec3(1.0);
    }

    // Distance estimate: plane-space distance to the set boundary, in pixels.
    // Only computed when something consumes it (dz is only tracked then too).
    if (useDE) {
        float d       = sqrt(m2 / max(dot(dz, dz), 1e-20)) * 0.5 * log(m2);
        float pixsize = 2.0 * uScale / uResolution.y;
        float dpix    = d / pixsize;

        // Fade the exterior toward the void by distance -> color hugs filigree.
        if (uFalloff > 0.0) {
            float vis = exp(-dpix * uFalloff);
            col = mix(uInsideColor, col, clamp(vis, 0.0, 1.0));
        }

        // Glow lights the very finest filaments right at the boundary.
        if (uGlow > 0.0) {
            float g = exp(-dpix * dpix * 0.5);
            col += uGlow * g * texture(uPalette, vec2(fract(uColorOffset + 0.5), 0.5)).rgb;
        }
    }

    // Hybrid accent (modality 1, density add). nebulaSample/Density were
    // already fetched above to share the texture read with the hue-shift
    // modality. In mono mode (uNebulaRgb=false) we tint a single density value
    // by uNebulaColor; in RGB mode we add the per-channel densities directly,
    // so wisps are multi-hued by orbit lifetime (each channel was rendered at
    // a different iteration threshold).
    if (uNebulaWeight > 0.0) {
        vec3 add = uNebulaRgb ? nebulaSample
                              : (nebulaDensity * uNebulaColor);
        col += add * uNebulaWeight;
    }

    FragColor = vec4(col, 1.0);
}

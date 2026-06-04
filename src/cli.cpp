#include "cli.h"
#include "palette.h"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace fractal {

namespace {

// Small helper to walk the argument list and pull typed values.
struct Cursor {
    const std::vector<std::string>& args;
    size_t i;
    std::string error;

    bool hasNext() const { return i < args.size(); }

    // Consume the next token as a string value for flag `flag`.
    bool nextStr(const std::string& flag, std::string& out) {
        if (i >= args.size()) { error = "missing value for " + flag; return false; }
        out = args[i++];
        return true;
    }
    bool nextDouble(const std::string& flag, double& out) {
        std::string s;
        if (!nextStr(flag, s)) return false;
        try { size_t pos; out = std::stod(s, &pos);
              if (pos != s.size()) throw std::invalid_argument(""); }
        catch (...) { error = "expected a number for " + flag + ", got '" + s + "'"; return false; }
        return true;
    }
    bool nextInt(const std::string& flag, int& out) {
        std::string s;
        if (!nextStr(flag, s)) return false;
        try { size_t pos; out = std::stoi(s, &pos);
              if (pos != s.size()) throw std::invalid_argument(""); }
        catch (...) { error = "expected an integer for " + flag + ", got '" + s + "'"; return false; }
        return true;
    }
};

// Curated presets: a known-good fractal + framing + palette combination.
// Sets the base config; any explicit flags the user also passes win over it.
// `palette_spec` is written here and resolved later.
bool applyPreset(const std::string& name, VideoConfig& c, std::string& palette_spec) {
    auto julia = [&](double cre, double cim, double scale, int iter, const char* pal) {
        c.type = FractalType::Julia; c.julia_cre = cre; c.julia_cim = cim;
        c.center_x = 0.0; c.center_y = 0.0; c.scale = scale; c.max_iter = iter;
        palette_spec = pal;
    };
    auto mandel = [&](double cx, double cy, double scale, int iter, const char* pal) {
        c.type = FractalType::Mandelbrot; c.center_x = cx; c.center_y = cy;
        c.scale = scale; c.max_iter = iter; palette_spec = pal;
    };

    if      (name == "noir-spiral")    julia(-0.7269,   0.1889,   1.10, 3000, "noir");
    else if (name == "frostbite")      julia(-0.7269,   0.1889,   1.10, 3000, "frost");
    else if (name == "molten")         julia(-0.74543,  0.11301,  1.25, 4000, "magma");
    else if (name == "deep-ocean")     julia(-0.8,      0.156,    1.30, 3000, "ocean");
    else if (name == "dusk")           julia(-0.512511, 0.521295, 1.30, 1800, "sunset");
    else if (name == "neon-dust")      julia( 0.285,    0.01,     1.60, 1500, "neon");
    else if (name == "viridian")       julia(-0.70176, -0.3842,   1.20, 3000, "viridis");
    else if (name == "candy-swirl")  { julia(-0.7269,   0.1889,   1.10, 3000, "candy");     c.stripe_contrast = 3.4; }
    else if (name == "ember-seahorse") mandel(-0.74364388703, 0.13182590421, 1.35 / 350.0, 2000, "ember");
    else if (name == "inferno-valley") mandel(-0.748, 0.1, 1.35 / 11.0, 2500, "inferno");
    // Presets built on popular color themes.
    else if (name == "synthwave")    { julia(-0.512511, 0.521295, 1.30, 2000, "synthwave"); c.stripe_contrast = 3.4; }
    else if (name == "nord")           julia(-0.74543,  0.11301,  1.25, 3000, "nord");
    else if (name == "dracula")      { julia(-0.7269,   0.1889,   1.10, 3000, "dracula");   c.stripe_contrast = 3.4; }
    else if (name == "gruvbox")        mandel(-0.74364388703, 0.13182590421, 1.35 / 350.0, 2000, "gruvbox");
    else if (name == "autumn")         julia(-0.8,      0.156,    1.30, 2500, "autumn");
    else if (name == "rosegold")     { julia(-0.512511, 0.521295, 1.30, 2000, "rosegold");  c.stripe_contrast = 3.4; }
    else if (name == "galaxy")         julia(-0.123,    0.745,    1.40, 2000, "galaxy");
    else if (name == "mint")           julia(-0.70176, -0.3842,   1.20, 3000, "mint");
    // Trippy fuchsia/teal on the full default Julia spiral. The CYCLIC prism
    // palette is the key: it sweeps the stripe relief through teal -> violet ->
    // fuchsia and loops, so both complements show strongly instead of the image
    // sitting in one hue. Higher stripe frequency packs in more bands.
    else if (name == "acid-swirl")   { julia(-0.512511, 0.521295, 1.30, 2000, "prism"); c.cyclic = true; c.stripe_freq = 9.0; }
    // Album-art covers (square, vice palette) -- one per design mode. Render big
    // for the final, e.g. `-P cover-mandala --size 3000x3000`.
    else if (name == "cover-mandala") { // N-fold radial symmetry -> mandala
        julia(-0.512511, 0.521295, 1.30, 1500, "vice");
        c.cyclic = true; c.stripe_freq = 8.0; c.stripe_contrast = 3.0;
        c.kaleido = 8.0;
        c.bloom = 0.45; c.vignette = 0.45; c.grain = 0.03;
    }
    else if (name == "cover-hero") {    // one deep spiral eye, cinematic
        mandel(-0.74364388703, 0.13182590421, 1.35 / 120.0, 2200, "vice");
        c.cyclic = true; c.bloom = 0.35; c.aberration = 2.0;
        c.vignette = 0.5; c.grain = 0.03;
    }
    else if (name == "cover-glitch") {  // hard RGB-split + scanlines
        julia(-0.512511, 0.521295, 1.30, 1500, "vice");
        c.cyclic = true; c.stripe_freq = 8.0; c.stripe_contrast = 3.2;
        c.aberration = 24.0; c.scanlines = 0.5; c.vignette = 0.35;
        c.grain = 0.05; c.bloom = 0.4;
    }
    else if (name == "cover-cosmic") {  // soft glowing nebula
        julia(-0.512511, 0.521295, 1.42, 1200, "vice");
        c.cyclic = true; c.bloom = 0.75; c.bloom_threshold = 0.35;
        c.vignette = 0.4; c.grain = 0.025;
    }
    // --- Style presets exercising the expanded coloring vocabulary ----------
    // Each combines flags that are off by default (Oklab interp, dual palettes,
    // posterization, inside coloring) so the look is qualitatively different
    // from anything the original 1-palette presets can produce.
    //
    // stained-glass: hard color bands of saturated jewel hues. Posterize gives
    // the screen-print blocks; Oklab keeps each band cleanly saturated; the
    // jewel palette is designed with distinct stops that READ as blocks.
    else if (name == "stained-glass") {
        julia(-0.7269, 0.1889, 1.10, 2000, "jewel");
        c.cyclic = true; c.interp = InterpMode::Oklab; c.posterize = 7;
        c.stripe_contrast = 3.0; c.bloom = 0.2;
    }
    // etching: ink-on-paper monochrome. Mono palette + posterize 5 + high
    // stripe freq packs the orbit-angle bands into discrete tone steps.
    else if (name == "etching") {
        mandel(-0.74364388703, 0.13182590421, 1.35 / 80.0, 2200, "mono");
        c.posterize = 5; c.stripe_freq = 9.0; c.stripe_contrast = 3.4;
        c.bloom = 0.0; c.color_density = 0.05;
    }
    // frost-ember: dual-palette warm/cool split. The iter layer draws the FIELD
    // in cool ice/blue (frost palette); the stripe layer draws the STRUCTURE in
    // warm fire (ember palette). Inverse of `solar-flare`.
    //   stripe_color < 1 is REQUIRED for dual-palette to show both: at the
    //   default 1.0, the overlay collapses to stripeCol*gate and the iter
    //   palette's hue never appears. 0.55 keeps the stripe structure but lets
    //   the iter field tone through underneath.
    else if (name == "frost-ember") {
        julia(-0.512511, 0.521295, 1.30, 2000, "frost");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.stripe_contrast = 3.0; c.stripe_color = 0.55;
        parsePalette("ember", c.stripe_palette);
    }
    // solar-flare: dual-palette warm-field, cool-structure. Ember field, arctic
    // spiral arms -- like burn but with a smoother contrast through Oklab.
    else if (name == "solar-flare") {
        mandel(-0.74364388703, 0.13182590421, 1.35 / 120.0, 2200, "ember");
        c.interp = InterpMode::Oklab; c.stripe_contrast = 3.0; c.bloom = 0.35;
        c.stripe_color = 0.55;
        parsePalette("arctic", c.stripe_palette);
    }
    // interior-bloom: shows the set INTERIOR colored by SAC instead of flat
    // black. The exterior gets ember (warm), the interior gets galaxy (deep
    // violet -> white), so the set body becomes its own miniature scene.
    // Uses the Douady rabbit (c=-0.122+0.745i) -- a CONNECTED Julia with a
    // substantial three-ear interior body. Dendrite-like Julia constants have
    // almost no interior so color_inside would have nothing to color.
    else if (name == "interior-bloom") {
        julia(-0.122, 0.745, 1.40, 1500, "ember");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.color_inside = true;
        parsePalette("galaxy", c.inside_palette);
        c.bloom = 0.3;
    }
    // oil-painting: low-contrast warm field with soft posterized bands -- the
    // discrete tone steps read as oil brush strokes rather than smooth ramps.
    else if (name == "oil-painting") {
        julia(-0.8, 0.156, 1.30, 2000, "autumn");
        c.interp = InterpMode::Oklab; c.posterize = 12;
        c.stripe_contrast = 2.0; c.bloom = 0.25; c.grain = 0.04;
    }
    // --- Vibrant aesthetic combos (Oklab + dual-palette / posterize / interior)
    // Every one is intentionally opinionated: a single color story, not a
    // generic ramp. All set --interp oklab so opposed hues stay saturated
    // through their midpoints; dual-palette presets keep stripe_color in (0,1)
    // so BOTH palette contributions actually appear (at 1.0 the iter palette
    // hue collapses away).

    // tropical: hot fuchsia field, electric lime spiral arms. Classic
    // complementary pair (the fuchsia-green axis) -- one of the most vivid
    // visual contrasts you can pull from this fractal vocabulary.
    else if (name == "tropical") {
        julia(-0.512511, 0.521295, 1.30, 2000, "vice");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.stripe_color = 0.6; c.stripe_contrast = 3.0; c.bloom = 0.3;
        parsePalette("lime", c.stripe_palette);
    }
    // cyberpunk: black + electric cyan/magenta + white peaks. Single palette,
    // very saturated, reads like neon signage at night.
    else if (name == "cyberpunk") {
        julia(-0.7269, 0.1889, 1.10, 2500, "electric");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.stripe_contrast = 3.4; c.bloom = 0.4;
    }
    // persimmon: burnt orange field, forest emerald spiral arms. A muted
    // complementary (orange/green) — bolder than the autumn preset and with the
    // dual palette giving genuinely separate hues for field vs structure.
    else if (name == "persimmon") {
        julia(-0.8, 0.156, 1.30, 2000, "ember");
        c.interp = InterpMode::Oklab;
        c.stripe_color = 0.6; c.stripe_contrast = 2.8; c.bloom = 0.3;
        parsePalette("emerald", c.stripe_palette);
    }
    // reef: coral pink field with deep teal spiral arms. The shallow-water
    // photo aesthetic -- bright warm with cool depths.
    else if (name == "reef") {
        julia(-0.7269, 0.1889, 1.10, 2500, "candy");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.stripe_color = 0.55; c.stripe_contrast = 3.0; c.bloom = 0.35;
        parsePalette("ocean", c.stripe_palette);
    }
    // cosmic-yellow: deep galaxy violet field with golden spiral structure.
    // Yellow-violet is a high-contrast color-theory complementary pair.
    else if (name == "cosmic-yellow") {
        julia(-0.123, 0.745, 1.40, 2000, "galaxy");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.stripe_color = 0.55; c.stripe_contrast = 3.0; c.bloom = 0.35;
        parsePalette("gold", c.stripe_palette);
    }
    // risograph: 3-color print look (federal blue, fluorescent pink, sunflower
    // yellow on cream). Posterize 4 gives the band-like ink layering.
    else if (name == "risograph") {
        mandel(-0.74364388703, 0.13182590421, 1.35 / 80.0, 2200, "riso");
        c.interp = InterpMode::Oklab; c.posterize = 4;
        c.stripe_contrast = 3.0; c.bloom = 0.0;
    }
    // mid-century: Eames-era teal/orange/mustard atomic palette with hard
    // posterized bands. Reads as a 1960s screen-printed travel poster.
    else if (name == "mid-century") {
        julia(-0.74543, 0.11301, 1.25, 2000, "atomic");
        c.interp = InterpMode::Oklab; c.posterize = 5;
        c.stripe_contrast = 3.0; c.bloom = 0.15;
    }
    // lava-lake: extreme heat-cold inversion. Set INTERIOR is iced over (ice
    // palette inside) while the exterior burns (fire palette outside). Shows
    // the color_inside mechanism at maximum contrast.
    else if (name == "lava-lake") {
        julia(-0.122, 0.745, 1.40, 1500, "fire");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.color_inside = true; c.bloom = 0.35;
        parsePalette("ice", c.inside_palette);
    }
    // magma-core: dark mono exterior, magma glowing INSIDE the set. Like
    // looking at a planet whose crust is dark stone but the interior glows
    // molten through the cracks at the edges.
    else if (name == "magma-core") {
        julia(-0.122, 0.745, 1.40, 1500, "noir");
        c.cyclic = false; c.interp = InterpMode::Oklab;
        c.color_inside = true; c.bloom = 0.4;
        parsePalette("magma", c.inside_palette);
    }
    // vaporwave-poster: vapor palette posterized to discrete bands. Pink/cyan/
    // violet rectangles like a Surfaces / Mac Plus album cover.
    else if (name == "vaporwave-poster") {
        julia(-0.512511, 0.521295, 1.30, 1800, "vapor");
        c.cyclic = true; c.interp = InterpMode::Oklab; c.posterize = 7;
        c.stripe_contrast = 3.0; c.bloom = 0.25;
    }
    // --- Hybrid (escape-time + Buddhabrot) presets --------------------------
    // These pre-render the Buddhabrot to overlay orbit-density information
    // that no per-pixel computation can produce. CPU+GPU two-stage; not deep.
    //
    // nebula-ghost: art-print Mandelbrot. Black silhouette on a violet haze of
    // orbit wisps. The fractal's magma exterior is mostly dark at this view,
    // so the nebula carries the color story. Modality 1 (direct accent).
    else if (name == "nebula-ghost") {
        mandel(-0.5, 0.0, 1.4, 800, "magma");
        c.interp = InterpMode::Oklab; c.bloom = 0.3;
        c.nebula_accent = 1.0; c.nebula_color = {0.62f, 0.13f, 1.0f}; // violet
        c.nebula_accent_samples = 10.0;
    }
    // lifetime-spectrum: 3-channel Buddhabrot painting the deep-space view.
    // R = short-lived orbits, G = mid, B = long-lived. The dark noir exterior
    // makes the wisps THE image. Modality 4 (RGB nebula).
    else if (name == "lifetime-spectrum") {
        mandel(-0.5, 0.0, 1.5, 600, "noir");
        c.interp = InterpMode::Oklab; c.bloom = 0.4;
        c.nebula_rgb = true; c.nebula_accent = 1.5;
        c.nebula_r = 6000; c.nebula_g = 600; c.nebula_b = 60;
        c.nebula_accent_samples = 12.0;
    }
    // hue-tide: density modulates the stripe palette sample so the hue of the
    // spiral arms shifts where orbits piled up -- like a tide of color
    // following the trajectory flow. Modality 2 (hue shift) with a tiny
    // accent to also see WHERE density is.
    else if (name == "hue-tide") {
        julia(-0.7269, 0.1889, 1.10, 2500, "neon");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.stripe_contrast = 3.0; c.bloom = 0.3;
        c.nebula_hue_shift = 0.18; c.nebula_accent = 0.15;
        c.nebula_color = {1, 1, 1};
        c.nebula_accent_samples = 8.0;
    }
    // nebula-aurora: maximalist. Density drives BOTH a hue shift AND a bloom
    // mask, so the bright glow follows orbit hot spots while the colors shift
    // through them. The fractal structure stays sharp underneath the diffuse
    // aurora-like glow above it. Modalities 2 + 3 stacked.
    else if (name == "nebula-aurora") {
        julia(-0.512511, 0.521295, 1.30, 2000, "vice");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.stripe_contrast = 3.0;
        c.bloom = 0.5; c.bloom_threshold = 0.35;
        c.nebula_hue_shift = 0.12;
        c.nebula_bloom = 1.8;
        c.nebula_color = {1.0f, 0.85f, 0.4f}; // gold tinted bloom
        c.nebula_accent_samples = 10.0;
        c.vignette = 0.4;
    }
    // --- Zoomed hybrid presets: nebula at scales where fractal structure is
    // intricate (still float-precision since the CPU buddhabrot can't align
    // under --deep). Wisps weave THROUGH the structure rather than around the
    // whole set; the hybrid character is most visible here.

    // nebula-seahorse: Mandelbrot seahorse valley with violet wisps traced
    // through the tail. Cover-hero framing + nebula accent.
    else if (name == "nebula-seahorse") {
        mandel(-0.74364388703, 0.13182590421, 1.35 / 120.0, 2200, "vice");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.bloom = 0.35; c.vignette = 0.4;
        c.nebula_accent = 0.7; c.nebula_color = {0.65f, 0.2f, 1.0f};
        c.nebula_accent_samples = 12.0;
    }
    // nebula-elephant: Mandelbrot elephant valley with 3-channel RGB nebula
    // tracing trunks in different orbit-lifetime bands. Rich multi-hue wisps.
    else if (name == "nebula-elephant") {
        // Mono mode at this scale: RGB lifetime-spectrum at scale 0.04 needs
        // hundreds of millions of samples per channel to read smooth (the
        // visible plane is ~0.1% of whole-set area, split three ways). Mono
        // single-channel is well within reach at 40M and gives clean cyan
        // wisps tracing the elephant trunks. lifetime-spectrum already shows
        // the RGB modality at whole-set scale where it works cleanly.
        mandel(-0.7454, 0.113, 0.04, 2000, "noir");
        c.interp = InterpMode::Oklab; c.bloom = 0.4;
        c.nebula_accent = 1.5;
        c.nebula_color = {0.4f, 0.85f, 1.0f}; // cool cyan wisps
        c.nebula_accent_samples = 40.0;
    }
    // dragon-storm: Julia twin-dragon zoomed off-center so the asymmetric
    // buddhabrot fills the void. Density-driven bloom turns each spiral core
    // into a lightning flash. (Julia buddhabrot has no mirror symmetry --
    // gives a more dramatic asymmetric density distribution than Mandelbrot.)
    else if (name == "dragon-storm") {
        julia(-0.512511, 0.521295, 0.7, 2000, "electric");
        c.center_x = 0.3; c.center_y = -0.1;
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.stripe_contrast = 3.2;
        c.bloom = 0.5; c.bloom_threshold = 0.4;
        c.nebula_bloom = 1.6;
        c.nebula_color = {0.5f, 0.9f, 1.0f};
        c.nebula_accent_samples = 10.0;
    }
    // rabbit-ember: Douady rabbit zoomed into the ear junction with the whole
    // hybrid stack: COLORED INTERIOR (ember palette inside the set body) plus
    // nebula hue-shift modulating the exterior so density traces hue zones.
    // Two distinct color stories layered.
    else if (name == "rabbit-ember") {
        julia(-0.122, 0.745, 0.6, 1500, "frost");
        c.center_x = 0.0; c.center_y = 0.55;
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.color_inside = true;
        parsePalette("ember", c.inside_palette);
        c.nebula_hue_shift = 0.15; c.nebula_accent = 0.25;
        c.nebula_color = {1, 1, 1};
        c.nebula_accent_samples = 10.0;
        c.bloom = 0.35;
    }
    // dendrite-glow: dendrite Julia zoomed into one spiral, with all three
    // density modalities at low strength. Subtle but the layered effect gives
    // the dendrite a sense of motion / atmospheric depth that pure escape-
    // time can't reach.
    else if (name == "dendrite-glow") {
        julia(-0.7269, 0.1889, 0.5, 2500, "neon-dark");
        c.center_x = -0.3; c.center_y = 0.1;
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.stripe_contrast = 3.0;
        c.bloom = 0.4; c.bloom_threshold = 0.4;
        c.nebula_accent = 0.4; c.nebula_color = {0.4f, 1.0f, 0.7f}; // mint
        c.nebula_hue_shift = 0.08;
        c.nebula_bloom = 0.8;
        c.nebula_accent_samples = 10.0;
    }
    // plasma-storm: Mandelbrot in the inferno-valley zoom with RGB nebula on
    // top of an inferno palette. The R/G/B wisps add chromatic mist over an
    // already vivid escape-time render -- maximalist hybrid.
    // --- Maths Town-style log-iter presets ---------------------------------
    // The signature look of Adam's channel: concentric color bands radiating
    // from the set boundary. Cyclic palette + log iteration coloring means
    // each natural-log unit of escape time gets a fresh palette band, so the
    // SAME relative depths always land on the same color (the camera flies
    // through colored "rooms," instead of every pixel rainbow-shifting per
    // frame). Stripe is off to keep the bands clean; angle-color adds a
    // subtle hue rotation by boundary direction (Maths Town's "Angle" mode).

    // mathstown-classic: whole-set Mandelbrot with the canonical concentric
    // rainbow bands. The single most recognizable Maths Town aesthetic.
    else if (name == "mathstown-classic") {
        mandel(-0.5, 0.0, 1.4, 1000, "prism");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.log_iter = true; c.color_density = 1.0;
        c.stripe_color = 0.0;
        c.angle_color = 0.10;
        c.bloom = 0.3;
    }
    // mathstown-seahorse: zoomed into the seahorse valley with denser bands.
    // Each spiral arm's depth in the structure reads as which band it falls
    // in, so the relative geometry pops without coloring details to death.
    else if (name == "mathstown-seahorse") {
        mandel(-0.74364388703, 0.13182590421, 0.005, 4000, "prism");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.log_iter = true; c.color_density = 1.5;
        c.stripe_color = 0.0;
        c.angle_color = 0.05;
        c.bloom = 0.3;
    }
    // mathstown-deep: df64 deep zoom (~1e11x) on an exact Misiurewicz with
    // log-iter active in deep.frag. Bands fly through the structure layer by
    // layer as you dive in -- the Maths Town aesthetic in true deep zoom
    // territory. Dark-anchored neon-dark palette so "lakes" stay dark.
    else if (name == "mathstown-deep") {
        mandel(-0.7432918908524302, 0.1312405523087976, 1e-11, 6000, "neon-dark");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.log_iter = true; c.color_density = 1.2;
        c.stripe_color = 0.0;
        c.deep = true;
        c.bloom = 0.25;
    }
    // mathstown-cosmos: Julia + galaxy palette + log-iter cyclic. Bands swirl
    // around the set in deep-space violets and pinks -- the "wider gamut"
    // Maths Town aesthetic with a non-rainbow palette.
    else if (name == "mathstown-cosmos") {
        julia(-0.7269, 0.1889, 1.10, 2000, "galaxy");
        c.cyclic = true; c.interp = InterpMode::Oklab;
        c.log_iter = true; c.color_density = 1.0;
        c.stripe_color = 0.0;
        c.angle_color = 0.08;
        c.bloom = 0.35;
    }
    // --- Maths Town monochromatic-relief presets (the OTHER signature look).
    // Restrained palette (dark->light single hue family) + Slopes (true depth
    // shading via gradient of log(mu)) + smooth low-density exp coloring + a
    // distinct inside_color accent for the deepest spiral eyes. Mimics the
    // embossed-fractal aesthetic of Adam's monochromatic shots.

    // mathstown-teal: dark teal field, lighter teal-grey filaments radiating
    // out from a Misiurewicz center in a star pattern, indigo dots at the
    // deepest spiral eyes. Mirrors the teal radiating-star reference frame.
    // Center is an exact Misiurewicz point so the radiating filaments are
    // sharp and self-similar.
    else if (name == "mathstown-teal") {
        mandel(-0.7432918908524302, 0.1312405523087976, 0.002, 3500, "tealfog");
        c.interp = InterpMode::Oklab;
        c.color_density = 0.013;
        c.stripe_color = 0.0;
        c.slopes = 0.6; c.slopes_spec = 0.35;
        c.light_angle = 135.0; c.light_height = 1.2; c.height_scale = 2.2;
        c.shininess = 14.0;
        c.bloom = 0.4; c.bloom_threshold = 0.6;
        c.inside_color = {0.10f, 0.08f, 0.30f}; // subdued indigo eye
    }
    // mathstown-sepia: warm cream/tan field with darker filaments, subtle
    // green-cream highlights at spiral cores. Reference image 2.
    else if (name == "mathstown-sepia") {
        mandel(-0.74364388703, 0.13182590421, 0.004, 3500, "sepia");
        c.interp = InterpMode::Oklab;
        c.color_density = 0.01;
        c.stripe_color = 0.0;
        c.slopes = 0.6; c.slopes_spec = 0.35;
        c.light_angle = 110.0; c.light_height = 1.4; c.height_scale = 2.2;
        c.shininess = 12.0;
        c.bloom = 0.35; c.bloom_threshold = 0.6;
        c.inside_color = {0.25f, 0.30f, 0.15f}; // muted olive-green eye
    }
    // mathstown-chrome: silver/grey embossed cluster of spirals with tiny
    // indigo eye accents. The purest monochromatic Maths Town render.
    // Seahorse-valley framing has more exterior structure than elephant valley
    // at the same scale, so the chrome field reads bright instead of being
    // dominated by inside_color. Reference image 3.
    else if (name == "mathstown-chrome") {
        mandel(-0.74364388703, 0.13182590421, 0.0035, 4000, "chrome");
        c.interp = InterpMode::Oklab;
        c.color_density = 0.011;
        c.stripe_color = 0.0;
        c.slopes = 0.7; c.slopes_spec = 0.45;
        c.light_angle = 125.0; c.light_height = 1.0; c.height_scale = 2.6;
        c.shininess = 16.0;
        c.bloom = 0.3; c.bloom_threshold = 0.65;
        c.inside_color = {0.10f, 0.08f, 0.40f}; // indigo eye
    }
    // mathstown-honey: warm gold field with lime-green spiral core highlights.
    // Reference image 4.
    else if (name == "mathstown-honey") {
        mandel(-0.748, 0.1, 0.003, 3500, "honeycomb");
        c.interp = InterpMode::Oklab;
        c.color_density = 0.011;
        c.stripe_color = 0.0;
        c.slopes = 0.65; c.slopes_spec = 0.4;
        c.light_angle = 140.0; c.light_height = 1.2; c.height_scale = 2.4;
        c.shininess = 14.0;
        c.bloom = 0.4; c.bloom_threshold = 0.55;
        c.inside_color = {0.30f, 0.55f, 0.20f}; // lime green eye
    }
    else if (name == "plasma-storm") {
        mandel(-0.748, 0.1, 0.09, 2500, "inferno");
        c.interp = InterpMode::Oklab; c.bloom = 0.4;
        c.nebula_rgb = true; c.nebula_accent = 0.8;
        c.nebula_r = 6000; c.nebula_g = 500; c.nebula_b = 50;
        // RGB nebula at zoom is sample-hungry per channel (see nebula-elephant).
        // At scale ~0.09, 40M samples gets the chromatic mist smooth instead of
        // reading as shot noise.
        c.nebula_accent_samples = 40.0;
        c.vignette = 0.3;
    }
    // Other iteration formulas (--formula). SAC coloring carries over to all the
    // escape-time ones; Newton has its own basin coloring.
    else if (name == "burning-ship") { // the "armada", reflected and jagged
        mandel(-0.5, -0.5, 1.45, 1500, "ember"); c.formula = Formula::BurningShip;
    }
    else if (name == "phoenix") {      // flame-like, uses the z_prev term
        julia(0.5667, 0.0, 1.35, 1000, "ice");
        c.formula = Formula::Phoenix; c.phoenix_pre = -0.5;
    }
    else if (name == "newton") {       // three root basins of z^3 - 1
        julia(0.0, 0.0, 1.40, 80, "neon");
        c.formula = Formula::Newton; c.cyclic = true; c.bloom = 0.2;
    }
    else return false;
    return true;
}

// Names of the built-in presets, for help text.
const std::vector<std::string>& presetNames() {
    static const std::vector<std::string> kNames = {
        "noir-spiral", "frostbite", "molten", "deep-ocean", "dusk",
        "neon-dust", "viridian", "candy-swirl", "ember-seahorse", "inferno-valley",
        "synthwave", "nord", "dracula", "gruvbox", "autumn", "rosegold",
        "galaxy", "mint", "acid-swirl",
        "cover-mandala", "cover-hero", "cover-glitch", "cover-cosmic",
        "stained-glass", "etching", "frost-ember", "solar-flare",
        "interior-bloom", "oil-painting",
        "tropical", "cyberpunk", "persimmon", "reef", "cosmic-yellow",
        "risograph", "mid-century", "lava-lake", "magma-core", "vaporwave-poster",
        "nebula-ghost", "lifetime-spectrum", "hue-tide", "nebula-aurora",
        "nebula-seahorse", "nebula-elephant", "dragon-storm", "rabbit-ember",
        "dendrite-glow", "plasma-storm",
        "mathstown-classic", "mathstown-seahorse", "mathstown-deep", "mathstown-cosmos",
        "mathstown-teal", "mathstown-sepia", "mathstown-chrome", "mathstown-honey",
        "burning-ship", "phoenix", "newton",
    };
    return kNames;
}

// Parse "WIDTHxHEIGHT" (e.g. "1920x1080").
bool parseSize(const std::string& s, int& w, int& h) {
    size_t x = s.find_first_of("xX");
    if (x == std::string::npos) return false;
    try {
        w = std::stoi(s.substr(0, x));
        h = std::stoi(s.substr(x + 1));
    } catch (...) { return false; }
    return w > 0 && h > 0;
}

} // namespace

std::string helpText() {
    std::ostringstream o;
    o <<
R"(fractal — a GPU fractal visualizer for stunning Julia & Mandelbrot art.

USAGE
  fractal render [options]      Render a single still image (PNG).
  fractal video  [options]      Render a seamless animation (MP4, needs ffmpeg).
  fractal buddhabrot [options]  Render a Buddhabrot/Nebulabrot density (PNG).
  fractal explore [options]     Open the interactive explorer window.
  fractal center [options]      Refine a zoom target to an exact periodic point.
  fractal help                  Show this help.

COMMON OPTIONS
  -P, --preset <name>             Start from a curated preset (see PRESETS)
  -t, --type <julia|mandelbrot>   Fractal family            (default: julia)
      --formula <name>            quadratic|burningship|tricorn|phoenix|newton
                                  (default: quadratic)
      --phoenix-p <float>         Phoenix z_prev coefficient (default: -0.5)
      --cre <float>               Julia constant real part  (default: -0.512511)
      --cim <float>               Julia constant imag part  (default: 0.521295)
      --center-x <float>          View center x             (default: 0)
      --center-y <float>          View center y             (default: 0)
      --scale <float>             View half-height in plane (default: 1.35)
      --zoom <float>              Zoom factor (scale = 1.35/zoom)
  -i, --iterations <int>          Max iterations            (default: 400)
      --exponent <float>          z^exponent + c            (default: 2)
      --bailout <float>           Escape radius             (default: 10000)
  -w, --width <int>               Image width px            (default: 1600)
      --height <int>              Image height px           (default: 1600)
      --size <WxH>                Set width and height at once
      --ssaa <int>                Supersampling factor      (default: 4)
      --deep                      Double-float precision for zooms past ~1e4
                                  (quadratic SAC path only; slower)
  -p, --palette <spec>            Named palette or hex list (default: noir)
      --cyclic                    Loop the gradient (ramps are open by default)
      --stripe-color <float>      Stripe overlay weight 0..1(default: 1.0)
      --stripe-freq <float>       Stripe density 4/6/8 best (default: 6)
      --stripe-contrast <float>   Stripe contrast stretch   (default: 2.2)
      --color-density <float>     Iteration ramp (0=stripe) (default: 0.035)
      --color-offset <float>      Palette phase shift [0,1) (default: 0)
      --angle-color <float>       Escape-angle hue weight   (default: 0)
      --trap-color <float>        Orbit-trap hue weight     (default: 0)
      --trap-x <float>            Orbit-trap point x        (default: 0)
      --trap-y <float>            Orbit-trap point y        (default: 0)
      --inside <hex>              Color of points in the set(default: #000000)
      --saturation <float>        Saturation grade          (default: 1.3)
      --gamma <float>             Gamma grade               (default: 1.05)
      --black-point <float>       Crush near-blacks to black (default: 0.08)
      --shading <float>           Diffuse light strength    (default: 0)
      --light-angle <float>       Light azimuth degrees     (default: 135)
      --light-height <float>      Light elevation           (default: 1.0)
      --specular <float>          Specular sheen strength   (default: 0)
      --shininess <float>         Specular tightness        (default: 18)
      --height-scale <float>      Relief depth for lighting (default: 1.5)
      --glow <float>              Distance-estimate glow     (default: 0)
      --bloom <float>             Luminous bloom strength    (default: 0.3)
      --bloom-threshold <float>   Brightness to bloom        (default: 0.5)
      --falloff <float>           Exterior fade-to-void      (default: 0)

  EXPANDED COLORING (additive; everything off-by-default preserves old behavior)
      --interp <rgb|oklab>        Gradient interpolation space (default: rgb).
                                  oklab keeps opposed hues clean instead of
                                  passing through grey/brown.
      --stripe-palette <spec>     Separate palette for the stripe layer (default:
                                  reuse main). Pair `-p ember --stripe-palette
                                  arctic` for warm field, cool spirals.
      --posterize <int>           Quantize gradient to N flat bands (default: 0/
                                  off, otherwise 2..32). Screen-print / stained-
                                  glass look; great with --interp oklab.
      --color-inside              Color set INTERIOR by orbit SAC instead of
                                  --inside flat. Off by default.
      --inside-palette <spec>     Palette for set interior when --color-inside
                                  (default: reuse main palette).
      --nebula-accent <float>     Overlay a Buddhabrot orbit-density "ghost"
                                  layer on top of the escape-time render
                                  (default: 0/off). A genuinely orthogonal
                                  color axis: density depends on what NEIGHBOR
                                  orbits do, not the pixel's own.
      --nebula-color <hex>        Wisp tint (default: #ffffff).
      --nebula-accent-samples <f> Millions of CPU samples for the nebula pre-
                                  pass (default: 8). Higher = smoother wisps,
                                  longer pre-render.
      --nebula-hue-shift <float>  Density modulates stripe palette sample so
                                  hue shifts trace orbit density (default: 0).
      --nebula-bloom <float>      Density adds to bloom bright-pass mask --
                                  wisps glow regardless of pixel brightness
                                  (default: 0). Needs --bloom > 0.
      --nebula-rgb                Use 3-channel Nebulabrot as accent: wisps are
                                  multi-hued by orbit lifetime (--nebula-r/g/b
                                  thresholds apply). Disables --nebula-color.
      --log-iter                  Maths Town-style log iteration coloring:
                                  iterS = fract(log(mu)*color_density + offset)
                                  instead of the default exp-compressed ramp.
                                  Each decade of escape time gets a palette
                                  band; deep zooms fly through colored zones
                                  instead of saturating to the bright end.
                                  Use color_density ~0.3-2.0 (not 0.035).
                                  Pairs well with a cyclic palette.
      --slopes <float>            Maths Town-style "Slopes": directional
                                  light from gradient of log(mu) (default 0).
                                  Unlike --shading (which lights luminance
                                  and breaks on banded palettes), --slopes
                                  reads TRUE depth so cyclic/posterized
                                  palettes still get clean 3D relief. Uses
                                  --light-angle / --light-height for
                                  direction; --slopes-spec adds highlights.
      --slopes-spec <float>       Specular highlight strength for --slopes
                                  (default 0). Tightness reuses --shininess.

  ALBUM-ART / DESIGN LAYER (all off at 0; see the cover-* presets)
      --kaleido <float>           N mirrored wedges -> mandala (default: 0/off)
      --kaleido-angle <float>     Rotate the symmetry, degrees (default: 0)
      --aberration <float>        Chromatic RGB split, pixels (default: 0)
      --vignette <float>          Darken corners 0..1         (default: 0)
      --grain <float>             Film-grain noise amount     (default: 0)
      --scanlines <float>         CRT scanline depth 0..1     (default: 0)
  -o, --output <path>             Output file

VIDEO OPTIONS
      --mode <rotate|zoom|cycle|spin> Animation type        (default: rotate)
                                  (spin = rotate a --kaleido mandala, seamless)
  -d, --duration <float>          Seconds                   (default: 20)
      --fps <int>                 Frames per second         (default: 30)
      --crf <int>                 x264 quality, lower=better(default: 18)
      --color-cycles <float>      Palette sweeps over the clip, any mode (def: 0)
      --rotate-radius <float>     |c| for rotate mode       (default: 0.7885)
      --zoom-end <float>          End scale for zoom mode    (default: 0.005)
      --zoom-target-x <float>     Zoom target x             (default: 0)
      --zoom-target-y <float>     Zoom target y             (default: 0)

BUDDHABROT OPTIONS (the `buddhabrot` command; CPU, quadratic only)
      --samples <float>           Millions of seed orbits    (default: 30)
      --nebula                    RGB Nebulabrot (3 thresholds) vs palette density
      --nebula-r <int>            R channel max-iter         (default: 5000)
      --nebula-g <int>            G channel max-iter         (default: 500)
      --nebula-b <int>            B channel max-iter         (default: 50)
      --buddha-gamma <float>      Density tone curve         (default: 2.2)
      (single-channel density is colored through -p / --palette; -i sets the
       threshold. center/scale/size frame the output; wide views work best.)

EXPLORER (the `explore` command): drag to pan, scroll to zoom, keys to tweak.
      Press the controls printed on launch; Space prints a render command,
      Enter saves a PNG. Defaults to a 900x900 window.

CENTER SOLVER (the `center` command): Newton-refine an approximate target to an
exact self-similar point so deep zooms stay locked. Pass the eyeballed guess as
--center-x/--center-y (and --cre/--cim for a Julia set), then:
      --max-period <int>          Largest period to search    (default: 128)
      --period <int>              Force one period (skip search)
      --misiurewicz               Mandelbrot: find a Misiurewicz point instead of
                                  a nucleus (self-similar, no interior -> best for
                                  deep zooms; a nucleus has a body you fall into)
      --max-preperiod <int>       Largest preperiod to search (default: 48)
      --scale <float>             Search radius around the guess (default: 0.01)
  Julia: finds the nearest repelling periodic point (a spiral eye). Mandelbrot:
  the period-p nucleus (a minibrot center), or with --misiurewicz a preperiodic
  spiral center. Prints exact coordinates and a ready-to-run video command.
  Quadratic map only.

PALETTES
  )";
    auto names = builtinPaletteNames();
    for (size_t i = 0; i < names.size(); ++i)
        o << names[i] << (i + 1 < names.size() ? ", " : "");
    o << R"(
  ...or a custom comma-separated hex list, e.g. "#05010d,#ff7b54,#3fd0c9".

PRESETS (use with -P; override any field with explicit flags)
  )";
    auto presets = presetNames();
    for (size_t i = 0; i < presets.size(); ++i)
        o << presets[i] << (i + 1 < presets.size() ? ", " : "");
    o << R"(

EXAMPLES
  fractal render -P frostbite -o spiral.png
  fractal render -P ember-seahorse --ssaa 6 -o seahorse.png
  fractal render -p aurora -o spiral.png
  fractal render --cre -0.8 --cim 0.156 --zoom 1.4 --ssaa 4 -o dendrite.png
  fractal video --mode rotate -d 20 --fps 30 -o loop.mp4
  fractal video --type mandelbrot --mode zoom --zoom-target-x -0.743 \
                --zoom-target-y 0.131 --zoom-end 0.0005 -o dive.mp4
  fractal buddhabrot --nebula --samples 60 -o nebula.png
  fractal buddhabrot -p gold --samples 40 -o buddha.png
  fractal explore -P ember-seahorse
)";
    return o.str();
}

ParsedArgs parseArgs(const std::vector<std::string>& args) {
    ParsedArgs out;

    if (args.empty()) { out.kind = CommandKind::Help; return out; }

    const std::string& cmd = args[0];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        out.kind = CommandKind::Help;
        return out;
    }
    if (cmd == "render")      out.kind = CommandKind::Render;
    else if (cmd == "video")  out.kind = CommandKind::Video;
    else if (cmd == "buddhabrot" || cmd == "buddha") out.kind = CommandKind::Buddha;
    else if (cmd == "explore") out.kind = CommandKind::Explore;
    else if (cmd == "center")  out.kind = CommandKind::Center;
    else { out.error = "unknown command '" + cmd + "' (expected render, video, buddhabrot, explore, or center)"; return out; }

    // Parse everything into a VideoConfig; the Render path uses the base slice.
    VideoConfig cfg;
    std::string palette_spec = "noir";
    bool output_set = false;
    bool ssaa_set = false;
    bool size_set = false;
    bool scale_set = false;
    int  max_period = 128;
    int  find_period = 0;
    bool misiurewicz = false;
    int  max_preperiod = 48;

    // Apply a preset first (as the base) so explicit flags can override it,
    // regardless of where --preset appears in the arguments.
    for (size_t k = 1; k + 1 < args.size(); ++k) {
        if (args[k] == "--preset" || args[k] == "-P") {
            if (!applyPreset(args[k + 1], cfg, palette_spec)) {
                out.error = "unknown preset '" + args[k + 1] + "'";
                return out;
            }
        }
    }

    Cursor cur{args, 1, ""};
    while (cur.hasNext()) {
        std::string flag = cur.args[cur.i++];
        auto fail = [&](const std::string& m) { out.error = m; };

        if (flag == "--help" || flag == "-h") { out.kind = CommandKind::Help; return out; }

        // Already applied in the pre-scan above; just consume its value.
        else if (flag == "--preset" || flag == "-P") {
            std::string v; if (!cur.nextStr(flag, v)) break;
        }
        else if (flag == "-t" || flag == "--type") {
            std::string v; if (!cur.nextStr(flag, v)) break;
            if (v == "julia") cfg.type = FractalType::Julia;
            else if (v == "mandelbrot") cfg.type = FractalType::Mandelbrot;
            else { fail("--type must be 'julia' or 'mandelbrot', got '" + v + "'"); break; }
        }
        else if (flag == "--formula") {
            std::string v; if (!cur.nextStr(flag, v)) break;
            if      (v == "quadratic")    cfg.formula = Formula::Quadratic;
            else if (v == "burningship")  cfg.formula = Formula::BurningShip;
            else if (v == "tricorn")      cfg.formula = Formula::Tricorn;
            else if (v == "phoenix")      cfg.formula = Formula::Phoenix;
            else if (v == "newton")       cfg.formula = Formula::Newton;
            else { fail("--formula must be quadratic, burningship, tricorn, phoenix, or newton; got '" + v + "'"); break; }
        }
        else if (flag == "--phoenix-p") { if (!cur.nextDouble(flag, cfg.phoenix_pre)) break; }
        else if (flag == "--cre") { if (!cur.nextDouble(flag, cfg.julia_cre)) break; }
        else if (flag == "--cim") { if (!cur.nextDouble(flag, cfg.julia_cim)) break; }
        else if (flag == "--center-x" || flag == "--cx") { if (!cur.nextDouble(flag, cfg.center_x)) break; }
        else if (flag == "--center-y" || flag == "--cy") { if (!cur.nextDouble(flag, cfg.center_y)) break; }
        else if (flag == "--scale") { if (!cur.nextDouble(flag, cfg.scale)) break; scale_set = true; }
        else if (flag == "--zoom") {
            double z; if (!cur.nextDouble(flag, z)) break;
            if (z <= 0) { fail("--zoom must be positive"); break; }
            cfg.scale = 1.35 / z; scale_set = true;
        }
        else if (flag == "-i" || flag == "--iterations" || flag == "--iter") {
            if (!cur.nextInt(flag, cfg.max_iter)) break;
            if (cfg.max_iter < 1) { fail("--iterations must be >= 1"); break; }
        }
        else if (flag == "--exponent") { if (!cur.nextDouble(flag, cfg.exponent)) break; }
        else if (flag == "--bailout")  { if (!cur.nextDouble(flag, cfg.bailout)) break; }
        else if (flag == "-w" || flag == "--width")  {
            if (!cur.nextInt(flag, cfg.width)) break;
            if (cfg.width < 1) { fail("--width must be >= 1"); break; }
            size_set = true;
        }
        else if (flag == "--height") {
            if (!cur.nextInt(flag, cfg.height)) break;
            if (cfg.height < 1) { fail("--height must be >= 1"); break; }
            size_set = true;
        }
        else if (flag == "--size") {
            std::string v; if (!cur.nextStr(flag, v)) break;
            if (!parseSize(v, cfg.width, cfg.height)) { fail("--size must be WxH, e.g. 1920x1080"); break; }
            size_set = true;
        }
        else if (flag == "--ssaa") {
            if (!cur.nextInt(flag, cfg.ssaa)) break;
            if (cfg.ssaa < 1 || cfg.ssaa > 8) { fail("--ssaa must be between 1 and 8"); break; }
            ssaa_set = true;
        }
        else if (flag == "-p" || flag == "--palette") { if (!cur.nextStr(flag, palette_spec)) break; }
        else if (flag == "--cyclic")    { cfg.cyclic = true; }
        else if (flag == "--no-cyclic") { cfg.cyclic = false; }
        else if (flag == "--color-density") { if (!cur.nextDouble(flag, cfg.color_density)) break; }
        else if (flag == "--color-offset")  { if (!cur.nextDouble(flag, cfg.color_offset)) break; }
        else if (flag == "--stripe-color")    { if (!cur.nextDouble(flag, cfg.stripe_color)) break; }
        else if (flag == "--stripe-freq")     { if (!cur.nextDouble(flag, cfg.stripe_freq)) break; }
        else if (flag == "--stripe-contrast") { if (!cur.nextDouble(flag, cfg.stripe_contrast)) break; }
        else if (flag == "--angle-color")   { if (!cur.nextDouble(flag, cfg.angle_color)) break; }
        else if (flag == "--trap-color")    { if (!cur.nextDouble(flag, cfg.trap_color)) break; }
        else if (flag == "--trap-x")        { if (!cur.nextDouble(flag, cfg.trap_x)) break; }
        else if (flag == "--trap-y")        { if (!cur.nextDouble(flag, cfg.trap_y)) break; }
        else if (flag == "--inside") {
            std::string v; if (!cur.nextStr(flag, v)) break;
            if (!parseHexColor(v, cfg.inside_color)) { fail("--inside must be a hex color, got '" + v + "'"); break; }
        }
        else if (flag == "--saturation") { if (!cur.nextDouble(flag, cfg.saturation)) break; }
        else if (flag == "--gamma")      { if (!cur.nextDouble(flag, cfg.gamma)) break; }
        else if (flag == "--black-point"){ if (!cur.nextDouble(flag, cfg.black_point)) break; }
        else if (flag == "--shading")    { if (!cur.nextDouble(flag, cfg.shading)) break; }
        else if (flag == "--light-angle"){ if (!cur.nextDouble(flag, cfg.light_angle)) break; }
        else if (flag == "--light-height"){ if (!cur.nextDouble(flag, cfg.light_height)) break; }
        else if (flag == "--specular")   { if (!cur.nextDouble(flag, cfg.specular)) break; }
        else if (flag == "--shininess")  { if (!cur.nextDouble(flag, cfg.shininess)) break; }
        else if (flag == "--height-scale"){ if (!cur.nextDouble(flag, cfg.height_scale)) break; }
        else if (flag == "--glow")       { if (!cur.nextDouble(flag, cfg.glow)) break; }
        else if (flag == "--bloom")      { if (!cur.nextDouble(flag, cfg.bloom)) break; }
        else if (flag == "--bloom-threshold") { if (!cur.nextDouble(flag, cfg.bloom_threshold)) break; }
        else if (flag == "--falloff")    { if (!cur.nextDouble(flag, cfg.falloff)) break; }
        // ---- expanded coloring vocabulary ----
        else if (flag == "--interp") {
            std::string v; if (!cur.nextStr(flag, v)) break;
            if (v == "rgb")        cfg.interp = InterpMode::Rgb;
            else if (v == "oklab") cfg.interp = InterpMode::Oklab;
            else { fail("--interp must be rgb or oklab; got '" + v + "'"); break; }
        }
        else if (flag == "--stripe-palette") {
            std::string v; if (!cur.nextStr(flag, v)) break;
            if (!parsePalette(v, cfg.stripe_palette)) { fail("--stripe-palette: unknown name or invalid hex list: '" + v + "'"); break; }
        }
        else if (flag == "--posterize") {
            if (!cur.nextInt(flag, cfg.posterize)) break;
            if (cfg.posterize < 0) { fail("--posterize must be >= 0 (0 = off, otherwise 2..)"); break; }
        }
        else if (flag == "--color-inside") { cfg.color_inside = true; }
        else if (flag == "--inside-palette") {
            std::string v; if (!cur.nextStr(flag, v)) break;
            if (!parsePalette(v, cfg.inside_palette)) { fail("--inside-palette: unknown name or invalid hex list: '" + v + "'"); break; }
        }
        else if (flag == "--nebula-accent")  { if (!cur.nextDouble(flag, cfg.nebula_accent)) break; }
        else if (flag == "--nebula-color")   {
            std::string v; if (!cur.nextStr(flag, v)) break;
            if (!parseHexColor(v, cfg.nebula_color)) { fail("--nebula-color must be a hex color, got '" + v + "'"); break; }
        }
        else if (flag == "--nebula-accent-samples") { if (!cur.nextDouble(flag, cfg.nebula_accent_samples)) break; }
        else if (flag == "--nebula-hue-shift") { if (!cur.nextDouble(flag, cfg.nebula_hue_shift)) break; }
        else if (flag == "--nebula-bloom")     { if (!cur.nextDouble(flag, cfg.nebula_bloom)) break; }
        else if (flag == "--nebula-rgb")       { cfg.nebula_rgb = true; }
        else if (flag == "--log-iter")         { cfg.log_iter = true; }
        else if (flag == "--slopes")           { if (!cur.nextDouble(flag, cfg.slopes)) break; }
        else if (flag == "--slopes-spec")      { if (!cur.nextDouble(flag, cfg.slopes_spec)) break; }
        // ---- album-art / design layer ----
        else if (flag == "--kaleido")    { if (!cur.nextDouble(flag, cfg.kaleido)) break; }
        else if (flag == "--kaleido-angle"){ if (!cur.nextDouble(flag, cfg.kaleido_angle)) break; }
        else if (flag == "--aberration") { if (!cur.nextDouble(flag, cfg.aberration)) break; }
        else if (flag == "--vignette")   { if (!cur.nextDouble(flag, cfg.vignette)) break; }
        else if (flag == "--grain")      { if (!cur.nextDouble(flag, cfg.grain)) break; }
        else if (flag == "--scanlines")  { if (!cur.nextDouble(flag, cfg.scanlines)) break; }
        else if (flag == "--deep")       { cfg.deep = true; }
        // ---- buddhabrot / nebulabrot ----
        else if (flag == "--samples")    { if (!cur.nextDouble(flag, cfg.samples)) break;
            if (cfg.samples <= 0) { fail("--samples must be positive"); break; } }
        else if (flag == "--nebula")     { cfg.nebula = true; }
        else if (flag == "--nebula-r")   { if (!cur.nextInt(flag, cfg.nebula_r)) break; }
        else if (flag == "--nebula-g")   { if (!cur.nextInt(flag, cfg.nebula_g)) break; }
        else if (flag == "--nebula-b")   { if (!cur.nextInt(flag, cfg.nebula_b)) break; }
        else if (flag == "--buddha-gamma"){ if (!cur.nextDouble(flag, cfg.buddha_gamma)) break; }
        // ---- center solver ----
        else if (flag == "--max-period") { if (!cur.nextInt(flag, max_period)) break;
            if (max_period < 1) { fail("--max-period must be >= 1"); break; } }
        else if (flag == "--period")     { if (!cur.nextInt(flag, find_period)) break;
            if (find_period < 1) { fail("--period must be >= 1"); break; } }
        else if (flag == "--misiurewicz") { misiurewicz = true; }
        else if (flag == "--max-preperiod") { if (!cur.nextInt(flag, max_preperiod)) break;
            if (max_preperiod < 1) { fail("--max-preperiod must be >= 1"); break; } }
        else if (flag == "-o" || flag == "--output") {
            if (!cur.nextStr(flag, cfg.output)) break;
            output_set = true;
        }
        // ---- video-only ----
        else if (flag == "--mode") {
            std::string v; if (!cur.nextStr(flag, v)) break;
            if (v == "rotate") cfg.mode = AnimMode::Rotate;
            else if (v == "zoom") cfg.mode = AnimMode::Zoom;
            else if (v == "cycle") cfg.mode = AnimMode::Cycle;
            else if (v == "spin") cfg.mode = AnimMode::Spin;
            else { fail("--mode must be rotate, zoom, cycle, or spin; got '" + v + "'"); break; }
        }
        else if (flag == "-d" || flag == "--duration") {
            if (!cur.nextDouble(flag, cfg.duration)) break;
            if (cfg.duration <= 0) { fail("--duration must be positive"); break; }
        }
        else if (flag == "--fps") {
            if (!cur.nextInt(flag, cfg.fps)) break;
            if (cfg.fps < 1) { fail("--fps must be >= 1"); break; }
        }
        else if (flag == "--crf") { if (!cur.nextInt(flag, cfg.crf)) break; }
        else if (flag == "--color-cycles")  { if (!cur.nextDouble(flag, cfg.color_cycles)) break; }
        else if (flag == "--rotate-radius") { if (!cur.nextDouble(flag, cfg.rotate_radius)) break; }
        else if (flag == "--zoom-end")      { if (!cur.nextDouble(flag, cfg.zoom_end)) break; }
        else if (flag == "--zoom-target-x") { if (!cur.nextDouble(flag, cfg.zoom_target_x)) break; }
        else if (flag == "--zoom-target-y") { if (!cur.nextDouble(flag, cfg.zoom_target_y)) break; }
        else { fail("unknown option '" + flag + "'"); break; }
    }

    if (!cur.error.empty() && out.error.empty()) out.error = cur.error;
    if (!out.error.empty()) return out;
    if (out.kind == CommandKind::Help) return out;

    // The center solver doesn't render, so it skips palette/output defaults. It
    // reuses --scale as the search radius around the guess; pick a sane default
    // (the whole-set scale is far too wide and would just return period 1).
    if (out.kind == CommandKind::Center) {
        out.render = static_cast<RenderConfig>(cfg);
        if (!scale_set) out.render.scale = 0.01;
        out.max_period = max_period;
        out.find_period = find_period;
        out.misiurewicz = misiurewicz;
        out.max_preperiod = max_preperiod;
        return out;
    }

    // Resolve palette.
    if (!parsePalette(palette_spec, cfg.palette)) {
        out.error = "invalid palette '" + palette_spec +
                    "' (use a built-in name or >=2 comma-separated hex colors)";
        return out;
    }

    // Per-command output default.
    if (!output_set) cfg.output = (out.kind == CommandKind::Video) ? "fractal.mp4" : "fractal.png";

    // Video renders hundreds of frames; default to lighter supersampling.
    if (out.kind == CommandKind::Video && !ssaa_set) cfg.ssaa = 2;

    // Cycle-mode video sweeps the palette phase, which only loops seamlessly
    // with a closed gradient. The same goes for an overlaid --color-cycles sweep.
    if (out.kind == CommandKind::Video &&
        (cfg.mode == AnimMode::Cycle || cfg.color_cycles != 0.0)) cfg.cyclic = true;

    // The explorer redraws on every interaction, so default it to a responsive
    // window size and light supersampling unless the user asked otherwise.
    if (out.kind == CommandKind::Explore) {
        if (!size_set) { cfg.width = 900; cfg.height = 900; }
        if (!ssaa_set) cfg.ssaa = 2;
    }

    if (out.kind == CommandKind::Video) {
        out.video = cfg;
    } else {
        out.render = static_cast<RenderConfig>(cfg); // Render / Buddha / Explore
    }
    return out;
}

} // namespace fractal

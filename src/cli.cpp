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
    else if (name == "candy-swirl")    julia(-0.7269,   0.1889,   1.10, 3000, "candy");
    else if (name == "ember-seahorse") mandel(-0.74364388703, 0.13182590421, 1.35 / 350.0, 2000, "ember");
    else if (name == "inferno-valley") mandel(-0.748, 0.1, 1.35 / 11.0, 2500, "inferno");
    // Presets built on popular color themes.
    else if (name == "synthwave")      julia(-0.512511, 0.521295, 1.30, 2000, "synthwave");
    else if (name == "nord")           julia(-0.74543,  0.11301,  1.25, 3000, "nord");
    else if (name == "dracula")        julia(-0.7269,   0.1889,   1.10, 3000, "dracula");
    else if (name == "gruvbox")        mandel(-0.74364388703, 0.13182590421, 1.35 / 350.0, 2000, "gruvbox");
    else if (name == "autumn")         julia(-0.8,      0.156,    1.30, 2500, "autumn");
    else if (name == "rosegold")       julia(-0.512511, 0.521295, 1.30, 2000, "rosegold");
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
  fractal help                  Show this help.

COMMON OPTIONS
  -P, --preset <name>             Start from a curated preset (see PRESETS)
  -t, --type <julia|mandelbrot>   Fractal family            (default: julia)
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

  ALBUM-ART / DESIGN LAYER (all off at 0; see the cover-* presets)
      --kaleido <float>           N mirrored wedges -> mandala (default: 0/off)
      --kaleido-angle <float>     Rotate the symmetry, degrees (default: 0)
      --aberration <float>        Chromatic RGB split, pixels (default: 0)
      --vignette <float>          Darken corners 0..1         (default: 0)
      --grain <float>             Film-grain noise amount     (default: 0)
      --scanlines <float>         CRT scanline depth 0..1     (default: 0)
  -o, --output <path>             Output file

VIDEO OPTIONS
      --mode <rotate|zoom|cycle>  Animation type            (default: rotate)
  -d, --duration <float>          Seconds                   (default: 20)
      --fps <int>                 Frames per second         (default: 30)
      --crf <int>                 x264 quality, lower=better(default: 18)
      --rotate-radius <float>     |c| for rotate mode       (default: 0.7885)
      --zoom-end <float>          End scale for zoom mode    (default: 0.005)
      --zoom-target-x <float>     Zoom target x             (default: 0)
      --zoom-target-y <float>     Zoom target y             (default: 0)

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
    else { out.error = "unknown command '" + cmd + "' (expected 'render' or 'video')"; return out; }

    // Parse everything into a VideoConfig; the Render path uses the base slice.
    VideoConfig cfg;
    std::string palette_spec = "noir";
    bool output_set = false;
    bool ssaa_set = false;

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
        else if (flag == "--cre") { if (!cur.nextDouble(flag, cfg.julia_cre)) break; }
        else if (flag == "--cim") { if (!cur.nextDouble(flag, cfg.julia_cim)) break; }
        else if (flag == "--center-x" || flag == "--cx") { if (!cur.nextDouble(flag, cfg.center_x)) break; }
        else if (flag == "--center-y" || flag == "--cy") { if (!cur.nextDouble(flag, cfg.center_y)) break; }
        else if (flag == "--scale") { if (!cur.nextDouble(flag, cfg.scale)) break; }
        else if (flag == "--zoom") {
            double z; if (!cur.nextDouble(flag, z)) break;
            if (z <= 0) { fail("--zoom must be positive"); break; }
            cfg.scale = 1.35 / z;
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
        }
        else if (flag == "--height") {
            if (!cur.nextInt(flag, cfg.height)) break;
            if (cfg.height < 1) { fail("--height must be >= 1"); break; }
        }
        else if (flag == "--size") {
            std::string v; if (!cur.nextStr(flag, v)) break;
            if (!parseSize(v, cfg.width, cfg.height)) { fail("--size must be WxH, e.g. 1920x1080"); break; }
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
        // ---- album-art / design layer ----
        else if (flag == "--kaleido")    { if (!cur.nextDouble(flag, cfg.kaleido)) break; }
        else if (flag == "--kaleido-angle"){ if (!cur.nextDouble(flag, cfg.kaleido_angle)) break; }
        else if (flag == "--aberration") { if (!cur.nextDouble(flag, cfg.aberration)) break; }
        else if (flag == "--vignette")   { if (!cur.nextDouble(flag, cfg.vignette)) break; }
        else if (flag == "--grain")      { if (!cur.nextDouble(flag, cfg.grain)) break; }
        else if (flag == "--scanlines")  { if (!cur.nextDouble(flag, cfg.scanlines)) break; }
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
            else { fail("--mode must be rotate, zoom, or cycle; got '" + v + "'"); break; }
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
        else if (flag == "--rotate-radius") { if (!cur.nextDouble(flag, cfg.rotate_radius)) break; }
        else if (flag == "--zoom-end")      { if (!cur.nextDouble(flag, cfg.zoom_end)) break; }
        else if (flag == "--zoom-target-x") { if (!cur.nextDouble(flag, cfg.zoom_target_x)) break; }
        else if (flag == "--zoom-target-y") { if (!cur.nextDouble(flag, cfg.zoom_target_y)) break; }
        else { fail("unknown option '" + flag + "'"); break; }
    }

    if (!cur.error.empty() && out.error.empty()) out.error = cur.error;
    if (!out.error.empty()) return out;
    if (out.kind == CommandKind::Help) return out;

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
    // with a closed gradient.
    if (out.kind == CommandKind::Video && cfg.mode == AnimMode::Cycle) cfg.cyclic = true;

    if (out.kind == CommandKind::Render) {
        out.render = static_cast<RenderConfig>(cfg); // slice base
    } else {
        out.video = cfg;
    }
    return out;
}

} // namespace fractal

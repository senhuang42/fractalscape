#include "palette.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>

namespace fractal {

namespace {

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(c));
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

// Built-in palettes. Each is a *designed gradient* that ramps dark -> a
// harmonious hue family -> bright, and loops cleanly (the cyclic flag closes
// it). That is the key to coherent images: stripe/iteration detail becomes
// light/dark texture within related hues, with dark "valleys" for negative
// space — instead of the rainbow noise a full-spectrum hue wheel produces.
// Several are the matplotlib perceptually-uniform colormaps, which are
// engineered to stay coherent no matter how the scalar field varies.
const std::map<std::string, std::vector<std::string>>& namedPalettes() {
    static const std::map<std::string, std::vector<std::string>> kPalettes = {
        // The default: warm coherent "sunset" — deep indigo -> plum ->
        // rose -> coral -> warm gold. Harmonious (no rainbow), like ref. art.
        {"aurora",      {"#070313", "#2c1250", "#8e2f6e", "#e85c5c", "#ffb14e", "#ffe6a8"}},
        // Cool steel blue -> white on black (matches the blue spiral reference).
        {"frost",       {"#00040c", "#082047", "#2f6fb0", "#7fbfe6", "#eaf7ff", "#ffffff"}},
        // matplotlib magma — perceptually uniform, black -> purple -> orange.
        {"magma",       {"#000004", "#3b0f70", "#8c2981", "#de4968", "#fe9f6d", "#fcfdbf"}},
        // matplotlib viridis — perceptually uniform, indigo -> teal -> yellow.
        {"viridis",     {"#440154", "#414487", "#2a788e", "#22a884", "#7ad151", "#fde725"}},
        // Warm volcanic.
        // Top stop is a warm gold, not near-white (#fff1c1): the slow-escape
        // spiral cores land on the brightest stop, and a desaturated near-white
        // there reads as a grey blob. Warm gold keeps the cores hot AND warm.
        {"ember",       {"#0a0200", "#3d0c02", "#9a1f0b", "#e8540c", "#ffae42", "#ffdc8f"}},
        // Cool glacial blues and whites.
        {"ice",         {"#01030f", "#0a2a5e", "#1f6fb2", "#56c4e8", "#b8f0ff", "#ffffff"}},
        // Classic flame.
        {"fire",        {"#000000", "#420a00", "#a01a00", "#ff4d00", "#ffb000", "#ffffff"}},
        // Grayscale relief (the default) — black void, grey fur, white cores.
        // Faithful to the reference SAC article's monochrome 3D look.
        {"noir",        {"#000000", "#101010", "#454545", "#9a9a9a", "#f2f2f2", "#ffffff"}},
        // Soft pastel pinks/golds on black (matches reference image 2).
        {"bloom",       {"#000000", "#5e2750", "#c8638f", "#f6b8c4", "#f0d8a8", "#cf9b6b", "#dff0d0"}},
        // matplotlib inferno / plasma / cividis — perceptually uniform.
        {"inferno",     {"#000004", "#420a68", "#932667", "#dd513a", "#fca50a", "#fcffa4"}},
        {"plasma",      {"#0d0887", "#6a00a8", "#b12a90", "#e16462", "#fca636", "#f0f921"}},
        {"cividis",     {"#00204d", "#31446b", "#666970", "#958f78", "#cbba69", "#ffea46"}},
        // Wider-gamut designed themes (span more of the spectrum, still tasteful).
        {"sunset",      {"#1a0633", "#5e1a6b", "#c02a6e", "#ff6b4a", "#ffb45e", "#ffe9a8"}},
        {"ocean",       {"#001028", "#003a5c", "#1f7a8c", "#52d6c4", "#bdf0e0", "#ffffff"}},
        {"neon",        {"#050008", "#ff0080", "#7a00ff", "#00e5ff", "#00ff95", "#faff00"}},
        // Fuchsia-forward neon (Miami-Vice synthwave): magenta/pink dominate,
        // a violet bridge, one cyan pop at the top -- no green/yellow. Built for
        // album art; pairs with the cover-* presets.
        {"vice",        {"#070010", "#5e0048", "#b80077", "#ff1a8d", "#ff66c4", "#c44bff", "#36e6ff"}},
        // Neon structure (pink/purple/cyan) bracketed by a BROAD dark band, so
        // deep-zoom "lakes" (slow-escape regions, which land at the top of the
        // ramp) read dark instead of the bright olive plain neon gives them. The
        // dark ends loop cleanly, so when cycled the background breathes through
        // black -> jewel tones rather than smearing. Built for deep zooms.
        {"neon-dark",   {"#02010a", "#150026", "#ff1a8d", "#8a50e0", "#00e5ff", "#071030"}},
        // Pink + cyan, saturated rather than pastel: the old version was three
        // light tints in a row, which washed out. Near-black anchor + one light
        // stop keeps the contrast.
        {"candy",       {"#0c0420", "#a3005f", "#ff2e88", "#ff77b0", "#00b4e6", "#7af0ff"}},
        {"gold",        {"#0a0600", "#3d2600", "#8a5a00", "#d49a1a", "#ffd86b", "#fff4cf"}},
        {"emerald",     {"#001a10", "#00402a", "#0a7a50", "#3fd089", "#b8f0c8", "#ffffff"}},
        {"vapor",       {"#1a0b2e", "#7b2ff7", "#f72585", "#4cc9f0", "#80ffea", "#ffffff"}},
        // Teal <-> fuchsia: a near-complementary journey (teal -> cyan -> blue
        // -> violet -> fuchsia) with a periwinkle bridge so the mid transition
        // stays colorful, not muddy. Saturated stops from a near-black anchor =
        // high contrast, so the subtle interior relief still reads. (Earlier I
        // muted this into pastels and it washed the interiors flat -- keep the
        // contrast; this version just dials the peak brightness down a touch.)
        {"prism",       {"#05021a", "#06525a", "#0eb8aa", "#4a9ae6", "#8a50e0", "#e62aa3", "#ecb0db"}},
        // Popular design color schemes, arranged as dark->bright ramps.
        // Ends on a saturated gold, not near-white cream (which washed the
        // highlights), with a darker anchor.
        {"synthwave",   {"#0c0020", "#2a0a52", "#b5179e", "#ff2a6d", "#ff6b3d", "#ffc24a"}},
        {"nord",        {"#11131a", "#2e3440", "#434c5e", "#5e81ac", "#88c0d0", "#eceff4"}},
        // Near-black anchor and a saturated purple mid instead of the muted
        // slate-grey, so it stops reading as flat lavender.
        {"dracula",     {"#0b0b12", "#3a2d5c", "#bd93f9", "#ff79c6", "#8be9fd", "#f1fa8c"}},
        // Anchored at near-black (not gruvbox's grey #1d2021) so the exterior
        // and deep relief stay dark -> full contrast instead of a washed haze.
        {"gruvbox",     {"#0d0b08", "#3c1a0a", "#cc241d", "#d65d0e", "#d79921", "#fbf1c7"}},
        {"autumn",      {"#1a0e08", "#4a1c10", "#8a3324", "#c1440e", "#e08e0b", "#f7d08a"}},
        // Orange-flame-red dominant FIELD with electric-blue SPIRAL ARMS, the
        // way the neon palette gives cyan field + magenta spirals. The trick is
        // a warm-cool-warm layout: cool stops (violet, blue) sit at palette
        // position ~0.4 -- where the stripe (SAC) layer samples for spiral arms
        // and high-iter structure -- while warm stops (vermillion, orange) sit
        // at ~0.6-0.85, where the iter layer samples for the bulk field. So
        // field and structure get visibly DIFFERENT colors instead of grading
        // through the same hue. Cool break placement matters more than gradient
        // smoothness here -- the warm->cool->warm jump shows mostly as a halo
        // around structures, not as a banded gradient. Yellow tip at the top
        // for the hottest cores. Built for the "burn" album.
        {"burn",        {"#02030f", "#5e0a08", "#c4180a", "#5a48d6", "#4090ff", "#ff5c14", "#ff9a2e", "#ffb83a", "#ffd84a"}},
        // Deeper anchor and more saturated rose mids than the old dusty version,
        // which read as low-contrast.
        {"rosegold",    {"#150509", "#4a142a", "#962f56", "#d4486f", "#ef8e8a", "#fbd9c4"}},
        {"galaxy",      {"#05010f", "#1a1248", "#4b2a9e", "#8e44ad", "#c06ff2", "#ffe6ff"}},
        {"mint",        {"#04140f", "#0a3d2e", "#138a5e", "#3fd089", "#9af0c8", "#eafff5"}},
        // Saturated rainbow on near-black — the deliberately vibrant option.
        {"psychedelic", {"#000010", "#ff006e", "#fb5607", "#ffbe0b", "#8ac926", "#3a86ff", "#8338ec"}},
        // Monochrome grayscale.
        {"mono",        {"#000000", "#ffffff"}},
        // Maths Town-style restrained palettes: smooth dark->light single-hue
        // ramps. Pair with low --color-density (~0.005-0.015) and heavy
        // --slopes / --shading for the embossed monochromatic look his videos
        // are known for. Each pairs with a distinct inside_color "spiral eye"
        // accent (set per preset, not in the palette).
        {"tealfog",     {"#0a1820", "#1a3340", "#3a6878", "#7ab0c0", "#dbeff5", "#f5fbfd"}},
        {"sepia",       {"#15100a", "#2a1f10", "#5a4a2c", "#a89878", "#e8dcc0", "#fcf5e8"}},
        {"chrome",      {"#080810", "#1a1a22", "#404048", "#909098", "#e0e0e8", "#fafaff"}},
        {"honeycomb",   {"#1a0a02", "#3a1f08", "#a86820", "#e8a838", "#f8e088", "#fff5d0"}},
        // High-voltage cyan/magenta on black with a white peak. Built for the
        // cyberpunk preset; pairs well with --posterize 4 for a Tron-grid look.
        {"electric",    {"#000010", "#0040c4", "#00e0ff", "#ff20cc", "#ffffff"}},
        // Electric green ramp. Designed as a tropical-stripe partner for vice:
        // hot fuchsia field + electric lime spiral arms is a high-saturation
        // complementary pop you cannot get from a single ramp.
        {"lime",        {"#000005", "#003a14", "#22a830", "#80ff20", "#e0ff80"}},
        // Risograph print palette: federal blue + fluorescent pink + sunflower
        // yellow on cream/black -- the canonical 3-color riso aesthetic. Looks
        // best with --posterize 4 or 5 so it reads as actual ink layers.
        {"riso",        {"#0a0a14", "#0050ff", "#ff3399", "#ffd000", "#fafafa"}},
        // Mid-century atomic: dark teal grounding, burnt-orange + mustard
        // mids, cream highlight. Reads as Eames / "Mad Men" with --posterize 5.
        {"atomic",      {"#0a1818", "#1a4a4a", "#d8782a", "#f0c850", "#f5f0d8"}},
        // Saturated jewel-tones for posterization / stained-glass renders. Each
        // stop is a distinct gem hue, designed to read as DISTINCT BLOCKS rather
        // than a gradient when quantized: dark anchor, sapphire, emerald, ruby,
        // citrine, amethyst. Pairs well with --posterize 6..8 + Oklab interp.
        {"jewel",       {"#0a0612", "#1c4ec0", "#0fa05a", "#d62a2e", "#ffc846", "#7a32c8"}},
        // Cool counterpart to `ember`, for warm/cool dual-palette pairings:
        // pass ember as the main palette and arctic as --stripe-palette (or vice
        // versa) for a warm-field, cool-structure (or inverse) composition.
        {"arctic",      {"#04060a", "#0a2848", "#1e5e96", "#54aedc", "#a8e0f0", "#e8f8ff"}},
    };
    return kPalettes;
}

} // namespace

bool parseHexColor(const std::string& in, Color& out) {
    std::string s = in;
    if (!s.empty() && s[0] == '#') s = s.substr(1);

    auto fromByte = [](int v) { return static_cast<float>(v) / 255.0f; };

    if (s.size() == 6) {
        int r1 = hexNibble(s[0]), r0 = hexNibble(s[1]);
        int g1 = hexNibble(s[2]), g0 = hexNibble(s[3]);
        int b1 = hexNibble(s[4]), b0 = hexNibble(s[5]);
        if ((r1|r0|g1|g0|b1|b0) < 0) return false;
        out = Color{fromByte(r1*16+r0), fromByte(g1*16+g0), fromByte(b1*16+b0)};
        return true;
    }
    if (s.size() == 3) {
        int r = hexNibble(s[0]), g = hexNibble(s[1]), b = hexNibble(s[2]);
        if ((r|g|b) < 0) return false;
        out = Color{fromByte(r*17), fromByte(g*17), fromByte(b*17)};
        return true;
    }
    return false;
}

bool parsePalette(const std::string& spec, std::vector<Color>& out) {
    out.clear();

    // Named palette?
    const auto& named = namedPalettes();
    auto it = named.find(spec);
    if (it != named.end()) {
        for (const auto& hex : it->second) {
            Color c;
            if (!parseHexColor(hex, c)) return false; // built-ins are always valid
            out.push_back(c);
        }
        return true;
    }

    // Otherwise treat as comma-separated hex list.
    std::stringstream ss(spec);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace.
        size_t a = token.find_first_not_of(" \t");
        size_t b = token.find_last_not_of(" \t");
        if (a == std::string::npos) continue;
        token = token.substr(a, b - a + 1);
        Color c;
        if (!parseHexColor(token, c)) return false;
        out.push_back(c);
    }
    return out.size() >= 2; // need at least two stops to form a gradient
}

std::vector<std::string> builtinPaletteNames() {
    std::vector<std::string> names;
    for (const auto& kv : namedPalettes()) names.push_back(kv.first);
    return names;
}

// --- Oklab color-space conversion ---------------------------------------
// Bjorn Ottosson's Oklab (https://bottosson.github.io/posts/oklab/). It is a
// perceptually-uniform LMS-based color space: interpolating between two colors
// in Oklab keeps the midpoint hues saturated instead of dipping through grey,
// which is what makes red->blue cross via clean magenta instead of mud.
//
// Pipeline used here: sRGB byte/float (the stored stops) -> linear-light sRGB
// (gamma decode, gamma 2.2 approximation is intentionally avoided since 2.4
// piecewise is what photographs use) -> Oklab -> lerp -> linear sRGB ->
// gamma-encoded sRGB -> byte. Done in doubles throughout; the math is cheap.

static double srgb_to_linear(double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
static double linear_to_srgb(double c) {
    return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

struct OkLab { double L, a, b; };

static OkLab linearRgbToOklab(double r, double g, double b) {
    double l = 0.4122214708*r + 0.5363325363*g + 0.0514459929*b;
    double m = 0.2119034982*r + 0.6806995451*g + 0.1073969566*b;
    double s = 0.0883024619*r + 0.2817188376*g + 0.6299787005*b;
    double l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);
    return OkLab{
        0.2104542553*l_ + 0.7936177850*m_ - 0.0040720468*s_,
        1.9779984951*l_ - 2.4285922050*m_ + 0.4505937099*s_,
        0.0259040371*l_ + 0.7827717662*m_ - 0.8086757660*s_};
}
static void oklabToLinearRgb(const OkLab& v, double& r, double& g, double& b) {
    double l_ = v.L + 0.3963377774*v.a + 0.2158037573*v.b;
    double m_ = v.L - 0.1055613458*v.a - 0.0638541728*v.b;
    double s_ = v.L - 0.0894841775*v.a - 1.2914855480*v.b;
    double l = l_*l_*l_, m = m_*m_*m_, s = s_*s_*s_;
    r =  4.0767416621*l - 3.3077115913*m + 0.2309699292*s;
    g = -1.2684380046*l + 2.6097574011*m - 0.3413193965*s;
    b = -0.0041960863*l - 0.7034186147*m + 1.7076147010*s;
}

std::vector<uint8_t> buildGradient(const std::vector<Color>& stops,
                                   int resolution, bool cyclic,
                                   InterpMode interp, int posterize) {
    std::vector<uint8_t> data;
    if (stops.empty() || resolution <= 0) return data;

    // Working list of stops; when cyclic, append the first stop to close it.
    std::vector<Color> s = stops;
    if (cyclic && s.size() >= 1) s.push_back(s.front());

    data.resize(static_cast<size_t>(resolution) * 3);

    const int segments = static_cast<int>(s.size()) - 1;
    if (segments <= 0) {
        // Single color fill.
        for (int i = 0; i < resolution; ++i) {
            data[i*3+0] = static_cast<uint8_t>(std::lround(std::clamp(s[0].r,0.0f,1.0f) * 255.0f));
            data[i*3+1] = static_cast<uint8_t>(std::lround(std::clamp(s[0].g,0.0f,1.0f) * 255.0f));
            data[i*3+2] = static_cast<uint8_t>(std::lround(std::clamp(s[0].b,0.0f,1.0f) * 255.0f));
        }
        return data;
    }

    // For Oklab interp, pre-convert each stop. For Rgb, we lerp the sRGB values
    // directly (faster, and what the original behavior was).
    std::vector<OkLab> stopsLab;
    if (interp == InterpMode::Oklab) {
        stopsLab.reserve(s.size());
        for (const auto& c : s) {
            double r = srgb_to_linear(std::clamp((double)c.r, 0.0, 1.0));
            double g = srgb_to_linear(std::clamp((double)c.g, 0.0, 1.0));
            double b = srgb_to_linear(std::clamp((double)c.b, 0.0, 1.0));
            stopsLab.push_back(linearRgbToOklab(r, g, b));
        }
    }

    // Clamp posterize to 0 (off) or [2, 1024]. <2 has no useful effect.
    const int post = (posterize >= 2) ? std::min(posterize, 1024) : 0;

    for (int i = 0; i < resolution; ++i) {
        // Position in [0, 1] across the gradient, optionally posterized.
        double u = (resolution == 1) ? 0.0
                 : static_cast<double>(i) / (resolution - 1);
        if (post > 0) {
            // Snap to N bands. Clamp the band index to [0, N-1] BEFORE adding
            // the +0.5 center offset, so the u=1.0 edge doesn't fall into a
            // bogus Nth band that just rounds to "the last color." Result is
            // exactly N distinct output colors, sampled at each band's center.
            double band = std::floor(u * post);
            if (band > post - 1) band = post - 1;
            u = (band + 0.5) / post;
        }
        double t = u * segments;
        int idx = std::min(static_cast<int>(t), segments - 1);
        double f = t - idx;

        double r, g, bl;
        if (interp == InterpMode::Oklab) {
            const OkLab& A = stopsLab[idx];
            const OkLab& B = stopsLab[idx + 1];
            OkLab mid{A.L + (B.L - A.L) * f,
                      A.a + (B.a - A.a) * f,
                      A.b + (B.b - A.b) * f};
            double lr, lg, lb;
            oklabToLinearRgb(mid, lr, lg, lb);
            // Out-of-gamut Oklab points can produce negative linear RGB; clamp
            // before encoding so we don't roll over to bogus sRGB values.
            r  = linear_to_srgb(std::clamp(lr, 0.0, 1.0));
            g  = linear_to_srgb(std::clamp(lg, 0.0, 1.0));
            bl = linear_to_srgb(std::clamp(lb, 0.0, 1.0));
        } else {
            const Color& a = s[idx];
            const Color& b = s[idx + 1];
            r  = a.r + (b.r - a.r) * f;
            g  = a.g + (b.g - a.g) * f;
            bl = a.b + (b.b - a.b) * f;
        }
        data[i*3+0] = static_cast<uint8_t>(std::lround(std::clamp(r,  0.0, 1.0) * 255.0));
        data[i*3+1] = static_cast<uint8_t>(std::lround(std::clamp(g,  0.0, 1.0) * 255.0));
        data[i*3+2] = static_cast<uint8_t>(std::lround(std::clamp(bl, 0.0, 1.0) * 255.0));
    }
    return data;
}

} // namespace fractal

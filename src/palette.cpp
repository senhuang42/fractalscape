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
        {"candy",       {"#190a2e", "#ff5fa2", "#ff9ec7", "#ffd6e8", "#a0e7ff", "#7afcff"}},
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
        {"synthwave",   {"#100024", "#3b0f6f", "#b5179e", "#ff2a6d", "#ff9e64", "#fff3b0"}},
        {"nord",        {"#11131a", "#2e3440", "#434c5e", "#5e81ac", "#88c0d0", "#eceff4"}},
        {"dracula",     {"#15161e", "#282a36", "#6272a4", "#bd93f9", "#ff79c6", "#f1fa8c"}},
        // Anchored at near-black (not gruvbox's grey #1d2021) so the exterior
        // and deep relief stay dark -> full contrast instead of a washed haze.
        {"gruvbox",     {"#0d0b08", "#3c1a0a", "#cc241d", "#d65d0e", "#d79921", "#fbf1c7"}},
        {"autumn",      {"#1a0e08", "#4a1c10", "#8a3324", "#c1440e", "#e08e0b", "#f7d08a"}},
        {"rosegold",    {"#1a0d12", "#4a1f2e", "#8a3a52", "#c76b7e", "#e8a9a0", "#f7e6dd"}},
        {"galaxy",      {"#05010f", "#1a1248", "#4b2a9e", "#8e44ad", "#c06ff2", "#ffe6ff"}},
        {"mint",        {"#04140f", "#0a3d2e", "#138a5e", "#3fd089", "#9af0c8", "#eafff5"}},
        // Saturated rainbow on near-black — the deliberately vibrant option.
        {"psychedelic", {"#000010", "#ff006e", "#fb5607", "#ffbe0b", "#8ac926", "#3a86ff", "#8338ec"}},
        // Monochrome grayscale.
        {"mono",        {"#000000", "#ffffff"}},
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

std::vector<uint8_t> buildGradient(const std::vector<Color>& stops,
                                   int resolution, bool cyclic) {
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

    for (int i = 0; i < resolution; ++i) {
        // Position in [0, segments].
        double t = (resolution == 1) ? 0.0
                 : (static_cast<double>(i) / (resolution - 1)) * segments;
        int idx = std::min(static_cast<int>(t), segments - 1);
        double f = t - idx;
        const Color& a = s[idx];
        const Color& b = s[idx + 1];
        double r = a.r + (b.r - a.r) * f;
        double g = a.g + (b.g - a.g) * f;
        double bl = a.b + (b.b - a.b) * f;
        data[i*3+0] = static_cast<uint8_t>(std::lround(std::clamp(r,  0.0, 1.0) * 255.0));
        data[i*3+1] = static_cast<uint8_t>(std::lround(std::clamp(g,  0.0, 1.0) * 255.0));
        data[i*3+2] = static_cast<uint8_t>(std::lround(std::clamp(bl, 0.0, 1.0) * 255.0));
    }
    return data;
}

} // namespace fractal

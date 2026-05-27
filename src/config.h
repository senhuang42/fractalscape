// config.h — parameters describing a single fractal render or an animation.
//
// Everything here is plain data with no OpenGL dependency so it can be
// constructed and inspected in unit tests.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace fractal {

// Which family to iterate.
enum class FractalType {
    Mandelbrot, // c = pixel,        z0 = 0
    Julia,      // c = fixed const,  z0 = pixel
};

// The iteration formula, orthogonal to FractalType (which only decides whether
// the pixel is c or z0). Quadratic/BurningShip/Tricorn/Phoenix are escape-time
// and use the normal SAC coloring; Newton is convergence-based (root basins)
// with its own coloring path. Integer values must match uFormula in the shader
// and Formula in fractal_math.h.
enum class Formula {
    Quadratic = 0,   // z -> z^2 + c
    BurningShip = 1, // z -> (|Re z| + i|Im z|)^2 + c
    Tricorn = 2,     // z -> conj(z)^2 + c
    Phoenix = 3,     // z -> z^2 + c + p*z_prev
    Newton = 4,      // z -> z - (z^3 - 1)/(3 z^2), colored by which root it reaches
};

// How an animation evolves over its duration. All modes are designed so that
// frame 0 and the final frame line up, giving a seamless loop.
enum class AnimMode {
    Rotate, // Julia constant orbits the origin: c = r * e^(i*theta), theta 0..2pi
    Zoom,   // smooth zoom from start scale to end scale toward a target point
    Cycle,  // static fractal, palette phase sweeps a full cycle
    Spin,   // kaleidoscope rotates by one symmetry segment (seamless mandala loop)
};

// An RGB color in linear-ish [0,1] sRGB space.
struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    bool operator==(const Color& o) const {
        return r == o.r && g == o.g && b == o.b;
    }
};

// A full description of one still image. The animation layer mutates a copy of
// this per frame (center, scale, julia_c, color_offset).
struct RenderConfig {
    FractalType type = FractalType::Julia;
    Formula     formula = Formula::Quadratic;
    // Phoenix parameter p (the z_prev coefficient); only used when formula is
    // Phoenix. Classic Phoenix uses a real p (e.g. -0.5) with c real.
    double      phoenix_pre = -0.5;
    double      phoenix_pim = 0.0;

    // View into the complex plane. `scale` is half the vertical extent in
    // complex units, so smaller scale == deeper zoom.
    double center_x = 0.0;
    double center_y = 0.0;
    double scale    = 1.35;

    // Julia constant (ignored for Mandelbrot). This default gives bold
    // double spirals reminiscent of the reference art.
    double julia_cre = -0.512511;
    double julia_cim = 0.521295;

    // Iteration controls.
    int    max_iter   = 400;
    double exponent   = 2.0;     // z^exponent + c
    double bailout    = 10000.0; // escape radius; large for smooth SAC/iteration

    // Deep zoom: iterate in emulated double-float (df64) precision instead of
    // 32-bit float, which pushes the pixelation limit from ~1e4 to ~1e13. Uses
    // the quadratic SAC path only (no formula variants / lighting / kaleido).
    bool   deep = false;

    // Output image size in pixels (pre-supersampling).
    int width  = 1600;
    int height = 1600;
    int ssaa   = 4; // supersampling factor per axis (1 = off) -> 16 samples/px

    // Coloring.
    std::vector<Color> palette;        // gradient stops; filled from name/spec
    // Built-in palettes are dark->bright ramps, so by default the gradient is
    // NOT looped: low stripe values map to the dark end (negative space) and
    // high values to the bright end. (cycle-mode video re-enables looping.)
    bool   cyclic        = false;      // close the gradient loop?
    // Iteration layer ramp: fast-escape exterior -> dark, slow-escape
    // filaments -> bright (coord = 1 - exp(-mu * color_density)). This is the
    // layer that draws the dendrite tendrils and the dark negative space.
    // 0 disables it -> stripe layer renders alone.
    double color_density = 0.035;      // iteration ramp steepness
    double color_offset  = 0.0;        // palette phase shift [0,1)
    // Stripe Average Coloring (Haerkoenen 2007) — the relief texture, laid over
    // the iteration layer with a hard-light overlay. 0 disables it -> iteration
    // layer renders alone; 1 = full overlay.
    double stripe_color    = 1.0;      // hard-light overlay weight
    double stripe_freq     = 6.0;      // stripe density (integer 4/6/8 best)
    double stripe_contrast = 2.2;      // stretch around mid to use full gradient
    // Escape-angle blend. Adds hue grain near the boundary but turns into
    // chaotic speckle deep in the body, so it's off by default.
    double angle_color   = 0.0;        // 0 = no angle contribution
    // Orbit-trap coloring: min distance the orbit comes to (trap_x, trap_y).
    // Off by default (SAC supersedes it); the raw min can seam, so prefer SAC.
    double trap_color    = 0.0;        // 0 = no orbit-trap contribution
    double trap_x        = 0.0;
    double trap_y        = 0.0;
    Color  inside_color  = {0,0,0};    // color for points in the set
    double saturation    = 1.3;        // final grade
    double gamma         = 1.05;       // final grade (1 = none)
    double black_point   = 0.08;       // crush near-blacks so empties are black

    // Blinn-Phong lighting of the relief as a height field (diffuse + specular).
    // Off by default (it can darken the bright relief), but it's a proper
    // height-field light when you want a polished, lit sheen — try
    // `--shading 0.3 --specular 0.6`.
    double shading      = 0.0;   // diffuse strength (0 = flat)
    double light_angle  = 135.0; // light azimuth, degrees
    double light_height = 1.0;   // light elevation (z of light vector)
    double specular     = 0.0;   // specular highlight strength (0 = none)
    double shininess    = 18.0;  // specular exponent (higher = tighter)
    double height_scale = 1.5;   // relief -> surface-normal tilt
    // Bloom: blur the bright areas and screen-blend them back for a luminous
    // glow. `bloom` is the strength (0 = off); `bloom_threshold` is how bright
    // a pixel must be to bloom.
    double bloom           = 0.3;
    double bloom_threshold = 0.5;

    // --- Album-art / design layer (all off by default) ---
    // Kaleidoscope: fold the view into N mirrored wedges around the image
    // center before the fractal math, for a radially-symmetric mandala. 0 = off
    // (any value < 2 is treated as off). Fractional values are fine.
    double kaleido       = 0.0;
    double kaleido_angle = 0.0;  // rotate the symmetry, degrees
    // Chromatic aberration: split RGB along the radius in the final pass, so
    // bright edges fringe into neon. Value is the max channel offset in pixels.
    double aberration    = 0.0;
    // Vignette: darken toward the corners to seat the focal point. 0..1.
    double vignette      = 0.0;
    // Film grain: additive noise so flat areas read as texture, not plastic.
    double grain         = 0.0;
    // Scanlines: horizontal CRT lines for a glitch/cyber look. 0..1.
    double scanlines     = 0.0;

    // `glow` lights the thin filaments using a distance estimate.
    double glow         = 0.0;   // 0 = off
    // `falloff` fades the exterior toward inside_color by distance to the set.
    // Off by default now: the iteration layer already supplies the black
    // negative space (and does it with real tendrils, not a smooth fade).
    double falloff      = 0.0;

    std::string output = "fractal.png";
};

// Extends RenderConfig with timeline controls for `video`.
struct VideoConfig : RenderConfig {
    AnimMode mode     = AnimMode::Rotate;
    double   duration = 20.0; // seconds
    int      fps      = 30;
    int      crf      = 18;   // x264 quality (lower = better)

    // Rotate mode: |c| stays fixed at this radius.
    double rotate_radius = 0.7885;
    // Zoom mode: end scale and target point.
    double zoom_end      = 0.005;
    double zoom_target_x = 0.0;
    double zoom_target_y = 0.0;

    int total_frames() const { return static_cast<int>(duration * fps + 0.5); }
};

} // namespace fractal

// buddhabrot.h — CPU Buddhabrot / Nebulabrot renderer.
//
// Unlike escape-time coloring (which asks "where did this pixel land?"), the
// Buddhabrot asks "which pixels did escaping orbits pass through?" and
// accumulates a density. That is a scatter/write-anywhere operation, which the
// GLSL path can't do on macOS (GL 4.1 has no compute shaders or image atomics),
// so this is a standalone multithreaded CPU renderer. It is also fully GL-free,
// which keeps it unit-testable. Quadratic map only (z -> z^2 + c).
#pragma once

#include "config.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace fractal {

// Render a Buddhabrot (or, with cfg.nebula, a Nebulabrot) into an RGB8 buffer,
// row-major with the top row first (PNG order), width*height*3 bytes.
//
// Fields used from cfg: type (mandelbrot samples c, julia samples z0), center_*,
// scale, width, height, bailout, julia_c*, samples, nebula, nebula_{r,g,b},
// max_iter (single-channel threshold), buddha_gamma, palette (single-channel
// coloring), inside_color (background).
//
// `progress`, if set, is called periodically with a fraction in [0,1].
std::vector<uint8_t> renderBuddhabrot(const RenderConfig& cfg,
                                      const std::function<void(float)>& progress = {});

} // namespace fractal

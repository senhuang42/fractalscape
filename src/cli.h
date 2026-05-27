// cli.h — command-line argument parsing.
//
// Parsing is pure (no I/O, no OpenGL) so it can be unit tested by feeding in
// argument vectors and inspecting the resulting config.
#pragma once

#include "config.h"

#include <string>
#include <vector>

namespace fractal {

enum class CommandKind { None, Help, Render, Video, Buddha, Explore, Center };

struct ParsedArgs {
    CommandKind  kind = CommandKind::None;
    RenderConfig render;  // valid when kind == Render / Buddha / Explore / Center
    VideoConfig  video;   // valid when kind == Video
    std::string  error;   // non-empty => parse failure (kind is unreliable)

    // Center-command parameters (kind == Center). The guess is render.center_*,
    // the Julia constant render.julia_c*, and the search radius render.scale.
    int  max_period = 128; // largest period to search
    int  find_period = 0;  // 0 = auto-search; else force this period
    bool misiurewicz = false; // Mandelbrot: find a Misiurewicz point, not a nucleus
    int  max_preperiod = 48;  // largest preperiod to search (Misiurewicz)
};

// Parse argv (excluding program name). The first token selects the subcommand
// ("render" or "video"); "help"/"--help"/"-h" requests usage.
ParsedArgs parseArgs(const std::vector<std::string>& args);

// Full usage/help text.
std::string helpText();

} // namespace fractal

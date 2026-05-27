// explorer.h — the interactive fractal explorer.
//
// Opens a live (Cocoa-backed) GLFW window and lets you fly through the set:
// drag to pan, scroll to zoom toward the cursor, and use the keyboard to cycle
// palettes/formulas, morph the Julia constant, toggle deep precision, snapshot
// a PNG, or print the exact `fractal render` command for the current view.
//
// This is the one other place besides renderer.cpp that touches GLFW, because
// it is inherently interactive (and so untestable). It drives input through the
// GLFWwindow* the Renderer exposes and presents frames via Renderer::present().
#pragma once

#include "config.h"

#include <string>

namespace fractal {

// Run the explorer starting from `start`. Blocks until the window closes.
// Returns 0 on clean exit, non-zero on init failure (message on stderr).
int runExplorer(const RenderConfig& start, const std::string& shader_dir);

} // namespace fractal

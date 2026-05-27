#include "explorer.h"

#include "gl.h"
#include "palette.h"
#include "renderer.h"
#include "stb_image_write.h" // declarations only; defined in main.cpp

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace fractal {

namespace {

const char* formulaName(Formula f) {
    switch (f) {
        case Formula::Quadratic:   return "quadratic";
        case Formula::BurningShip: return "burningship";
        case Formula::Tricorn:     return "tricorn";
        case Formula::Phoenix:     return "phoenix";
        case Formula::Newton:      return "newton";
    }
    return "quadratic";
}

// Everything the input callbacks need. Stashed on the GLFW window user pointer.
struct State {
    Renderer* r = nullptr;
    RenderConfig cfg;
    RenderConfig start;
    std::vector<std::string> palettes;
    int palIdx = -1;            // -1 = the starting (unnamed) palette
    std::string palName = "(start)";
    bool dirty = true;
    bool dragging = false;
    double lastx = 0, lasty = 0;
    int snapshot = 0;
};

State& stateOf(GLFWwindow* w) { return *static_cast<State*>(glfwGetWindowUserPointer(w)); }

// Map a window-space cursor position (logical pixels, origin top-left) to a
// point on the complex plane, mirroring the shader's mapping.
void screenToPlane(GLFWwindow* w, const RenderConfig& c, double sx, double sy,
                   double& px, double& py) {
    int ww = 1, wh = 1;
    glfwGetWindowSize(w, &ww, &wh);
    px = c.center_x + (sx - ww * 0.5) / wh * 2.0 * c.scale;
    py = c.center_y - (sy - wh * 0.5) / wh * 2.0 * c.scale; // screen y is down
}

void applyPalette(State& s) {
    if (s.palIdx < 0 || s.palIdx >= (int)s.palettes.size()) return;
    s.palName = s.palettes[s.palIdx];
    parsePalette(s.palName, s.cfg.palette);
    auto grad = buildGradient(s.cfg.palette, 1024, s.cfg.cyclic);
    s.r->setPalette(grad, 1024);
    s.dirty = true;
}

void rebuildGradient(State& s) {
    auto grad = buildGradient(s.cfg.palette, 1024, s.cfg.cyclic);
    s.r->setPalette(grad, 1024);
    s.dirty = true;
}

void printStatus(const State& s) {
    const RenderConfig& c = s.cfg;
    std::fprintf(stderr,
        "\r%-10s %-11s c=(%+.6f,%+.6f) center=(%+.9g,%+.9g) scale=%.3g iter=%d "
        "pal=%-10s%s%s%s   ",
        c.type == FractalType::Mandelbrot ? "mandelbrot" : "julia",
        formulaName(c.formula),
        c.julia_cre, c.julia_cim, c.center_x, c.center_y, c.scale, c.max_iter,
        s.palName.c_str(),
        c.cyclic ? " cyc" : "",
        c.deep ? " deep" : "",
        c.kaleido >= 2.0 ? " kaleido" : "");
    std::fflush(stderr);
}

// Print a `fractal render` command that reproduces the current view.
void printCommand(const State& s) {
    const RenderConfig& c = s.cfg;
    std::string cmd = "fractal render";
    cmd += c.type == FractalType::Mandelbrot ? " -t mandelbrot" : " -t julia";
    if (c.formula != Formula::Quadratic) { cmd += " --formula "; cmd += formulaName(c.formula); }
    if (c.type == FractalType::Julia) {
        char b[128];
        std::snprintf(b, sizeof b, " --cre %.10g --cim %.10g", c.julia_cre, c.julia_cim);
        cmd += b;
    }
    char b[256];
    std::snprintf(b, sizeof b, " --center-x %.12g --center-y %.12g --scale %.6g -i %d",
                  c.center_x, c.center_y, c.scale, c.max_iter);
    cmd += b;
    if (s.palIdx >= 0) { cmd += " -p "; cmd += s.palName; }
    if (c.cyclic)  cmd += " --cyclic";
    if (c.deep)    cmd += " --deep";
    if (c.kaleido >= 2.0) { std::snprintf(b, sizeof b, " --kaleido %g", c.kaleido); cmd += b; }
    cmd += " --size 2000x2000 --ssaa 4 -o view.png";
    std::fprintf(stderr, "\n%s\n", cmd.c_str());
}

void saveSnapshot(State& s) {
    auto px = s.r->readPixels();
    char name[64];
    std::snprintf(name, sizeof name, "explore_%03d.png", s.snapshot++);
    if (stbi_write_png(name, s.r->width(), s.r->height(), 3, px.data(), s.r->width() * 3))
        std::fprintf(stderr, "\nsaved %s (%dx%d)\n", name, s.r->width(), s.r->height());
    else
        std::fprintf(stderr, "\nfailed to save %s\n", name);
}

void scrollCB(GLFWwindow* w, double, double yoff) {
    State& s = stateOf(w);
    double sx, sy; glfwGetCursorPos(w, &sx, &sy);
    double px, py; screenToPlane(w, s.cfg, sx, sy, px, py);
    s.cfg.scale *= std::pow(0.85, yoff); // scroll up -> zoom in
    s.cfg.scale = std::max(s.cfg.scale, 1e-15);
    int ww = 1, wh = 1; glfwGetWindowSize(w, &ww, &wh);
    // Re-center so the complex point under the cursor stays put.
    s.cfg.center_x = px - (sx - ww * 0.5) / wh * 2.0 * s.cfg.scale;
    s.cfg.center_y = py + (sy - wh * 0.5) / wh * 2.0 * s.cfg.scale;
    s.dirty = true;
}

void mouseBtnCB(GLFWwindow* w, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    State& s = stateOf(w);
    if (action == GLFW_PRESS) { s.dragging = true; glfwGetCursorPos(w, &s.lastx, &s.lasty); }
    else if (action == GLFW_RELEASE) s.dragging = false;
}

void cursorCB(GLFWwindow* w, double x, double y) {
    State& s = stateOf(w);
    if (!s.dragging) return;
    int ww = 1, wh = 1; glfwGetWindowSize(w, &ww, &wh);
    const double perPx = 2.0 * s.cfg.scale / wh;
    s.cfg.center_x -= (x - s.lastx) * perPx;
    s.cfg.center_y += (y - s.lasty) * perPx; // screen y down -> plane y up
    s.lastx = x; s.lasty = y;
    s.dirty = true;
}

void keyCB(GLFWwindow* w, int key, int, int action, int) {
    if (action == GLFW_RELEASE) return;
    State& s = stateOf(w);
    RenderConfig& c = s.cfg;
    const int n = (int)s.palettes.size();
    const double jstep = 0.0025;

    switch (key) {
        case GLFW_KEY_ESCAPE:
        case GLFW_KEY_Q: glfwSetWindowShouldClose(w, GLFW_TRUE); break;

        case GLFW_KEY_EQUAL:
        case GLFW_KEY_KP_ADD:
            c.max_iter = std::min(200000, (int)(c.max_iter * 1.4) + 10); s.dirty = true; break;
        case GLFW_KEY_MINUS:
        case GLFW_KEY_KP_SUBTRACT:
            c.max_iter = std::max(20, (int)(c.max_iter / 1.4)); s.dirty = true; break;

        case GLFW_KEY_LEFT_BRACKET:  s.palIdx = (s.palIdx <= 0 ? n : s.palIdx) - 1; applyPalette(s); break;
        case GLFW_KEY_RIGHT_BRACKET: s.palIdx = (s.palIdx + 1) % n; applyPalette(s); break;

        case GLFW_KEY_F: { // cycle iteration formula
            c.formula = static_cast<Formula>(((int)c.formula + 1) % 5); s.dirty = true; break;
        }
        case GLFW_KEY_T:
            c.type = c.type == FractalType::Julia ? FractalType::Mandelbrot : FractalType::Julia;
            s.dirty = true; break;
        case GLFW_KEY_S: c.stripe_color = c.stripe_color > 0.0 ? 0.0 : 1.0; s.dirty = true; break;
        case GLFW_KEY_C: c.cyclic = !c.cyclic; rebuildGradient(s); break;
        case GLFW_KEY_D:
            c.deep = !c.deep;
            std::fprintf(stderr, "\ndeep precision %s\n", c.deep ? "ON (df64)" : "off");
            s.dirty = true; break;
        case GLFW_KEY_K: // cycle kaleidoscope symmetry
            c.kaleido = c.kaleido < 2.0 ? 6.0 : (c.kaleido < 6.5 ? 8.0 : (c.kaleido < 8.5 ? 12.0 : 0.0));
            s.dirty = true; break;
        case GLFW_KEY_B: c.bloom = c.bloom > 0.0 ? 0.0 : 0.3; s.dirty = true; break;

        case GLFW_KEY_UP:    c.julia_cim += jstep; s.dirty = true; break;
        case GLFW_KEY_DOWN:  c.julia_cim -= jstep; s.dirty = true; break;
        case GLFW_KEY_LEFT:  c.julia_cre -= jstep; s.dirty = true; break;
        case GLFW_KEY_RIGHT: c.julia_cre += jstep; s.dirty = true; break;

        case GLFW_KEY_R: { // reset view
            std::string keepName = s.palName; int keepIdx = s.palIdx;
            s.cfg = s.start; s.palName = keepName; s.palIdx = keepIdx;
            rebuildGradient(s); break;
        }
        case GLFW_KEY_SPACE: printCommand(s); break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER: saveSnapshot(s); break;
        default: break;
    }
}

void printHelp() {
    std::fprintf(stderr,
        "fractalscape explorer — controls:\n"
        "  drag           pan            scroll      zoom toward cursor\n"
        "  +/-            iterations     [ ]         prev/next palette\n"
        "  arrows         morph Julia c  F           cycle formula\n"
        "  T              julia<->mandel S           toggle stripe layer\n"
        "  C              cyclic palette K           cycle kaleidoscope\n"
        "  B              toggle bloom   D           toggle deep (df64) zoom\n"
        "  R              reset view     Space       print render command\n"
        "  Enter          save PNG       Q/Esc       quit\n\n");
}

} // namespace

int runExplorer(const RenderConfig& start, const std::string& shader_dir) {
    Renderer r;
    std::string err;
    if (!r.init(start.width, start.height, start.ssaa, shader_dir, err, /*visible=*/true)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    State s;
    s.r = &r;
    s.cfg = start;
    s.start = start;
    s.palettes = builtinPaletteNames();

    // Upload the starting palette as-is (it was already resolved by the CLI).
    auto grad = buildGradient(s.cfg.palette, 1024, s.cfg.cyclic);
    r.setPalette(grad, 1024);

    GLFWwindow* win = static_cast<GLFWwindow*>(r.window());
    glfwSetWindowUserPointer(win, &s);
    glfwSetScrollCallback(win, scrollCB);
    glfwSetMouseButtonCallback(win, mouseBtnCB);
    glfwSetCursorPosCallback(win, cursorCB);
    glfwSetKeyCallback(win, keyCB);

    printHelp();

    while (!glfwWindowShouldClose(win)) {
        glfwWaitEventsTimeout(0.05);
        if (s.dirty) { r.render(s.cfg); s.dirty = false; printStatus(s); }
        r.present();
    }
    std::fprintf(stderr, "\n");
    return 0;
}

} // namespace fractal

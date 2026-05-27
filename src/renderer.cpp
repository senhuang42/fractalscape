#include "renderer.h"
#include "gl.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef __APPLE__
#  include <mach-o/dyld.h>
#endif
#include <libgen.h>
#include <sys/stat.h>

namespace fractal {

namespace {

bool fileExists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

std::string readFile(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { ok = false; return {}; }
    std::ostringstream ss;
    ss << f.rdbuf();
    ok = true;
    return ss.str();
}

// Directory containing the running executable (for locating shaders/).
std::string executableDir() {
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(&buf[0], &size) != 0) return ".";
    // Resolve to dirname; dirname may modify its argument.
    std::string copy = buf;
    return std::string(dirname(&copy[0]));
#else
    return ".";
#endif
}

// Find a directory containing the shader files.
std::string findShaderDir(const std::string& explicit_dir) {
    std::vector<std::string> candidates;
    if (!explicit_dir.empty()) candidates.push_back(explicit_dir);
    candidates.push_back("shaders");
    const std::string exe = executableDir();
    candidates.push_back(exe + "/shaders");
    candidates.push_back(exe + "/../shaders");
    for (const auto& c : candidates)
        if (fileExists(c + "/fullscreen.vert")) return c;
    return {};
}

unsigned int compileShader(GLenum type, const std::string& src, std::string& err) {
    unsigned int s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetShaderInfoLog(s, len, nullptr, &log[0]);
        err = "shader compile error: " + log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

unsigned int linkProgram(const std::string& vsrc, const std::string& fsrc, std::string& err) {
    unsigned int vs = compileShader(GL_VERTEX_SHADER, vsrc, err);
    if (!vs) return 0;
    unsigned int fs = compileShader(GL_FRAGMENT_SHADER, fsrc, err);
    if (!fs) { glDeleteShader(vs); return 0; }
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetProgramInfoLog(prog, len, nullptr, &log[0]);
        err = "program link error: " + log;
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// Allocate an RGBA8 color texture + FBO at the given size.
bool makeTarget(int w, int h, unsigned int& fbo, unsigned int& tex, std::string& err) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        err = "framebuffer incomplete at " + std::to_string(w) + "x" + std::to_string(h);
        return false;
    }
    return true;
}

void setUniform1i(unsigned int p, const char* n, int v)   { glUniform1i(glGetUniformLocation(p, n), v); }
void setUniform1f(unsigned int p, const char* n, float v) { glUniform1f(glGetUniformLocation(p, n), v); }
void setUniform2f(unsigned int p, const char* n, float a, float b) { glUniform2f(glGetUniformLocation(p, n), a, b); }
void setUniform3f(unsigned int p, const char* n, float a, float b, float c) { glUniform3f(glGetUniformLocation(p, n), a, b, c); }

} // namespace

Renderer::~Renderer() {
    if (window_) {
        if (palette_tex_)     glDeleteTextures(1, &palette_tex_);
        for (unsigned int t : {tex_hi_, tex_lo_, tex_b0_, tex_b1_, tex_out_, tex_post_})
            if (t) glDeleteTextures(1, &t);
        for (unsigned int f : {fbo_hi_, fbo_lo_, fbo_b0_, fbo_b1_, fbo_out_, fbo_post_})
            if (f) glDeleteFramebuffers(1, &f);
        if (vao_)             glDeleteVertexArrays(1, &vao_);
        if (fractal_prog_)    glDeleteProgram(fractal_prog_);
        if (downsample_prog_) glDeleteProgram(downsample_prog_);
        if (bloom_prog_)      glDeleteProgram(bloom_prog_);
        if (composite_prog_)  glDeleteProgram(composite_prog_);
        if (post_prog_)       glDeleteProgram(post_prog_);
        glfwDestroyWindow(static_cast<GLFWwindow*>(window_));
        glfwTerminate();
    }
}

bool Renderer::init(int width, int height, int ssaa,
                    const std::string& shader_dir, std::string& err) {
    width_ = width;
    height_ = height;
    ssaa_ = ssaa;

    if (!glfwInit()) { err = "glfwInit failed (no display/GL available?)"; return false; }

    // Request OpenGL 3.3 core, forward-compatible (required on macOS).
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // offscreen

    GLFWwindow* win = glfwCreateWindow(64, 64, "fractal", nullptr, nullptr);
    if (!win) { err = "failed to create GL context"; glfwTerminate(); return false; }
    window_ = win;
    glfwMakeContextCurrent(win);

    // Load and link shader programs.
    const std::string dir = findShaderDir(shader_dir);
    if (dir.empty()) {
        err = "could not locate shaders/ (looked next to the binary and in CWD); "
              "pass --shader-dir";
        return false;
    }
    bool ok1 = false, ok2 = false, ok3 = false, ok4 = false, ok5 = false, ok6 = false;
    std::string vsrc  = readFile(dir + "/fullscreen.vert", ok1);
    std::string fsrc  = readFile(dir + "/fractal.frag", ok2);
    std::string dssrc = readFile(dir + "/downsample.frag", ok3);
    std::string blsrc = readFile(dir + "/bloom.frag", ok4);
    std::string cmsrc = readFile(dir + "/composite.frag", ok5);
    std::string ptsrc = readFile(dir + "/post.frag", ok6);
    if (!ok1 || !ok2 || !ok3 || !ok4 || !ok5 || !ok6) { err = "failed to read shader files from " + dir; return false; }

    fractal_prog_ = linkProgram(vsrc, fsrc, err);
    if (!fractal_prog_) return false;
    downsample_prog_ = linkProgram(vsrc, dssrc, err);
    if (!downsample_prog_) return false;
    bloom_prog_ = linkProgram(vsrc, blsrc, err);
    if (!bloom_prog_) return false;
    composite_prog_ = linkProgram(vsrc, cmsrc, err);
    if (!composite_prog_) return false;
    post_prog_ = linkProgram(vsrc, ptsrc, err);
    if (!post_prog_) return false;

    glGenVertexArrays(1, &vao_); // empty VAO required by core profile

    const int hw = width_ * ssaa_, hh = height_ * ssaa_;
    if (!makeTarget(hw, hh, fbo_hi_, tex_hi_, err)) return false;
    if (!makeTarget(width_, height_, fbo_lo_, tex_lo_, err)) return false;
    if (!makeTarget(width_, height_, fbo_b0_, tex_b0_, err)) return false;
    if (!makeTarget(width_, height_, fbo_b1_, tex_b1_, err)) return false;
    if (!makeTarget(width_, height_, fbo_out_, tex_out_, err)) return false;
    if (!makeTarget(width_, height_, fbo_post_, tex_post_, err)) return false;

    // Linear filtering for textures sampled with texture() (resolve + bloom +
    // post; aberration in particular samples between texels).
    for (unsigned int t : {tex_hi_, tex_lo_, tex_b0_, tex_b1_, tex_out_}) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    glGenTextures(1, &palette_tex_);
    return true;
}

void Renderer::setPalette(const std::vector<uint8_t>& rgb, int resolution) {
    glBindTexture(GL_TEXTURE_2D, palette_tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, resolution, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);        // seamless cycling
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::render(const RenderConfig& cfg) {
    const int hw = width_ * ssaa_, hh = height_ * ssaa_;

    // --- Pass 1: fractal at supersampled resolution ---
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_hi_);
    glViewport(0, 0, hw, hh);
    glUseProgram(fractal_prog_);
    glBindVertexArray(vao_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, palette_tex_);

    setUniform2f(fractal_prog_, "uResolution", (float)hw, (float)hh);
    setUniform2f(fractal_prog_, "uCenter", (float)cfg.center_x, (float)cfg.center_y);
    setUniform1f(fractal_prog_, "uScale", (float)cfg.scale);
    setUniform2f(fractal_prog_, "uJuliaC", (float)cfg.julia_cre, (float)cfg.julia_cim);
    setUniform1i(fractal_prog_, "uType", cfg.type == FractalType::Mandelbrot ? 0 : 1);
    setUniform1i(fractal_prog_, "uMaxIter", cfg.max_iter);
    setUniform1f(fractal_prog_, "uBailout", (float)cfg.bailout);
    setUniform1f(fractal_prog_, "uExponent", (float)cfg.exponent);
    setUniform1i(fractal_prog_, "uPalette", 0);
    setUniform1f(fractal_prog_, "uColorDensity", (float)cfg.color_density);
    setUniform1f(fractal_prog_, "uColorOffset", (float)cfg.color_offset);
    setUniform1f(fractal_prog_, "uAngleColor", (float)cfg.angle_color);
    setUniform1f(fractal_prog_, "uTrapColor", (float)cfg.trap_color);
    setUniform2f(fractal_prog_, "uTrapPoint", (float)cfg.trap_x, (float)cfg.trap_y);
    setUniform1f(fractal_prog_, "uStripeColor", (float)cfg.stripe_color);
    setUniform1f(fractal_prog_, "uStripeFreq", (float)cfg.stripe_freq);
    setUniform1f(fractal_prog_, "uStripeContrast", (float)cfg.stripe_contrast);
    setUniform3f(fractal_prog_, "uInsideColor", cfg.inside_color.r, cfg.inside_color.g, cfg.inside_color.b);
    setUniform1f(fractal_prog_, "uShading", (float)cfg.shading);
    setUniform1f(fractal_prog_, "uLightAngle", (float)cfg.light_angle);
    setUniform1f(fractal_prog_, "uLightHeight", (float)cfg.light_height);
    setUniform1f(fractal_prog_, "uSpecular", (float)cfg.specular);
    setUniform1f(fractal_prog_, "uShininess", (float)cfg.shininess);
    setUniform1f(fractal_prog_, "uHeightScale", (float)cfg.height_scale);
    setUniform1f(fractal_prog_, "uGlow", (float)cfg.glow);
    setUniform1f(fractal_prog_, "uFalloff", (float)cfg.falloff);
    setUniform1f(fractal_prog_, "uKaleido", (float)cfg.kaleido);
    setUniform1f(fractal_prog_, "uKaleidoAngle", (float)(cfg.kaleido_angle * M_PI / 180.0));

    glDrawArrays(GL_TRIANGLES, 0, 3);

    // --- Pass 2: gamma-correct downsample to output resolution ---
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_lo_);
    glViewport(0, 0, width_, height_);
    glUseProgram(downsample_prog_);
    glBindVertexArray(vao_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_hi_);
    setUniform1i(downsample_prog_, "uTex", 0);
    setUniform1i(downsample_prog_, "uSSAA", ssaa_);
    setUniform1f(downsample_prog_, "uSaturation", (float)cfg.saturation);
    setUniform1f(downsample_prog_, "uGamma", (float)cfg.gamma);
    setUniform1f(downsample_prog_, "uBlackPoint", (float)cfg.black_point);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    read_fbo_ = fbo_lo_;
    read_tex_ = tex_lo_;

    // --- Bloom: bright-pass + separable blur, screen-composited back ---
    if (cfg.bloom > 0.0) {
        const float fw = (float)width_, fh = (float)height_;
        const float spread = std::max(1.0f, fh / 400.0f); // tap spacing in px
        glViewport(0, 0, width_, height_);
        glBindVertexArray(vao_);

        // Pass A: extract bright + horizontal blur (fbo_lo -> b0)
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_b0_);
        glUseProgram(bloom_prog_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_lo_);
        setUniform1i(bloom_prog_, "uTex", 0);
        setUniform2f(bloom_prog_, "uTexSize", fw, fh);
        setUniform2f(bloom_prog_, "uDir", spread, 0.0f);
        setUniform1i(bloom_prog_, "uExtract", 1);
        setUniform1f(bloom_prog_, "uThreshold", (float)cfg.bloom_threshold);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Pass B: vertical blur (b0 -> b1)
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_b1_);
        glBindTexture(GL_TEXTURE_2D, tex_b0_);
        setUniform2f(bloom_prog_, "uDir", 0.0f, spread);
        setUniform1i(bloom_prog_, "uExtract", 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Composite: fbo_lo + bloom(b1) -> fbo_out
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_out_);
        glUseProgram(composite_prog_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_lo_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex_b1_);
        setUniform1i(composite_prog_, "uBase", 0);
        setUniform1i(composite_prog_, "uBloom", 1);
        setUniform2f(composite_prog_, "uTexSize", fw, fh);
        setUniform1f(composite_prog_, "uStrength", (float)cfg.bloom);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glActiveTexture(GL_TEXTURE0);

        read_fbo_ = fbo_out_;
        read_tex_ = tex_out_;
    }

    // --- Post: chromatic aberration + vignette + scanlines + grain ---
    // Only run when something is active; otherwise it's an identity copy.
    const bool wantPost = cfg.aberration > 0.0 || cfg.vignette > 0.0 ||
                          cfg.grain > 0.0 || cfg.scanlines > 0.0;
    if (wantPost) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_post_);
        glViewport(0, 0, width_, height_);
        glUseProgram(post_prog_);
        glBindVertexArray(vao_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, read_tex_);
        setUniform1i(post_prog_, "uTex", 0);
        setUniform2f(post_prog_, "uTexSize", (float)width_, (float)height_);
        setUniform1f(post_prog_, "uAberration", (float)cfg.aberration);
        setUniform1f(post_prog_, "uVignette", (float)cfg.vignette);
        setUniform1f(post_prog_, "uScanlines", (float)cfg.scanlines);
        setUniform1f(post_prog_, "uGrain", (float)cfg.grain);
        setUniform1f(post_prog_, "uSeed", (float)cfg.color_offset * 977.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        read_fbo_ = fbo_post_;
        read_tex_ = tex_post_;
    }
    // No glFinish() here: the subsequent glReadPixels already forces a sync,
    // so an explicit finish would just be a redundant full-pipeline flush.
}

std::vector<uint8_t> Renderer::readPixels() {
    std::vector<uint8_t> buf(static_cast<size_t>(width_) * height_ * 3);
    glBindFramebuffer(GL_FRAMEBUFFER, read_fbo_ ? read_fbo_ : fbo_lo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, buf.data());

    // GL returns bottom-to-top; flip to top-to-bottom for image files.
    const int stride = width_ * 3;
    std::vector<uint8_t> row(stride);
    for (int y = 0; y < height_ / 2; ++y) {
        uint8_t* top = &buf[static_cast<size_t>(y) * stride];
        uint8_t* bot = &buf[static_cast<size_t>(height_ - 1 - y) * stride];
        std::copy(top, top + stride, row.begin());
        std::copy(bot, bot + stride, top);
        std::copy(row.begin(), row.end(), bot);
    }
    return buf;
}

} // namespace fractal

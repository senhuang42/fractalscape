#version 330 core
// Post-processing for the "album art" design layer. Runs once on the final
// composited image. All effects are no-ops at 0, so the pass is only bound when
// at least one is active.
//
//   * Chromatic aberration -> split R/B along the radius so bright edges fringe
//     into neon cyan/magenta (reads as energy/motion).
//   * Vignette  -> darken toward the corners to seat the focal point.
//   * Scanlines -> horizontal CRT lines for a glitch/cyber look.
//   * Grain     -> additive noise so flat regions read as texture, not plastic.

out vec4 FragColor;

uniform sampler2D uTex;
uniform vec2  uTexSize;
uniform float uAberration; // max radial channel offset, in pixels
uniform float uVignette;   // 0..1 darkening strength at the corners
uniform float uScanlines;  // 0..1 scanline depth
uniform float uGrain;      // additive noise amplitude (~0..0.1)
uniform float uSeed;       // varies the grain pattern (per frame for video)

const float PI = 3.14159265358979;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 uv  = gl_FragCoord.xy / uTexSize;
    vec2 toC = uv - 0.5;
    float r  = length(toC) * 1.41421356; // 0 at center, ~1 at the corners

    // Chromatic aberration: push R out / B in along the radius, ramped by r^2 so
    // the center stays crisp and the edges fringe.
    vec3 col;
    if (uAberration > 0.0) {
        vec2 off = toC * (uAberration / uTexSize) * r * 2.0;
        col.r = texture(uTex, uv + off).r;
        col.g = texture(uTex, uv).g;
        col.b = texture(uTex, uv - off).b;
    } else {
        col = texture(uTex, uv).rgb;
    }

    // Scanlines: a fixed ~260 CRT lines, resolution-independent (tying the
    // frequency to pixel height just aliases into invisibility at high res).
    if (uScanlines > 0.0) {
        float s = 0.5 + 0.5 * sin(uv.y * 260.0 * PI);
        col *= 1.0 - uScanlines * 0.6 * (1.0 - s);
    }

    // Vignette.
    if (uVignette > 0.0) {
        float v = smoothstep(1.05, 0.25, r); // 1 in the center -> 0 at corners
        col *= mix(1.0, v, uVignette);
    }

    // Film grain.
    if (uGrain > 0.0) {
        float n = hash(gl_FragCoord.xy + uSeed) - 0.5;
        col += n * uGrain;
    }

    FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}

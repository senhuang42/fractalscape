#version 330 core
// Separable Gaussian blur with an optional bright-pass extract, used for bloom.
// Run twice (horizontal then vertical); the first pass extracts bright areas.

out vec4 FragColor;

uniform sampler2D uTex;
uniform sampler2D uNebulaTex;     // Buddhabrot density (read in extract pass)
uniform vec3      uNebulaColor;   // tint applied to nebula-driven glow
uniform bool      uNebulaRgb;     // if true, use full nebula .rgb as bloom seed
uniform float     uNebulaBloom;   // density-driven bloom strength (0 = off)
uniform vec2  uTexSize;    // size of uTex in pixels
uniform vec2  uDir;        // blur direction in pixels: (1,0) then (0,1)
uniform int   uExtract;    // 1 = subtract threshold first (bright pass)
uniform float uThreshold;  // bloom threshold

vec3 sample(vec2 px) {
    vec3 c = texture(uTex, px / uTexSize).rgb;
    if (uExtract == 1) {
        float lum = dot(c, vec3(0.299, 0.587, 0.114));
        c *= max(lum - uThreshold, 0.0) / max(lum, 1e-4); // keep hue, drop dim
        // Modality 3: orbit-density-driven bloom mask. The nebula texture is
        // sampled at the same UV; pixels with high orbit density become bloom
        // sources INDEPENDENT of their own luminance. Wisps glow even where
        // the underlying fractal is dim. Flip y to match buddhabrot top-down
        // layout against bottom-up gl_FragCoord.
        if (uNebulaBloom > 0.0) {
            vec2 nuv = px / uTexSize;
            vec3 nsam = texture(uNebulaTex, vec2(nuv.x, 1.0 - nuv.y)).rgb;
            float d = uNebulaRgb ? max(max(nsam.r, nsam.g), nsam.b) : nsam.r;
            vec3 seed = uNebulaRgb ? nsam : (d * uNebulaColor);
            c += seed * d * uNebulaBloom; // weighted by density (d^2 in mono)
        }
    }
    return c;
}

void main() {
    // 9-tap Gaussian (sigma ~ 2.5), weights normalized.
    float w[5] = float[](0.20236, 0.17891, 0.12384, 0.06738, 0.02853);
    vec2  base = gl_FragCoord.xy;
    vec3  sum  = sample(base) * w[0];
    for (int i = 1; i < 5; i++) {
        vec3 ofs = vec3(0.0);
        sum += sample(base + uDir * float(i)) * w[i];
        sum += sample(base - uDir * float(i)) * w[i];
    }
    FragColor = vec4(sum, 1.0);
}

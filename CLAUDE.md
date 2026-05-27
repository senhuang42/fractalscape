# CLAUDE.md

Guidance for working in this repo. Keep it current as the project evolves.

## What this is

A C++/OpenGL CLI that renders Julia/Mandelbrot fractals as stills (PNG) and
seamless loop videos (MP4). GPU does the real work via GLSL fragment shaders;
the CLI just builds a config, drives the renderer, and writes files. Two
commands sit outside the GPU escape-time path: `buddhabrot` (a CPU density
renderer) and `explore` (a live interactive window).

## Build / test / run

```sh
brew install glfw ffmpeg     # one-time deps
make                         # -> ./fractal  (arm64, -O3 -flto, system OpenGL + GLFW)
make test                    # -> ./run_tests (GL-free unit tests)
./fractal help               # full CLI reference
```

- Targets macOS / Apple Silicon. Uses Apple's system OpenGL framework (no
  glad/glew) + Homebrew GLFW. GL 3.3 core profile, runs on Metal.
- Shaders are loaded from `shaders/` at runtime (searched next to the binary
  and in CWD; override with `FRACTAL_SHADER_DIR` or `--shader-dir`). You can
  edit a shader and re-run **without** rebuilding the C++.

## Architecture

- `src/config.h` — all parameters as plain data (`RenderConfig`,
  `VideoConfig`). No GL, no I/O. Everything flows from here.
- `src/cli.{h,cpp}` — argv -> config. Pure and fully unit-tested. Add new flags
  here *and* in `helpText()` *and* cover them in `tests/test_cli.cpp`.
- `src/palette.{h,cpp}` — hex/named palette parsing -> RGB gradient bytes.
- `src/renderer.{h,cpp}` + `src/gl.h` — the ONLY OpenGL code. Passes: render
  fractal into a supersampled FBO -> gamma-correct downsample to output FBO ->
  (optional) bloom: bright-pass + separable Gaussian blur (`bloom.frag`, run H
  then V) screen-composited back (`composite.frag`) -> (optional) post pass
  (`post.frag`: chromatic aberration + vignette + scanlines + grain, run only
  if any is > 0) -> `glReadPixels` from whichever FBO was last written
  (tracked by `read_fbo_`/`read_tex_`).
- Shaders: `fractal.frag` (math/coloring/lighting + the kaleidoscope coord
  fold), `downsample.frag` (resolve + grade), `bloom.frag` (bright-pass +
  separable blur), `composite.frag`, `post.frag` (album-art design layer).
- `shaders/fractal.frag` — the math + coloring + shading + traps.
- `src/fractal_math.h` — CPU reference that MIRRORS `fractal.frag`. Kept in sync
  so the algorithm is unit-testable without a GL context. If you change the
  iteration/coloring math in the shader, update this too (and its tests).
- `src/main.cpp` — PNG via `stb_image_write`, MP4 by piping raw RGB frames to
  `ffmpeg`. Per-frame animation lives in `frameConfig()`. Dispatches the four
  commands (render / video / buddhabrot / explore).
- `src/buddhabrot.{h,cpp}` — the `buddhabrot` command. GL-free, multithreaded
  CPU Buddhabrot/Nebulabrot. Stays out of the renderer because it is a scatter
  (write-anywhere) operation that macOS GL 4.1 can't express (no compute
  shaders / image atomics). Being GL-free, it is unit-tested (`test_buddhabrot`).
- `src/explorer.{h,cpp}` — the `explore` command: a live GLFW window. THE ONLY
  other TU besides renderer.cpp allowed to touch GLFW (it is interactive and so
  untestable). It drives input through the GLFWwindow* exposed by
  `Renderer::window()` and shows frames via `Renderer::present()` (a blit of the
  last FBO to the default framebuffer); the renderer still owns all GL resources.
- `src/periodic.h` + `src/doubledouble.h` — the `center` command. Header-only,
  GL-free, unit-tested (`test_periodic`). Newton-refines an approximate zoom
  target to an EXACT self-similar point: a Julia repelling periodic point
  (`f^p(z)=z`) or a Mandelbrot nucleus (`f^p(0)=0`). The Newton core is templated
  on the complex scalar so it runs in either `std::complex<double>` (the tested
  wrappers) or `cdd` (CPU double-double, ~32 digits) -- `findCenter` uses cdd so
  the result holds to the renderer's df64 wall. `doubledouble.h` is the CPU twin
  of the GPU df64: two_prod uses a real `std::fma` (exact on arm64), two_sum uses
  only +/- (clang won't contract those), so no special flags are needed.

## Adding a parameter (the full path)

1. Field + default in `config.h`.
2. Flag parsing in `cli.cpp` + a line in `helpText()`.
3. If it affects rendering: a `uniform` in the relevant shader + a
   `setUniform*` call in `renderer.cpp::render()`.
4. A test in `tests/test_cli.cpp`.

## Gotchas / hard-won lessons

- **Makefile header deps**: objects are rebuilt when headers change via
  `-MMD -MP` + `-include *.d`. This matters because `config.h` is included
  everywhere — without it, changing a struct field gives an ABI mismatch
  between `main.o` and the others and the program crashes (SIGABRT) with
  garbage config. If you ever see weird crashes after editing a header,
  `make clean && make`.
- **Coloring is two layers, gated (the philthompson overlay idea).** See the
  color block in `fractal.frag`:
  1. *Iteration layer*: `iterS = 1 - exp(-mu * color_density)`. Fast-escape
     exterior → ~0 (dark), slow-escape filaments → ~1 (bright). This draws the
     structure, the dendrite tendrils, and the dark negative space.
  2. *Stripe layer (SAC)*: average `½+½·sin(freq·arg z)` over the orbit,
     interpolate incl/excl the last point by `frac(mu)`. The de-banding
     interpolation is non-negotiable (no it → level-set seams; with it → smooth
     3D relief). Needs a LARGE bailout (default 10000).
  - Combine: `mix(iterCol, stripeCol * smoothstep(0.12,0.55,iterS), stripe_color)`.
    The smoothstep gate keeps gaps black while the structure shows FULL stripe
    relief (a plain multiply dims the structure; a plain hard-light leaks bright
    fur into the gaps — both were tried and rejected).
  - Layer selection: `color_density==0` → stripe only (image #10);
    `stripe_color==0` → iteration only (image #9); both > 0 → overlay (#8).
- **Tendril reach is a property of the fractal, not a setting.** Whole-set
  Julia views have smooth far-field exteriors (no tendrils → SAC makes smooth
  "fur" there). Real dendrite tendrils live near the boundary — more dendritic
  `c` (e.g. -0.7269+0.1889i) or Mandelbrot regions, plus high iterations.
- **Restrained ramp palettes are essential.** A full-spectrum hue wheel turns
  fine detail into rainbow noise. Built-ins are dark→bright ramps; `cyclic`
  defaults false (cycle-mode video forces it true). `magma`/`viridis` are the
  perceptually-uniform matplotlib maps.
- **Off by default** (they fight the clean look): `--shading`/`--specular`
  (Blinn-Phong height-field lighting — proper but can darken), `--angle-color`,
  `--trap-color`, `--falloff`. All still there.
- **`--black-point`** (downsample, default 0.08) crushes near-blacks so the
  empty exterior reads as true black instead of dark-grey haze. The other half
  of that fix is the overlay gate `smoothstep(0.20, 0.62, iterS)`.
- **Bloom** is the last render pass (`bloom.frag` H+V, `composite.frag`), on by
  default. **Lighting** is in `fractal.frag` (height = displayed luminance via
  `dFdx/dFdy`, resolution-normalized).
- **Album-art design layer** (all off by default; the `cover-*` presets wire
  them up on the fuchsia-forward `vice` palette): `--kaleido N` folds the screen
  coordinate into N mirrored wedges in `fractal.frag` *before* the plane map
  (symmetry about frame center, independent of `--center`); `--aberration`,
  `--vignette`, `--scanlines`, `--grain` are a single post pass (`post.frag`).
  Two gotchas baked in: scanline frequency is a FIXED line count (260), not
  tied to pixel height, or it aliases to nothing at high res; radial aberration
  barely fringes a centered subject, so it needs a healthy value (~20px) to read.
  Covers are square — render `--size 3000x3000` for streaming/print.
- **Presets** (`applyPreset` in `cli.cpp`) are curated c+framing+palette combos
  applied as a base in a pre-scan, so explicit flags override them. Add new ones
  there AND in `presetNames()` AND a test in `test_cli.cpp`. Per-preset example
  stills live in `assets/presets/<name>.png`; `assets/presets.png` is the
  `montage` contact sheet of them (regenerate both when adding a preset).
- **A cyclic palette is the key to a multi-hue image.** A non-cyclic ramp on a
  region whose stripe/iter values cluster reads as ONE hue (e.g. all teal). The
  `acid-swirl` preset gets vivid teal↔fuchsia from the *same default Julia set*
  only because `prism` is applied cyclic (`c.cyclic=true`) so the relief sweeps
  the whole gradient and loops. Reach for cyclic before reaching for a new `c`.
- **Washed-out interiors = a contrast problem, almost always the palette.** The
  deep interior of a spiral has high `mu` and SAC ≈ 0.5, so its relief is
  inherently *low-amplitude* — it only shows if the palette has the dynamic
  range to expand it. Two things flatten it: a palette whose dark anchor isn't
  near-black (gruvbox's old `#1d2021` grey → washed haze; fixed to `#0d0b08`),
  and pastel/desaturated mids or a lowered `saturation` grade (don't — I muted
  `prism` into pastels once and the interiors went flat; reverted). Also: a
  *desaturated near-white* top stop makes slow-escape cores read as a grey blob
  (ember's old `#fff1c1` → grey seahorse eye; fixed to warm gold `#ffdc8f`).
  Rule of thumb: ramp near-black → saturated mids → warm/colored (not white)
  highlight, and leave `saturation` at the default 1.3. The per-preset lever for
  internal relief contrast is `stripe_contrast` (default 2.2): bumping it to
  ~3.4 stretches the SAC values toward the palette's dark and bright ends, which
  un-washes flat interiors (applied to candy-swirl/synthwave/dracula/rosegold
  alongside more contrasty palettes). The faithful muted themes (`nord`,
  `cividis`) are intentionally low-contrast and left alone.
- **Palettes** are dark→bright ramps in `palette.cpp`; restrained ones keep
  detail coherent, wider-gamut ones (sunset/neon/prism/etc.) are vibrant.
- **Zoom videos**: the default path is 32-bit float -> pixelates past ~1e4×.
  Keep the target on a detail-rich exterior point at ALL scales (probe with
  stills) or the path crosses black set-interior bodies. -0.7453,0.1127 works
  to ~0.0013.
- **`--color-cycles N`** (VideoConfig, applied at the end of `frameConfig`)
  overlays N palette sweeps on ANY mode by adding `N*u` to `color_offset`, so a
  zoom can cycle colors as it dives. The parser forces `cyclic=true` when it's
  set (a sweep only loops smoothly on a closed gradient) -- which also means the
  exterior/negative space sweeps through colors instead of staying dark.
- **Deep-zoom "lakes" land at the TOP of the palette, so the top stop sets their
  color.** At depth there's no fast-escape exterior in frame; the smooth regions
  between filaments are slow-escape (high mu), so the iteration layer maps them to
  iterS~1 = the bright end of the ramp. With plain `neon` (top = green/yellow)
  that reads as a murky olive; a palette with a DARK top renders them black and
  the structure pops. `--color-density` can't fix it at depth (mu is so large
  iterS saturates regardless); `--color-offset` only relocates the hue. The
  `neon-dark` palette brackets the neon hues with a broad dark band for exactly
  this, and because both ends are dark it can be CYCLED: the background then
  breathes black -> jewel tones instead of olive. assets/zoom_neon_dust.mp4 uses
  it with a slow one-cycle sweep. (Caveat: cycling rotates the whole ramp, so
  "lakes" are only black for the part of the cycle the dark band covers -- a
  broad dark band keeps them dark most of the time; you can't have permanently
  black lakes AND a full color cycle from one offset-swept gradient.)
- **Deep zoom centers: use `fractal center`, don't hand-navigate.** Eyeballing a
  spiral eye reads only ~7 digits before it drifts off-frame, which used to cap
  zooms around ~1e-9. The `center` command Newton-refines the guess to the exact
  periodic point / nucleus, good past the df64 wall. Workflow: eyeball a spiral
  in `explore` or a still -> `fractal center --center-x.. --center-y.. [--cre
  --cim]` -> paste the printed `--deep` video command. Verified neon-dust target
  (c=0.285+0.01i): period-79 repelling point at
  (-0.5265090209000343, 0.1889619338054614), |multiplier| 96623, sharp to ~1e-12
  -> assets/zoom_neon_dust.mp4. NOTE the period-79 center came out the same to 15
  digits in double and df64 -- at the EXACT periodic point the orbit is bounded
  so there's no error amplification; the multiplier only blows up precision when
  you're off the point. The df64 solver still matters as insurance for very high
  periods, and was cheap via the template.
- **df64 deep zoom into a JULIA set needs the constant c in df64 too.** `c` was
  being passed to deep.frag as a single float (`vec2(uJuliaC.x, 0.0)`), so its
  ~1e-7 rounding error was injected every iteration and capped Julia deep zooms
  at ~1e-7 -- looked exactly like a bad center (flat single-hue frames at depth).
  Fixed by passing `uJuliaCX`/`uJuliaCY` as df64 hi/lo pairs (`setUniformDF`).
  Mandelbrot was never affected because there `c = center + offset` is already
  df64. Symptom to remember: Julia deep zoom flattens ~1e-7 while Mandelbrot is
  fine to ~1e-12 => suspect a low-precision constant in the iteration, not the
  center or the renderer.
- **`--deep` (deep zoom)**: `deep.frag` iterates in emulated double-float (df64,
  a hi+lo float pair) instead of float, holding sharp to ~1e12 (verified) before
  it softens near df64's ~14 digits. Quadratic Mandelbrot/Julia SAC path only.
  THE gotcha: df64's error-free transforms collapse to plain float unless the
  driver is forbidden from fusing/reordering them. That needs the GLSL `precise`
  qualifier, which is 4.00+, so the GL context is requested at **4.1** (max on
  macOS; the 330 shaders still compile under it). Without `precise` the deep
  path pixelates identically to float -- looks like a no-op. True unbounded
  precision would need perturbation + a bignum reference orbit (not done).
- **Buddhabrot tone mapping must normalize to a percentile, not the max.** The
  density has a long tail -- a few pixels in attractor regions are orders of
  magnitude hotter than the body, so dividing by the true max crushes the whole
  image to near-black (this looked like a total failure until fixed). The 99.9th
  percentile (`nth_element`) as the white point clips those outliers and exposes
  the structure. Mandelbrot Buddhabrot is mirror-symmetric about the real axis,
  so when `center_y==0` it samples only the upper half and splats each orbit
  point AND its y-reflection (verified exact: mean byte diff ~0.01). Julia
  Buddhabrot is NOT mirror-symmetric (it has 180-deg rotational symmetry), so
  that path correctly does no mirroring -- a Julia buddhabrot *looks* lopsided
  and that's right, don't "fix" it. Both are quadratic-only.
- **Explorer plane<->pixel mapping mirrors the shader** (`uv =
  (frag-0.5*res)/res.y`, `plane = center + uv*2*scale`), but cursor coords are
  top-left origin / y-down, so the y term is negated. Zoom-to-cursor keeps the
  complex point under the cursor fixed by recomputing center after scaling. The
  window is non-resizable and `present()` blits the fixed-size FBO to the
  (possibly Retina, 2x) default framebuffer with GL_LINEAR -- no flip on screen
  (both are GL bottom-up; readPixels still flips for PNG files).
- Video uses lower default SSAA (2) than stills (4) because it renders hundreds
  of frames. x264 + yuv420p needs even dimensions (handled in `runVideo`).
- **Rendering is GPU-compute-bound, dominated by iteration count, not I/O.**
  Measured: a deep seahorse frame costs ~10× a shallow one at equal res because
  near-boundary pixels run the full `max_iter`. So the real speed levers are
  `-i` and `--ssaa`, nothing else. Encoding is ~free (x264 keeps up trivially),
  so there's no point parallelizing it; `runVideo` does overlap readback+encode
  on a writer thread, but the GPU is the serial bottleneck and `glReadPixels`
  must sync on the GL thread, so the win is small. `-i 2000` is visually
  identical to `-i 4000` (PSNR ~73 dB) on detail-rich zooms — don't over-budget
  iterations. The shader skips derivative (`dz`) tracking unless glow/falloff
  are on (`useDE`); lossless, but minor next to the stripe `atan2`+`sin`.

## Conventions

- Match the surrounding style: namespaced in `fractal`, comments explain *why*.
- Keep GL strictly inside `renderer.cpp`. Everything else stays testable.
- Default parameter values are tuned to look good with a bare `fractal render`;
  if you retune defaults, re-check the gallery in `assets/` and update README.

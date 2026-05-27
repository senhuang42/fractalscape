# CLAUDE.md

Guidance for working in this repo. Keep it current as the project evolves.

## What this is

A C++/OpenGL CLI that renders Julia/Mandelbrot fractals as stills (PNG) and
seamless loop videos (MP4). GPU does the real work via GLSL fragment shaders;
the CLI just builds a config, drives the renderer, and writes files.

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
  `ffmpeg`. Per-frame animation lives in `frameConfig()`.

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
  highlight, and leave `saturation` at the default 1.3.
- **Palettes** are dark→bright ramps in `palette.cpp`; restrained ones keep
  detail coherent, wider-gamut ones (sunset/neon/prism/etc.) are vibrant.
- **Zoom videos**: shader is 32-bit float -> pixelates past ~1e4×. Keep the
  target on a detail-rich exterior point at ALL scales (probe with stills) or
  the path crosses black set-interior bodies. -0.7453,0.1127 works to ~0.0013.
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

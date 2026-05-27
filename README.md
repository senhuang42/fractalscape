# fractalscape

A GPU command-line renderer for Julia and Mandelbrot fractals — stills (PNG) and
seamless loop videos (MP4). The math runs in a GLSL shader, so a 4K still at 16×
supersampling comes back in under a second on Apple Silicon. The coloring —
stripe-average relief over an escape-time layer — is the interesting part; see
[how it works](#how-it-works).

![grayscale stripe-average spiral](assets/hero_noir.png)

## Build

macOS / Apple Silicon, clang + GLFW (+ ffmpeg for video):

```sh
brew install glfw ffmpeg
make            # -> ./fractal
make test       # GL-free unit tests
```

Links Apple's system OpenGL (runs on Metal) + GLFW — no GL loader needed.

## Usage

```sh
fractal render [options]   # still -> PNG
fractal video  [options]   # animation -> MP4
fractal help               # every flag, palette, and preset
```

Start from a one-word **preset** and override anything with a flag:

```sh
fractal render -P frostbite -o spiral.png
fractal render -P acid-swirl --size 2000x2000 -o trippy.png
fractal render -P frostbite --cre -0.8 -p ocean -o custom.png   # presets are just defaults
```

![preset gallery](assets/presets.png)

## Animation

`fractal video` has four modes — three are seamless loops (Spotify Canvas,
wallpapers, visualizers):

| mode | what moves | example |
|---|---|---|
| `rotate` | Julia constant orbits → shape morphs | [loop_rotate.mp4](assets/loop_rotate.mp4) |
| `cycle` | palette sweeps one full cycle | [loop_cycle.mp4](assets/loop_cycle.mp4) |
| `spin` | a kaleidoscope axis turns a full revolution | [loop_spin.mp4](assets/loop_spin.mp4) |
| `zoom` | continuous dive (one-shot, not a loop) | [zoom_seahorse.mp4](assets/zoom_seahorse.mp4) |

```sh
fractal video -P cover-mandala --mode spin -d 8 -o spin.mp4
```

## Covers & the design layer

Off-by-default toggles turn a render into a designed piece: `--kaleido N`
(mirrored-wedge mandala), `--aberration` (RGB split), `--scanlines`,
`--vignette`, `--grain`. Four square `cover-*` presets wire them up on the neon
`vice` palette — render at `--size 3000x3000` for streaming/print.

![cover modes](assets/covers.png)

Mix them onto anything, e.g. `fractal render -P ember-seahorse --kaleido 12
--vignette 0.4`. ([assets/test_cover.png](assets/test_cover.png) is the
`cover-hero` location with the `cover-glitch` treatment.)

## How it works

Two layers, combined per pixel:

1. **Iteration layer** — escape-time, mapped so the fast-escaping exterior goes
   dark and slow-escaping filaments stay bright (the structure and the tendrils).
2. **Stripe Average Coloring** — averages `½+½·sin(s·arg z)` along the orbit and
   interpolates by the fractional escape count to kill banding, giving smooth 3D
   relief with no actual lighting (Härkönen 2007, via Phil Thompson's
   [writeup](https://philthompson.me/2023/Stripe-Average-Coloring.html)).

The iteration layer gates the stripe layer so empty gaps stay black;
`--stripe-color 0` or `--color-density 0` isolates either one. Bloom (on by
default), optional Blinn-Phong height-field lighting, linear-light
supersampling, and an iq
[distance estimate](https://iquilezles.org/articles/distancefractals/) round it
out. Palettes are dark→bright ramps (`fractal help` lists them); deep zooms
pixelate past ~10,000× since the shader is 32-bit float.

## Credits

Stripe Average Coloring — Jussi Härkönen (2007), via [Phil Thompson](https://philthompson.me/2023/Stripe-Average-Coloring.html).
Distance estimation — [Iñigo Quilez](https://iquilezles.org/articles/distancefractals/).
[stb_image_write](https://github.com/nothings/stb), [GLFW](https://www.glfw.org/), [ffmpeg](https://ffmpeg.org/).

## License

MIT — see [LICENSE](LICENSE).

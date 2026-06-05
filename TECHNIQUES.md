# Techniques

A catalogue of every rendering and coloring technique in fractalscape: what it
is, the math, the flags that drive it, where it came from, and a preset that
shows it off. Shader implementations live in `shaders/fractal.frag` (mirrored
on the CPU in `src/fractal_math.h`); flags in `src/cli.cpp`.

Notation: the orbit is `z_{n+1} = z_n^2 + c` (or a formula variant), `mu` is
the smooth iteration count, `N` the escape iteration, bailout defaults to
10000 (large on purpose — the average colorings and field-line techniques
need it).

## Iteration

| Technique | Math | Flags | Source |
|---|---|---|---|
| Escape time | iterate until `\|z\| > bailout` or max_iter | `-i`, `--bailout` | classic |
| Smooth iteration count | `mu = n + 1 - log(log\|z\|/log B)/log p` — continuous bands, no stair-stepping | (always on) | [Vepstas / classic](https://linas.org/art-gallery/escape/escape.html) |
| Formula variants | Burning Ship `(\|Re z\|+i\|Im z\|)^2+c`; Tricorn `conj(z)^2+c`; Phoenix `z^2+c+p·z_prev`; arbitrary power `z^p+c` | `--formula`, `--exponent`, `--phoenix-p` | classic |
| Newton fractal | `z -= (z^3-1)/(3z^2)`, colored by root basin + convergence speed | `--formula newton` | classic |
| Deep zoom (df64) | iteration in emulated double-float (hi+lo float pairs, error-free transforms guarded by GLSL `precise`) — sharp to ~1e12 | `--deep` | [Andrew Thall](https://andrewthall.org/papers/df64_qf128.pdf) |
| Exact zoom centers | Newton-refine a guess to a Julia repelling periodic point (`f^p(z)=z`) or Mandelbrot nucleus (`f^p(0)=0`), in CPU double-double | `fractal center` | [mathr.co.uk](https://mathr.co.uk/blog/) |
| Buddhabrot / Nebulabrot | density of *escaping* orbit trajectories, CPU scatter, 99.9th-percentile white point; Nebulabrot = three iteration thresholds into R/G/B | `fractal buddhabrot`, `--nebula` | [Melinda Green](https://superliminal.com/fractals/bbrot/) |

## Coloring — the two-layer core

The default image is two gated layers (see the color block in `fractal.frag`):

| Technique | Math | Flags | Source |
|---|---|---|---|
| Iteration layer (exp) | `iterS = 1 - exp(-mu · density)` — fast-escape exterior dark, slow filaments bright; a `smoothstep` gate keeps gaps black under the overlay | `--color-density` | philthompson.me overlay idea |
| Iteration layer (log) | `iterS = fract(log(mu) · density + offset)` — cyclic band per e-fold of escape time; gateless, so bands run all the way out | `--log-iter` | Maths Town aesthetic |
| Stripe Average Coloring (SAC) | average of `½+½ sin(s·arg z_n)` over the orbit, interpolated incl/excl the last point by `frac(mu)` (the de-banding trick — non-negotiable) | `--stripe-color`, `--stripe-freq`, `--stripe-contrast` | [Härkönen 2007](https://archive.org/details/j-harkonen-on-smooth-fractal-coloring-techniques-masters-thesis-2007-hi-res) eq 4.10 |
| Triangle Inequality Average (TIA) | average of where `\|z_{n+1}\|` falls in `[\|\|z_n^2\|-\|c\|\|, \|z_n^2\|+\|c\|]`, same de-banding — flame plumes reaching out of the set | `--relief tia` | Härkönen 2007 eq 4.4 |
| Curvature average | average of `\|arg((z_n - z_{n-1})/(z_{n-1} - z_{n-2}))\|/π` — the orbit path's turning angle; angular marbled ridgelines | `--relief curvature` | Härkönen 2007 eq 4.8 |
| Layer combination | `col = mix(iterCol, stripeCol · gate, stripe_color)` — the gate is what keeps gaps black while structure carries full relief | `--stripe-color` 0..1 | this repo |

Presets: `noir-spiral` (SAC), `flame-mandel` (TIA), `marble-vein` /
`flame-curl` (curvature), `mathstown-classic` (log-iter).

## Coloring — orbit statistics

All of these add an extra term to the stripe-palette sample position, so they
compose with each other and with SAC/TIA/curvature:

| Technique | Math | Flags | Source |
|---|---|---|---|
| Escape angle | `arg(z_N)/2π` added to the palette coord | `--angle-color` | classic ("decomposition-continuous") |
| Orbit traps | min over the orbit of a shape's closed-form distance: point `\|z-t\|`; cross `min(\|dx\|,\|dy\|)`; circle `\|\|d\|-r\|`; astroid `\|\|dx\|^⅔+\|dy\|^⅔-r^⅔\|`; diamond `\|\|dx\|+\|dy\|-r\|`; hyperbola `\|dx·dy-r\|`; waves `\|\|dy\|-r+sin(dx·f)·r/2\|`; log-spiral `\|sin(θ+f·log r)\|·r` | `--trap-color`, `--trap-shape`, `--trap-x/y`, `--trap-radius`, `--trap-freq` | [Ultra Fractal standard.ucl](https://www.ultrafractal.com/help/coloring/standard.html) |
| Pickover stalks | `exp(-f · min_n min(\|Re z\|, \|Im z\|))` — how closely the orbit grazed the coordinate axes | `--stalk-color`, `--stalk-freq` | [Pickover](https://en.wikipedia.org/wiki/Pickover_stalk) |
| Gaussian integer | `exp(-f · min_n \|z_n - round(z_n)\|)` — closest pass to the integer lattice; crystalline grid texture | `--gauss-color`, `--gauss-freq` | Ultra Fractal "Gaussian Integer" |
| Slope-angle sheen | `atan2(∂y log mu, ∂x log mu)/2π` as a hue shift — the *direction* of the escape-time gradient; iridescent oil film | `--sheen` | [mathr fake-DE colouring](https://mathr.co.uk/blog/2014-12-13_faking_distance_estimate_colouring.html) (hue-routed) |
| Binary decomposition | darken cells where `floor(2^k·arg(z_N)/2π) + floor(mu)` is odd — cell edges trace external rays × equipotentials | `--decomp`, `--decomp-strength` | Peitgen & Richter, *The Beauty of Fractals*; [mrob Mu-Ency](http://www.mrob.com/pub/muency/binarydecomposition.html) |

Presets: `stargate` / `starcross` / `whirlpool` / `neon-astroid` (traps),
`stardust` (stalks), `crystal-court` (gauss), `oil-slick` (sheen),
`peitgen-grid` / `ray-stain` (decomposition).

## Coloring — set interior

The body of the set doesn't have to be flat black (`--interior`, sampling
`--inside-palette`):

| Mode | Math | Looks like | Source |
|---|---|---|---|
| `flat` | the `--inside` color | classic | — |
| `sac` | the orbit's accumulated stripe value | relief continuing into the body | this repo |
| `bof60` | `sqrt(min_n \|z_n\|)` — closest approach to the origin | glowing nested "embryo" shapes | *Beauty of Fractals* p.60 (FractInt `inside=bof60`) |
| `bof61` | `argmin_n \|z_n\|` — *which* iteration came closest, golden-ratio-spaced into the gradient | flat atom-domain cells keyed to period | *Beauty of Fractals* p.61 (FractInt `inside=bof61`); [mathr atom domains](https://mathr.co.uk/web/m-atom-domains.html) |
| `expsmooth` | `Σ exp(-1/min(\|z_n-z_{n-1}\|, \|z_n-z_{n-2}\|))` — exponential smoothing, convergent form (the `z_{n-2}` term handles period-2 attractors) | smooth glow, brightest where orbits settle slowest | Ron Barnett / Ultra Fractal "Exponential Smoothing" |

Presets: `interior-bloom` (sac), `ember-eyes` (bof60), `atom-cells` (bof61),
`glass-lake` (expsmooth). Placement rules (Mandelbrot vs Julia, contrast
clamps) are in CLAUDE.md's gotchas.

## Lighting & distance estimation

| Technique | Math | Flags | Source |
|---|---|---|---|
| Blinn-Phong height-field | displayed luminance as height; screen-space gradient (`dFdx/dFdy`) → normal → diffuse + specular | `--shading`, `--specular`, `--light-angle`, `--light-height`, `--shininess`, `--height-scale` | classic emboss |
| Slopes | same lighting but the height field is `log(mu)` (true depth) — survives cyclic/posterized palettes | `--slopes`, `--slopes-spec` | Maths Town "Slopes" |
| Distance estimate | `d = \|z\|·log\|z\|/\|dz\|` with `dz_{n+1} = 2 z_n dz_n (+1)` — only tracked when consumed | (feeds glow/falloff) | [Inigo Quilez](https://iquilezles.org/articles/distancefractals/) |
| Filament glow | `exp(-d_px²/2)` palette-tinted at the boundary | `--glow` | iq |
| Falloff | `exp(-d_px · falloff)` fade of the exterior toward the void | `--falloff` | iq |

## Palettes & grading

| Technique | What it does | Flags |
|---|---|---|
| Dark→bright ramps | restrained gradients keep fine detail coherent (a hue wheel turns it into noise) | `-p`, custom hex lists |
| Cyclic gradients | close the loop so relief sweeps the whole gradient — the key to multi-hue images | `--cyclic` |
| Oklab interpolation | perceptually-uniform stop lerp; opposed hues stay saturated instead of dipping through grey | `--interp oklab` |
| Dual palettes | iteration layer (field) and stripe layer (structure) sample *different* gradients | `--stripe-palette` |
| Interior palette | separate gradient for `--interior` modes | `--inside-palette` |
| Posterize | quantize the gradient sample to N flat bands — screen-print / stained glass | `--posterize` |
| Grade | saturation / gamma / black-point crush (empties read as true black) | `--saturation`, `--gamma`, `--black-point` |

## Hybrid Buddhabrot accent (four modalities)

A CPU Buddhabrot pre-pass at the same view, overlaid on the escape-time
render — orbit *density* is information no per-pixel computation can produce:

1. **Additive accent** — tinted wisps over the render (`--nebula-accent`, `--nebula-color`)
2. **Hue shift** — density modulates the stripe palette coord (`--nebula-hue-shift`)
3. **Bloom mask** — density drives the bloom bright-pass, wisps glow regardless of brightness (`--nebula-bloom`)
4. **RGB lifetime** — three-threshold Nebulabrot added per-channel (`--nebula-rgb`)

Presets: `nebula-ghost`, `hue-tide`, `dragon-storm`, `lifetime-spectrum`.

## Post & design layer

| Technique | Math / note | Flags |
|---|---|---|
| Gamma-correct SSAA | render at N× and average in linear light (`downsample.frag`) | `--ssaa` |
| Bloom | bright-pass + separable Gaussian, screen-composited | `--bloom`, `--bloom-threshold` |
| Kaleidoscope | fold the screen coord into N mirrored wedges *before* the plane map | `--kaleido`, `--kaleido-angle` |
| Chromatic aberration | radial RGB split in pixels (needs ~20px to read on a centered subject) | `--aberration` |
| Vignette / grain / scanlines | corner darkening, additive noise, fixed-count CRT lines | `--vignette`, `--grain`, `--scanlines` |

## Animation

| Mode | What moves |
|---|---|
| `rotate` | Julia constant orbits the origin (`c = r·e^iθ`), seamless |
| `zoom` | exponential dive toward a target (pair with `fractal center` and `--deep`) |
| `cycle` | palette phase sweeps one full cycle (forces cyclic) |
| `spin` | kaleidoscope rotates one symmetry segment, seamless |
| `--color-cycles N` | overlay N palette sweeps on any mode |

## Primary sources

- J. Härkönen, *On Smooth Fractal Coloring Techniques* (2007) — the average-coloring framework (SAC, TIA, curvature) and the de-banding interpolation
- H.-O. Peitgen & P. Richter, *The Beauty of Fractals* (1986) — bof60/bof61, binary decomposition
- [Ultra Fractal standard library](https://www.ultrafractal.com/help/coloring/standard.html) — trap shapes, Gaussian integer, exponential smoothing
- FractInt / [iterated-dynamics](https://github.com/LegalizeAdulthood/iterated-dynamics) — `inside=`/`outside=` modes
- [Inigo Quilez](https://iquilezles.org/articles/) — distance estimation, smooth iteration
- [Claude Heiland-Allen (mathr.co.uk)](https://mathr.co.uk/blog/) — atom domains, slope shading, deep-zoom math
- [Maths Town](https://www.youtube.com/@MathsTown) — log-iter banding and Slopes aesthetics

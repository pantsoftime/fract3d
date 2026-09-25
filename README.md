# Fract3D

A GPU fractal explorer for Linux, built as a 3D homage to **Fractint**. You can fly through Mandelbulbs, Mandelboxes and Kleinian caves in real time, or switch to a path tracer and let the image refine to photographic quality. There is also a Classic 2D mode with VGA palettes and color cycling. Every fractal includes a short lesson on the math behind it.

| | | |
|---|---|---|
| ![](docs/images/07-mandelbulb-path-traced.jpg) | ![](docs/images/11-kleinian-caves.jpg) | ![](docs/images/15-kifs-gem.jpg) |
| ![](docs/images/09-mandelbox-cathedral.jpg) | ![](docs/images/10-menger-crystal.jpg) | ![](docs/images/12-crater-lake.jpg) |
| ![](docs/images/01-fractint-1990.jpg) | ![](docs/images/02-seahorse-valley.jpg) | ![](docs/images/13-fractint-3d-1990.jpg) |

## Build & run

Needs a C++20 compiler, CMake, GLFW 3 and libepoxy, plus an OpenGL 4.6 GPU. Dear ImGui and stb are vendored in `third_party/`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/fract3d
```

To install for your user, including an app-menu entry:

```bash
cmake --install build --prefix ~/.local
```

## What's inside

**3D fractals** (ray marched with distance estimators, all in `fractals/*.glsl`):
Mandelbulb, Mandelbox, Menger sponge, Sierpinski tetrahedron, Kaleidoscopic IFS, Quaternion Julia (a 3D slice of a 4D object), Apollonian cathedral, Pseudo-Kleinian, and an escape-time landscape that turns any 2D Mandelbrot view into terrain.

**Two renderers.** *Real-time* uses soft shadows, ambient occlusion, fog and glow, and adapts its resolution to hold your target frame rate. *Path traced* adds global illumination, glossy reflections and depth of field. Both keep refining (anti-aliasing and noise) while the camera is still.

**Classic 2D mode.** Mandelbrot, Burning Ship, Tricorn, Multibrot, Newton, Phoenix, Lambda and Magnet I:
- the exact VGA power-on palette with Fractint-style integer color bands, or smooth coloring
- free color cycling (only palette indices are stored, just like rotating the VGA DAC)
- right-click any point for its Julia set
- an orbit viewer that draws z0, z1, z2 ... under the cursor
- automatic fp64 for deep zooms (to about 10^13x)
- **Lift into 3D** turns the current view into a landscape

**Learning.** The Learn panel has three tabs:
- **This fractal:** a lesson on the current fractal
- **Concepts:** fractal dimension, escape time, distance estimation, IFS and folding, orbit traps, precision, path tracing, history
- **Tour:** a guided walk through 15 curated views

Every slider has a tooltip explaining what it does.

**Nostalgia.** A "Fractint (DOS blue)" UI theme, a retro filter (VGA 256 or EGA 16 colors with ordered dithering, chunky pixels, scanlines), Fractint `.MAP` palette files, and `.par` parameter files. Every screenshot saves a `.par` beside it, so you can recreate the view later.

## Controls

| 3D | |
|---|---|
| Left drag | orbit around the target |
| Right drag | look around |
| Middle drag / Shift+left drag | pan |
| Wheel | zoom toward the target |
| W A S D, Q/E | fly (speed adapts to the distance to the surface); Shift = 4x |
| Double-click | orbit around the clicked point |
| F | focus depth of field at screen center |
| P | toggle path tracing |
| R | reset view |
| 1-9 | switch fractal |

| 2D | |
|---|---|
| Left drag / wheel | pan / zoom at cursor |
| Right-click or Space | Julia set of the point (and back) |
| O | show orbit under cursor |
| B | Fractint bands vs smooth color |
| + / - | double / halve max iterations |

| Everywhere | |
|---|---|
| Tab | hide/show UI |
| M | switch 3D / Classic 2D |
| C | color cycling |
| N / Shift+N | next / previous tour stop |
| L | Learn panel |
| F1 | help |
| F11 | fullscreen |
| F12 | screenshot (+ .par) to ~/Pictures/fract3d |
| Ctrl+S | save view as .par |

## Write your own fractal

Drop a `.glsl` file into `fractals/`. The header comments become the UI:

```glsl
// @name My Fractal
// @category Folding
// @camera 2 1.5 -3   0 0 0            (camera position, then target)
// @render stepFactor=0.9 detail=0.5   (ray marching hints)
// @look palette="Gold" colorScale=1.2 floor=1 floorY=-1.2
// @param float scale = 2 [1, 3] "Scale" -- tooltip text
// @param vec3 offset = (1, 1, 1) [0, 2] "Offset" -- ...
// @param choice fold = 0 {Tetrahedral, Octahedral} "Symmetry" -- ...
// @about
// # A heading
// Lesson text shown in the Learn panel.
// $ formula lines render in monospace
// * bullet points
// @end

float DE(vec3 p, inout vec4 trap) {
    // return a distance estimate; set trap.x/.y (orbit traps) and .z (iterations) for coloring
}
```

Parameter types are `float`, `int`, `bool`, `vec2`, `vec3`, `vec4`, `color` and `choice`. Add `log` after the range for a logarithmic slider. Helpers such as folds, rotations, and complex and quaternion math are in `shaders/common.glsl`. Save the file and the app reloads it instantly; compile errors appear on the Fractal tab. The same goes for editing anything in `shaders/`.

## Command line

```
fract3d [--fractal KEY] [--par FILE] [--2d] [--pt|--rt] [--theme modern|fractint]
fract3d --par presets/07-mandelbulb-path-traced.par --render out.png --size 3840x2160 --samples 1024
```

`--render` renders offscreen and exits, which is handy for wallpapers.

## Layout

```
src/        C++: app loop & input, renderer, UI, fractal file parser, PAR files, palettes
shaders/    raymarch.frag (3D), classic2d.frag, display.frag (tonemap/palette/retro), common.glsl
fractals/   one self-describing .glsl per 3D fractal
presets/    the guided tour (.par)
docs/       concepts.txt and classic.txt (Learn panel content)
```

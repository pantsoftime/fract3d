# Resolution of the 2026-09-25 peer review

Every item in [REVIEW-2026-09-25.md](REVIEW-2026-09-25.md), in the order its §5 recommended,
with the commit that addresses it. Baseline: `a9411d8`.

## Priority order (§5)

| # | Item | Commit | Notes |
|---|---|---|---|
| 1 | 1.1 2D render could hang the GPU | `daf2db4` | Went further than tiling: 2D is now a compute shader with per-pixel orbit state, so each dispatch runs a bounded chunk of *iterations*, not just rows. 4M iterations at 1080p: frames stay near budget (was 240+ ms each, minutes before tiling). Also bands expensive 3D samples. |
| 2 | 1.2 PAR validation | `74e24b6` | `sanitize()` for render/2D settings, camera and fractal params (NaN/Inf, ranges, enum indices). |
| 2 | 1.3 FBO checks, GL debug output | `daf2db4`, `74e24b6` | Max texture size + completeness checks; impossible renders exit 1; `--gl-debug`, always on in Debug builds. |
| 3 | 1.4 Poster snapshot | `a2adfa7` | Render settings, de-animated params, palette, cycle offset and the PAR text are frozen at start. |
| 4 | 2.1 Memberwise signatures | `4c69c82` | `settings.h` field table drives signatures, PAR I/O, `@look` and perf prefs; bools legal. |
| 4 | 1.8 `classic.fp64` persisted | `4c69c82` | |
| 5 | 2.5 Git + tests | `a9411d8`, `26a2ab8` | CTest suite (now 57 tests), golden images, imgdiff tool, ASan/UBSan preset. The sanitizer run immediately found UB (inverted `std::clamp` bounds) - fixed. |

## Remaining items

| Item | Commit | Notes |
|---|---|---|
| 1.5 Wayland fullscreen / monitor | `60ebc8c` | No `glfwGetWindowPos` on Wayland; overlap-based monitor on X11; explicit monitor choice (saved). |
| 1.6 `rotAxis` "transposed" | `60ebc8c` | **Not a bug in the matrix** - verified numerically against Rodrigues' formula; the old *comment* was wrong and the review took it at face value. Comment fixed; Sierpinski uses +angle. |
| 1.7 `system()` for xdg-open | `60ebc8c` | `posix_spawnp` with argv, child reaped. |
| 1.9 Lift keeps palette | `60ebc8c` | Flatten carries color density back. |
| 1.10 (all sub-items) | `60ebc8c`, `4c69c82` | Animated refocus, two-sided floor seen by the probe, dead `minD`, alloc-free uniform lookups (in `daf2db4`), keep-lighting toggle, derived look reset, disabled Lift, flySpeed clamp, empty Ctrl+S name, zoom drift, fps. |
| 2.2 God object | `80d9f64` | `ViewState` / `Session` / `UiState`. |
| 2.3 Hidden statics | `80d9f64` | All moved to members; docs/presets hot-reload. |
| 2.4 Dead code | `80d9f64` | |
| 3.1 Sampling | `c551981` | Owen-scrambled Sobol + blue-noise shift: 1.2-1.6x lower RMSE (≈2.6x fewer samples at equal error), verified unbiased. |
| 3.2 Glossy NEE | `c551981` | |
| 3.3 Landscape overshoot | `c551981` | Bisection refinement on overshoot. |
| 3.4 (all sub-items) | `daf2db4`, `4c69c82`, `c551981` | Parallel shader compile + warm-up, retro 3D LUT, R32F+R8 iteration buffer, fp64 threshold, cycling signature, fixed step reference, fog/sun, idle sleep. |
| 4.1 Formula files | `5636f29` | `.frm` parser/transpiler, 12 formulas, editor, 2D + 3D landscape. |
| 4.2 Scanline reveal | `daf2db4` | Part of the resumable renderer. |
| 4.3 Julia inset | `7fe159b` | |
| 4.4 Perturbation deep zoom | `7dbcd37` | MPFR reference orbit, float-mantissa/int-exponent deltas, rebasing; to 10^290x. |
| 4.5 Camera paths + video | `72d06c9` | Catmull-Rom over the settings table, ffmpeg (x264/NVENC). |
| 4.6 Undo/history | `c664e00` | |
| 4.7 Cursor capture + gamepad | `e0c85c3` | Gamepad path untested (no hardware here). |
| 4.8 PAR inside PNG | `aa3b5d3` | iTXt chunk; drag-and-drop. |
| 4.9 2D coloring modes | `cb8f22f` | |
| 4.10 Palette editor | `3f5587f` | |
| 4.11 Session autosave | `c664e00` | |
| 4.12 Fullscreen on current monitor | `60ebc8c` | See 1.5 (Wayland can't report window position, so it's a user choice there). |

## Bugs found beyond the review

- **Shader hot-reload never worked** (`5636f29`): `file_time_type{}` is libstdc++'s file-clock
  epoch in the year 2174, so the "newest shader time" started later than every real file.
- **Path-tracer Fresnel bug** (`c551981`): glossy Fresnel was computed from the outgoing
  direction, clamping it to 1.0; every glossy bounce reflected nearly all energy.
- **GL calls after context destruction** at exit for several buffers (`daf2db4`, `7fe159b`).
- **Undefined behavior** from inverted `std::clamp` bounds (found by the new ASan/UBSan preset).
- Formula parser missing the `pixel` built-in, and stdout buffering hiding status lines when piped
  (both found and fixed while building 4.1).

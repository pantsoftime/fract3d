# Resolution of the second-pass review (r2)

Every item in [REVIEW-2026-09-25-r2.md](REVIEW-2026-09-25-r2.md), in the order its §6
recommended, with the commit that addresses it. Baseline: `f0dff01` (the review itself).
Final state: 86 CTest tests (was 57) pass in the default and ASan/UBSan builds; the CI
steps pass with GCC and Clang, warnings as errors.

## §1 Bugs confirmed by experiment

| Item | Commit | Resolution |
|---|---|---|
| 1.1 Poster dialog during video export; its Cancel hung the export | `f5cb055` | Video frames don't show the dialog; its Cancel would cancel the export. Screenshot-verified. |
| 1.2 Deep-to-shallow camera path ends at the wrong center | `f5cb055` | PAR loads reset the 2D settings and the exact center; keyframes sync their own centers; the path ends exactly on its last keyframe. `--path-time` + `path.deep-to-shallow-*` tests, and a mid-path self-test that fails without the fix. |
| 1.3 "Fractint bands" did nothing for custom formulas | `c8a8d0c` | The transpiler reads the escape radius from `\|z\| <= N` / `cabs(z) < R`; `; @bailout`, `; @power`, `; @smooth = 0` annotations override. Formulas with other tests say in the panel that they stay banded. |
| 1.4 Deep zoom vs fp64: 4.3% of pixels far off | `3ab5889` | **The review's diagnosis was half right.** Measured against exact MPFR arithmetic (new `tools/itercheck`): that view is ill-conditioned (moving its center 1e-17 changes 3.4% of the exact result) and fp64 itself was 4.4% wrong. A double reference alone changed nothing (6.21% vs 6.20%). The real problem was float *deltas* over long orbits: 20.8% wrong at 1e-20 against a 0.42% floor. Deltas and the reference are now doubles (no separate exponent needed down to 1e-290, which keeps the cost at 3x rather than 10x): 0.39% at 1e-20 (at the floor), 0.09% at 5e-10 (45x closer to exact than plain fp64). |
| 1.5 Path-tracer noise repeats every 64 px | `5d50cbf` | Per-tile Owen seed and blue-noise offset. Correlation at 64 px: +0.18 → +0.03; RMSE unchanged within noise. |

## §2 Bugs from reading

| Item | Commit | Resolution |
|---|---|---|
| 2.1 Idle sleep ignored the reference worker and Julia inset | `f5cb055` | |
| 2.2 History filled by playback; animation flags lost | `f5cb055` | Also: parsing keyframes no longer resets other fractals' parameters. |
| 2.3 Keyframe parsing recompiled the formula per keyframe | `f5cb055` | `compileFormula` skips identical requests. |
| 2.4 `loadParText` kept the previous 2D settings | `f5cb055` | 2D settings reset. Formula and palette *editor* contents are deliberately kept: they're the user's work, and saved PARs always include them when they're used. Also: formula keys now apply before settings (a PAR's center beats a formula's `@view`), landscapes that use the custom formula now save it, and loading a PAR keeps the performance preferences (it reset them). |
| 2.5 Poster snapshot leaks (p1-p3, formula recompiles) | `a9318a8` | Formula parameters travel in `View3D`, and are in the 3D signature now; a recompile restarts a render that uses the formula. |
| 2.6 Video finish/cancel blocked the UI | `a9318a8` | Cancel terminates ffmpeg; finishing is polled. `video.cancel` test (`--cancel-after-frames`). |
| 2.7 Right-click Julia on custom formulas lost the view | `a9318a8` | Formulas can name a Julia partner (`; @julia = Name`, c = p1): Mandel→Julia, FnMandel→new FnJulia. Others say so. Self-tested. |
| 2.8 Two `glFinish` per 2D pass | `a9318a8`, `375f443` | Timer queries read back a frame later. **This introduced a regression** that the BLA benchmarking found: pass length was planned from unmeasured or unrepresentative estimates, a single pass could run for seconds, and the driver then dropped the work (a 300000-iteration view at 3840x2160 came back black). Fixed in `375f443`: pass length changes only on fresh measurements and is capped by the measured cost of a band's first pass. Median frame 9.1 ms (budget 13.3) on a 4M-iteration view. |
| 2.9 No "compiling" indicator | `a9318a8` | HUD, menu bar, Fractal tab. |
| 2.10 Null dereference in Flatten | `a9318a8` | |
| 2.11 Copyable GL wrappers | `a9318a8` | |
| 2.12 Function statics back in ui.cpp | `a9318a8` | |
| 2.13 Path interpolation | `1021e46` | Sun unwraps; counts interpolate (`maxIter` in log space) via a new `kCount` settings flag; per-segment ease in/out, saved in `.f3dpath`. Self-tested. |
| 2.14 Small ones | `f5cb055`, `a9318a8`, `1021e46` | Undo keeps unrecorded changes for Redo; gamepad polling waits 50 ms; deep coordinates in the HUD and Classic panel come from the exact center; the orbit viewer says when it's too deep; the Learn panel explains the 10^290x limit; odd video sizes are rounded; SIGPIPE restored; duplicate includes. |

## §3 Rendering notes

| Note | Commit | Resolution |
|---|---|---|
| Sampler, perturbation math, glossy NEE normalization | — | Confirmed correct (recorded in the review). |
| `ldexp` overflow in the perturbation kernel | `3ab5889` | Moot: double deltas need no exponent. |
| Glossy lobe mismatch | `1021e46` | Documented in the shader as deliberate. |
| Orbit viewer for custom formulas | `511336e` | Via the new CPU interpreter. |
| Slow PNG writing | `1021e46` | Sub filter, zlib 5: an 8K render finishes 1.2 s sooner, files 3-6% smaller (measured; the review's "halves it" was optimistic). |

## §4 Tests and repository

| Item | Commit | Resolution |
|---|---|---|
| 4.1 Loose deep-zoom test | `3ab5889` | `deep.exact-*` compare with exact arithmetic (tolerances just above each view's conditioning); `deep.agrees-with-fp64` at a well-conditioned view with imgdiff's default tolerance. |
| 4.2 Round-trip coverage | `eddf7ad` | Gradient, cosine, formula+fn, landscape formula, animation, deep center. One of them rendered black (too few iterations) and so compared black with black: fixed in `1021e46`. |
| 4.3 Path test | `f5cb055` | `--path-time`; endpoint tests. |
| 4.4 Oversize test accepted any failure | `eddf7ad` | Now checks the message - and it **was** passing for the wrong reason (the hidden window was created at the render size and failed first). |
| 4.5 CI | `eddf7ad` | GitHub Actions: GCC + Clang, `-Werror`, GPU-free tests (label `nogpu`). Verified in the same Arch container locally. There's no remote yet, so it hasn't run on GitHub. |
| 4.6 Vendored ImGui | `eddf7ad` | 8.3 MB → 3.9 MB; update recipe in `third_party/README.md`. |
| 4.7 Test nits | `3ab5889` | No more `sh -c`. |

## §5 Enhancements

| Item | Commit | Notes |
|---|---|---|
| 5.1 Import Fractint .PAR files | `ca15597` | Built-in types, formula entries, center-mag/corners, params, maxiter, bailout, inside/outside/decomp/biomorph, the encoded `colors=` palette (as 256 exact gradient stops). What can't be reproduced is listed, not dropped. Chooser window for multi-entry files; `--par-entry`. |
| 5.2 BLA | `375f443` | Table built with the reference orbit; tolerance chosen against exact arithmetic (1e-16: at the conditioning floor). 5x on a 300000-iteration minibrot at 10^32x - found with a Newton nucleus search and added as tour stop 18. |
| 5.3 Formula language | `511336e` | if/elseif/else/endif, p4/p5, maxit, whitesq, Fractint's remaining functions. |
| 5.4 CPU interpreter | `511336e` | Shared syntax tree; orbit viewer; Julia preview for formulas with a partner; every formula's GPU render is compared with the interpreter pixel by pixel (98-100% agreement; an injected transpiler bug fails the tests). |
| 5.5 `--path-time`, PNG sequences | `f5cb055`, `a03cdb1` | |
| 5.6 Easing, integer interpolation | `1021e46` | |
| 5.7 Deep "Lift into 3D" | `dd1cc6b` | Split floats wouldn't have helped (the iteration itself loses the digits): the landscape got its own double-precision reference orbit and perturbation for all five maps; `hires` parameters keep the center's digits. Sharp to ~10^11x (was ~10^4x). |
| 5.8 Compiling indicator | `a9318a8` | |

## Found beyond the review

- The 2D pass-length regression above (from this round's own 2.8 change).
- `oversize_render_fails` and `roundtrip.center-hp` were passing without testing what they claimed.
- An offline render whose `--par` can't be loaded now fails (exit 1) instead of rendering the previous view.
- Loading any PAR reset the user's performance preferences.
- A formula chosen by name in a PAR had its `@view` override the PAR's own center.
- Landscapes using the custom formula didn't save it.
- The landscape reused formula variables from the previous point.

## Not verified here

Interactive feel (mouse, cursor capture, the gamepad), the GitHub Actions run itself, and real
1990s Fractint PAR collections (the importer was tested on a sample file written for this).

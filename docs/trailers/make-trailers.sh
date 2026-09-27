#!/bin/bash
# Renders the two 30-second trailers (docs/media/) from scratch:
#   docs/trailers/make-trailers.sh [WORKDIR]
# Needs a built fract3d (build/fract3d), ffmpeg with libx264 and drawtext, python3 with
# Pillow and numpy, and the Noto Sans fonts. Every shot is rendered deterministically
# (--fixed-dt, fixed autopilot seed), so a rerun gives the same trailers. The app runs
# with a throwaway config directory: your own settings and window layout are untouched.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
APP=$REPO/build/fract3d
WORK=${1:-$(mktemp -d /tmp/fract3d-trailers.XXXX)}
mkdir -p "$WORK"/{in,seg,cfg,cfg2/fract3d}
cd "$WORK"
echo "working in $WORK"
printf 'uiScale=2\n' > cfg2/fract3d/prefs.ini  # (the cockpit close-up is recorded at 4K with a 2x UI)
python3 "$REPO/docs/trailers/make_inputs.py"

# the views that need more than a preset
printf 'mode = 2d\ncolor.palette = Fractint VGA (default)\nclassic.banded = 1\nclassic.center = -0.7453 0.1127\nclassic.height = 0.012\nclassic.maxIter = 400\ncolor.cycleSpeed = 30\n' > in/title-cycle.par
printf 'mode = 2d\ncolor.palette = Ultra Fractal\nclassic.banded = 0\nclassic.center = -0.5 0\nclassic.height = 2.6\nclassic.maxIter = 500\nclassic.showOrbit = 1\n' > in/orbits.par
printf 'color.cycleSpeed = 25\n' > in/cyc-only.par

app() { XDG_CONFIG_HOME="$WORK/cfg" "$APP" "$@"; }
REC=(--fixed-dt 0.0333333 --size 1920x1080)
FLY=(--autopilot auto --autopilot-speed 1.6 --cockpit --hide-panels)

# camera paths, rendered at full quality by the app's own video export (no UI)
for spec in deepzoom:1 land:24 bulb:48 kifs:48 klein:32 mbox:16; do
    n=${spec%%:*}
    app --path "in/$n.f3dpath" --render "seg/$n.mp4" --fps 30 --size 1920x1080 --samples "${spec#*:}"
done
# shots with the UI, recorded frame by frame (--ui-record)
app --par in/title-cycle.par --hide-ui "${REC[@]}" --record-from 15 --frames 129 --ui-record seg/e-title.mp4
app --par in/orbits.par --inset --mouse 1118,229 --mouse-to 858,494 "${REC[@]}" --record-from 10 --frames 154 --ui-record seg/e-orbits.mp4
app --par in/cyc-only.par --formula Spider --open formula "${REC[@]}" --record-from 20 --frames 131 --ui-record seg/e-formula.mp4
app --par "$REPO/presets/07-mandelbulb-path-traced.par" "${FLY[@]}" "${REC[@]}" --record-from 100 --frames 850 --ui-record seg/a-bulb-long.mp4
app --par "$REPO/presets/13-crater-lake.par" "${FLY[@]}" "${REC[@]}" --record-from 100 --frames 850 --ui-record seg/a-land-long.mp4
app --par "$REPO/presets/12-kleinian-caves.par" "${FLY[@]}" "${REC[@]}" --record-from 100 --frames 310 --ui-record seg/a-klein.mp4
app --par "$REPO/presets/10-inside-the-mandelbox.par" "${FLY[@]}" "${REC[@]}" --record-from 390 --frames 590 --ui-record seg/a-mbox.mp4
XDG_CONFIG_HOME="$WORK/cfg2" "$APP" --par "$REPO/presets/12-kleinian-caves.par" "${FLY[@]}" --fixed-dt 0.0333333 --size 3840x2160 \
    --record-from 200 --frames 350 --ui-record seg/a-close4k.mp4

# edit: trims, captions, crossfades -> 30 s masters; then the web and sharing versions
python3 "$REPO/docs/trailers/assemble.py"
ENC="$REPO/docs/trailers/encode.sh"
"$ENC" out/master-trailer.mp4 out/fract3d-trailer.mp4 3500 1280:720
"$ENC" out/master-autopilot.mp4 out/fract3d-autopilot-trailer.mp4 3500 1280:720
"$ENC" out/master-trailer.mp4 out/fract3d-trailer-1080p.mp4 8000 1920:1080
"$ENC" out/master-autopilot.mp4 out/fract3d-autopilot-trailer-1080p.mp4 8000 1920:1080
ls -la out/
echo "the 720p versions go in docs/media/; the 1080p ones are for sharing"

# The trailers

`docs/media/fract3d-trailer.mp4` (the educational tour) and `docs/media/fract3d-autopilot-trailer.mp4`
are rendered by the app itself, then edited with ffmpeg:

```bash
docs/trailers/make-trailers.sh            # about 4 minutes on a fast GPU
```

- `make_inputs.py` writes the camera paths (orbits around the presets' own cameras; inside
  caves the camera only turns and creeps forward, so it never crosses a wall) and the 2D views.
- `make-trailers.sh` renders the path shots with the app's video export (`--path ... --render x.mp4`),
  and records the shots that show the UI with `--ui-record`, which pipes every frame, UI included,
  to ffmpeg. `--fixed-dt` makes every frame advance by the same time and waits for the GPU's
  distance probes, and the autopilot's wander seed is fixed, so the flights are the same every run.
  The app runs with a throwaway config directory (`XDG_CONFIG_HOME`), so your settings and window
  layout are untouched.
- `assemble.py` trims each shot, captions it on a fading band (Noto Sans), crossfades the shots into
  exactly 30 seconds and writes near-lossless masters; `encode.sh` makes the 720p web versions
  (3.5 Mbit/s, for this repository) and 1080p versions for sharing (8 Mbit/s).

The flight windows in `assemble.py` (which seconds of each recording are used) were picked by scoring
the frames for visible detail; if the autopilot's behaviour changes, pick them again.

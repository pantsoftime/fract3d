#!/usr/bin/env python3
# Builds the two Fract3D trailers from the recorded shots in seg/:
#   each shot is trimmed, normalised (1920x1080, 30 fps) and captioned on a fading band,
#   then the shots are chained with crossfades into exactly 30 seconds.
import subprocess, sys, os

FPS = 30
W, H = 1920, 1080
XF = 0.4  # crossfade length
FONT_BLACK = "/usr/share/fonts/noto/NotoSans-Black.ttf"
FONT_BOLD = "/usr/share/fonts/noto/NotoSans-Bold.ttf"
FONT_REG = "/usr/share/fonts/noto/NotoSans-Regular.ttf"
FONT_MED = "/usr/share/fonts/noto/NotoSans-Medium.ttf"
os.makedirs("clip", exist_ok=True)
os.makedirs("out", exist_ok=True)


def esc(t):  # drawtext text escaping
    return t.replace("\\", "\\\\").replace(":", "\\:").replace("'", "’").replace("%", "\\%")


def run(args):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        print(" ".join(args)[:2000])
        print(r.stderr[-3000:])
        sys.exit(1)


def text(t, font, size, x, y, color="white", alpha=1.0, shadow=True):
    s = f"drawtext=fontfile={font}:text='{esc(t)}':fontsize={size}:fontcolor={color}@{alpha}:x={x}:y={y}"
    if shadow:
        s += ":shadowcolor=black@0.6:shadowx=2:shadowy=2"
    return s


def caption_layer(dur, lines, where, t0=0.35, t1=None):
    """A transparent layer with a translucent band and text, fading in at t0 and out at t1."""
    t1 = dur - 0.45 if t1 is None else t1
    band_h = 150 if len(lines) > 1 else 100
    y = 44 if where == "top" else H - band_h - 60
    f = [f"color=c=black@0.0:s={W}x{H}:d={dur}:r={FPS}", "format=rgba",
         f"drawbox=x=0:y={y}:w={W}:h={band_h}:color=black@0.62:t=fill:replace=1"]
    title, *rest = lines
    f.append(text(title, FONT_BOLD, 60, "(w-text_w)/2", y + 14))
    if rest:
        f.append(text(rest[0], FONT_REG, 32, "(w-text_w)/2", y + 94, color="0xDDE6F0"))
    f += [f"fade=t=in:st={t0}:d=0.45:alpha=1", f"fade=t=out:st={t1}:d=0.45:alpha=1"]
    return ",".join(f)


def title_layer(dur, big, sub, sub2=None, t1=None, dim=0.6, subsize=46, subfont=None):
    """A full-frame title: darkened picture, a big wordmark and a line or two under it."""
    t1 = dur - 0.5 if t1 is None else t1
    f = [f"color=c=black@0.0:s={W}x{H}:d={dur}:r={FPS}", "format=rgba",
         f"drawbox=x=0:y=0:w={W}:h={H}:color=black@{dim}:t=fill:replace=1",
         text(big, FONT_BLACK, 190, "(w-text_w)/2", "(h-text_h)/2-110"),
         text(sub, subfont or FONT_MED, subsize, "(w-text_w)/2", "(h)/2+70", color="0xF2E6C8")]
    if sub2:
        f.append(text(sub2, FONT_REG, 34, "(w-text_w)/2", f"(h)/2+{70 + subsize + 24}", color="0xD8E2EC"))
    f += [f"fade=t=in:st=0.15:d=0.5:alpha=1", f"fade=t=out:st={t1}:d=0.5:alpha=1"]
    return ",".join(f)


def make_clip(name, src, start, dur, layer=None, pre="", speed=1.0):
    """Trim src from start for dur seconds (of output), normalise, overlay the layer."""
    srcdur = dur / speed
    base = f"[0:v]trim=start={start}:duration={srcdur},setpts=(PTS-STARTPTS)/{speed},{pre}scale={W}:{H}:flags=lanczos,fps={FPS},format=yuv420p,setsar=1[b]"
    if layer:
        fc = base + f";{layer}[l];[b][l]overlay=format=auto,format=yuv420p[v]"
    else:
        fc = base.replace("[b]", "[v]")
    run(["ffmpeg", "-loglevel", "error", "-y", "-i", src, "-filter_complex", fc, "-map", "[v]",
         "-t", str(dur), "-c:v", "libx264", "-preset", "medium", "-crf", "12", "-an", f"clip/{name}.mp4"])
    return (f"clip/{name}.mp4", dur)


def chain(clips, out, total, xf=XF, crf=21):
    """Crossfade the clips in order; the result is exactly `total` seconds."""
    inputs, fc, prev, t = [], [], "[0:v]", clips[0][1]
    for c, _ in clips:
        inputs += ["-i", c]
    for i, (c, d) in enumerate(clips[1:], start=1):
        off = t - xf
        lab = f"[x{i}]"
        fc.append(f"{prev}[{i}:v]xfade=transition=fade:duration={xf}:offset={off:.3f}{lab}")
        prev, t = lab, off + d
    fc.append(f"{prev}fade=t=out:st={total - 0.6}:d=0.6,format=yuv420p[v]")
    run(["ffmpeg", "-loglevel", "error", "-y", *inputs, "-filter_complex", ";".join(fc), "-map", "[v]",
         "-t", str(total), "-r", str(FPS), "-c:v", "libx264", "-preset", "slow", "-crf", str(crf),
         "-profile:v", "high", "-pix_fmt", "yuv420p", "-movflags", "+faststart", "-an", out])
    print(f"{out}: chained length {t:.2f} s, cut to {total} s")
    return t


def seq_len(durs, xf=XF):
    return sum(durs) - xf * (len(durs) - 1)


GH = "github.com/pantsoftime/fract3d"

# ---------------------------------------------------------------- trailer 1: the educational tour
gal = [("bulb", 0.1, 2.35), ("mbox", 0.1, 2.3), ("klein", 0.1, 2.3), ("kifs", 0.1, 2.35)]
gclips = [make_clip(f"g-{n}", f"seg/{n}.mp4", s, d) for n, s, d in gal]
chain(gclips, "clip/gallery-raw.mp4", seq_len([d for *_, d in gal], 0.3), xf=0.3, crf=12)
glen = seq_len([d for *_, d in gal], 0.3)
e = [
    make_clip("e1", "seg/e-title.mp4", 0.0, 3.6, title_layer(3.6, "FRACT3D", "Explore fractals. Understand them.", "The spirit of 1990's Fractint, on a modern GPU")),
    make_clip("e2", "seg/deepzoom.mp4", 0.0, 4.6, caption_layer(4.6, ["Zoom 100,000,000,000,000,000,000×", "Deep zoom, checked pixel by pixel against exact arithmetic"], "bottom")),
    make_clip("e3", "seg/e-orbits.mp4", 0.0, 4.8, caption_layer(4.8, ["See the math", "Orbits, live Julia sets, and a lesson for every fractal"], "top")),
    make_clip("e4", "seg/e-formula.mp4", 0.0, 3.7, caption_layer(3.7, ["Write your own formulas", "Fractint .frm files, compiled for the GPU"], "top")),
    make_clip("e5", "seg/land.mp4", 0.0, 4.4, caption_layer(4.4, ["Lift any view into 3D", "Escape time becomes a landscape"], "bottom")),
    make_clip("e6", "clip/gallery-raw.mp4", 0.0, glen, caption_layer(glen, ["Fly through 3D fractals", "Real-time or path traced · Mandelbulb · Mandelbox · Kleinian caves · KIFS"], "bottom", t1=glen - 0.6)),
]
end1 = 30.0 - (seq_len([d for _, d in e]) - XF)  # what the end card must cover
e.append(make_clip("e7", "seg/e-title.mp4", 0.2, round(end1 + 0.001, 3),  # (bookends: the Fractint colors again)
                   title_layer(round(end1, 3), "FRACT3D", "Free and open source \u00b7 Linux \u00b7 OpenGL 4.6", GH, t1=99, dim=0.66)))
chain(e, "out/master-trailer.mp4", 30.0, crf=12)

# ---------------------------------------------------------------- trailer 2: the autopilot
CROP = "crop=2560:1440:640:720,"  # the 4K close-up: the bottom middle, cockpit whole (scaled to 1080p)
def two(layer_a, layer_b):  # two layers, one over the other
    return layer_a + "[ta];" + layer_b + "[tb];[tb][ta]overlay=format=auto"
a = [
    make_clip("a1", "seg/a-bulb-long.mp4", 8.3, 7.2,
              two(title_layer(7.2, "FRACT3D", "AUTOPILOT", "Take the controls \u2014 or let it fly", t1=2.9, subsize=72, subfont=FONT_BOLD),
                  caption_layer(7.2, ["Around", "It circles the outside, holding its altitude"], "top", t0=3.4))),
    make_clip("a2", "seg/a-klein.mp4", 0.3, 6.2, caption_layer(6.2, ["Through", "It threads caves and tunnels, never touching a wall"], "top")),
    make_clip("a3", "seg/a-mbox.mp4", 0.4, 5.4, caption_layer(5.4, ["It senses its world", "Distance estimates and 34 probe rays, every frame"], "top")),
    make_clip("a4", "seg/a-close4k.mp4", 0.5, 4.4, caption_layer(4.4, ["A real cockpit", "Speed \u00b7 artificial horizon \u00b7 clearance \u00b7 a live map sliced from the fractal"], "top"), pre=CROP),
    make_clip("a5", "seg/a-land-long.mp4", 20.4, 4.6, caption_layer(4.6, ["The autopilot tour", "Every 3D world, hands-free"], "top")),
]
end2 = 30.0 - (seq_len([d for _, d in a]) - XF)
a.append(make_clip("a6", "seg/a-bulb-long.mp4", 18.6, round(end2 + 0.001, 3),
                   title_layer(round(end2, 3), "FRACT3D", "Press G. Enjoy the flight.", GH, t1=99, dim=0.66)))
chain(a, "out/master-autopilot.mp4", 30.0, crf=12)

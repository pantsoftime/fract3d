import math, os
F3D = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))  # the repository
IN = "in"
def preset(name):
    return "".join(l for l in open(f"{F3D}/presets/{name}.par") if not l.startswith(";"))
def rot_y(v, a):
    c, s = math.cos(a), math.sin(a)
    return (v[0]*c + v[2]*s, v[1], -v[0]*s + v[2]*c)
def cam(P, T):
    return f"camera.pos = {P[0]:.6f} {P[1]:.6f} {P[2]:.6f}\ncamera.target = {T[0]:.6f} {T[1]:.6f} {T[2]:.6f}\n"
def orbit(name, P, T, angles, dist, secs, extra=""):
    base = preset(name) + extra
    keys = []
    for i, (a, d) in enumerate(zip(angles, dist)):
        off = rot_y(tuple(p - t for p, t in zip(P, T)), math.radians(a))
        Pk = tuple(t + o * d for t, o in zip(T, off))
        keys.append(base + cam(Pk, T))
    return keys
def lookaround(name, P, T, angles, fwd, extra=""):
    base = preset(name) + extra
    keys = []
    for a, f in zip(angles, fwd):
        d = tuple(t - p for p, t in zip(P, T))
        Pk = tuple(p + x * f for p, x in zip(P, d))
        dk = rot_y(d, math.radians(a))
        keys.append(base + cam(Pk, tuple(p + x for p, x in zip(Pk, dk))))
    return keys
def write_path(fn, keys, secs):
    with open(f"{IN}/{fn}", "w") as o:
        o.write("; Fract3D camera path\n")
        for k, s in zip(keys, secs):
            o.write(f"---- keyframe {s} ease\n{k}")
# 3D gallery and the landscape (each ~2.4-4.4 s)
write_path("bulb.f3dpath", orbit("07-mandelbulb-path-traced", (1.85, 0.95, -2.05), (0, -0.05, 0), [-14, 0, 14], [1.0, 0.94, 0.9], None), [1.2, 1.2, 1])
write_path("kifs.f3dpath", orbit("15-kifs-gem", (1.55, 1.05, -1.85), (0, -0.05, 0), [16, 0, -16], [1.0, 0.95, 0.9], None), [1.2, 1.2, 1])
write_path("klein.f3dpath", lookaround("11-kleinian-caves", (0.3, 0.1, -0.9), (0.2, 0.05, 0.0), [-12, 0, 12], [0, 0.07, 0.14]), [1.2, 1.2, 1])
write_path("mbox.f3dpath", lookaround("09-mandelbox-cathedral", (0, 0, -4.2), (0, 0, 0), [10, 0, -10], [0, 0.001, 0.002]), [1.2, 1.2, 1])
write_path("land.f3dpath", orbit("12-crater-lake", (0.9, 1.5, -1.9), (-0.15, 0.0, 0.1), [-25, 0, 25], [1.0, 0.9, 0.82], None), [2.2, 2.2, 1])
# 2D: the deep zoom, whole set -> 10^20x at Seahorse Valley (a constant zoom rate)
deep = ("mode = 2d\ncolor.palette = Ultra Fractal\nclassic.banded = 0\nclassic.supersample = 2\nclassic.fp64 = 2\n"
        "classic.center = -0.743643887037158704752191506114774 0.131825904205311970493132056385139\n"
        "classic.centerHP = -0.743643887037158704752191506114774 0.131825904205311970493132056385139\n")
write_path("deepzoom.f3dpath", [deep + "classic.height = 2.6\nclassic.maxIter = 300\n", deep + "classic.height = 1e-20\nclassic.maxIter = 12000\n"], [4.6, 1])
with open(f"{IN}/deep-end.par", "w") as o: o.write(deep + "classic.height = 1e-20\nclassic.maxIter = 12000\n")
# 2D title: Fractint 1990 with the palette cycling
with open(f"{IN}/fractint-cycling.par", "w") as o: o.write(preset("01-fractint-1990") + "color.cycleSpeed = 45\n")
print(sorted(os.listdir(IN)))

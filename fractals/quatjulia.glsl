// @name Quaternion Julia
// @category Escape-time 3D
// @credit Alan Norton, 1982; John C. Hart et al., 1989 (ray tracing with distance estimation)
// @camera 1.6 1.0 -2.0   0 0 0
// @autopilot around 0.25
// @render stepFactor=0.9 detail=0.5 maxSteps=280 maxDist=6
// @look palette="Rainbow (cosine)" colorScale=0.9 paletteMix=0.85 specular=0.7 roughness=0.12 sunAzimuth=60 sunElevation=40 floor=1 floorY=-1.25
// @param vec4 c = (-0.2, 0.6, 0.2, 0.2) [-1.2, 1.2] "Constant c" -- The 4D constant. Each c gives a different Julia set. c near the edge of the Mandelbrot set gives the most intricate shapes.
// @param float slice = 0 [-1.2, 1.2] "4th dimension slice (w)" -- We can only show a 3D cross-section of a 4D object. This chooses where the cut is. Animate it to watch the object pass through our space.
// @param choice formula = 0 {Quadratic z^2 + c, Cubic z^3 + c} "Formula" -- The power used in the iteration.
// @param int iterations = 11 [2, 30] "Iterations" -- More gives a sharper surface.
// @about
// # Julia sets in four dimensions
// A Julia set uses a single fixed c and asks, for each starting point z: does z -> z^2 + c escape to infinity? In the complex plane this gives Fractint's Julia sets.
//
// Quaternions are 4D numbers (a + bi + cj + dk), found by Hamilton in 1843. They have a multiplication that works almost like complex numbers, so z^2 + c still makes sense. The resulting Julia set is a 4D object. What you see is a 3D slice of it, just as a CT scan shows a 2D slice of a body.
// $ z = (x, y, z, w)   w = the slice value
// $ z -> z^2 + c       (quaternion multiplication)
//
// # A landmark in fractal rendering
// In 1989 John Hart, Dan Sandin and Lou Kauffman used these objects to show that a distance estimate lets you ray trace a fractal with no polygons. This app works the same way. You're looking at a technique that took hours per frame in 1989, now running many times per second.
//
// # Things to try
// * Turn on "animate" next to the slice slider and watch the 4D object move through our 3D view.
// * Adjust c slowly. Complex Julia sets are connected only while c is inside the Mandelbrot set; the 4D shapes fall apart into dust in the same way.
// * Cubic formula with c = (0.4, 0.4, 0.1, 0.0).
// @end

float DE(vec3 p, inout vec4 trap) {
    vec4 z = vec4(p, slice);
    float dr = 1.0;
    float m = dot(z, z);
    float orb = 1e10;
    int i = 0;
    for (; i < iterations; i++) {
        if (formula == 0) {
            dr *= 2.0 * sqrt(m);
            z = qsqr(z) + c;
        } else {
            dr *= 3.0 * m;
            z = qmul(qsqr(z), z) + c;
        }
        m = dot(z, z);
        orb = min(orb, m);
        if (m > 64.0) break;
    }
    float r = sqrt(m);
    trap = vec4(sqrt(orb) * 0.8, abs(z.w) * 0.5, float(i) / float(iterations), 0.0);
    return 0.5 * r * log(max(r, 1e-12)) / max(dr, 1e-12);
}

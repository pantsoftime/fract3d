// @name Escape-time Landscape
// @category Classic 2D in 3D
// @credit Benoit Mandelbrot, 1980; Fractint's 3D mode, 1990
// @camera 0.9 1.5 -1.9   -0.15 0.0 0.1
// @autopilot around 0.08 look=0.35 orbit=0.35
// @render stepFactor=0.4 detail=0.6 maxSteps=500 maxDist=8
// @look palette="Ultra Fractal" colorScale=5.0 paletteMix=1.0 specular=0.04 roughness=0.7 sunAzimuth=150 sunElevation=28 sunSize=1.0 fov=50 fog=0.06 fogColor=0.66,0.70,0.80
// @param choice formula = 0 {Mandelbrot, Burning Ship, Tricorn, Multibrot z^3, Julia, Custom formula} "Formula" -- The 2D escape-time formula that the terrain is made from. "Custom formula" uses the formula from Classic 2D mode's formula editor.
// @param int iterations = 160 [10, 1500] "Iterations" -- Maximum iterations per point. Deeper zooms need more.
// @param float heightScale = 0.35 [0.0, 2.0] "Height" -- How tall the terrain is. The height of each point comes from its escape time.
// @param float curve = 1.8 [0.1, 4.0] "Height curve" -- Shapes the terrain. High values keep the plains flat and raise sharp ridges near the set. Low values lift everything into rolling hills.
// @param vec2 center = (-0.6, 0.0) [-2.0, 2.0] hires "Center" -- The point in the complex plane at the middle of the world.
// @param float zoom = 1.0 [0.05, 100000] log "Zoom" -- Magnification of the complex plane. At zoom 1, one world unit equals one unit in the complex plane.
// @param vec2 juliaC = (-0.8, 0.156) [-2.0, 2.0] "Julia constant" -- The c used by the Julia formula.
// @param choice insideStyle = 1 {Plateau, Lake} "Inside the set" -- Points that never escape (the set itself) can form a flat mesa on top, or a glassy lake at the bottom.
// @about
// # From numbers to mountains
// Every pixel of a Mandelbrot picture is a complex number c. We repeat z -> z^2 + c starting from z = 0 and count how many steps it takes for |z| to escape past 2. That count, the "escape time", is what Fractint turned into its colored bands.
//
// Here the escape time also sets the height. Points that escape quickly form the low plains. Points near the boundary take longer and climb into ridges. Points that never escape are the Mandelbrot set itself, shown as a black lake (or as a plateau on top).
// $ height = log(1 + n) / log(1 + maxIter)
// $ n = smooth iteration count = i + 1 - log2(log|z|)
// Fractint had a 3D mode that did this with lines and triangles. Here the terrain is ray-marched with soft shadows and depth of field.
//
// # The smooth iteration count
// Whole-number counts give terraces, just like Fractint's bands. The "smooth" or "continuous" count adds a fraction based on how far past the bailout z landed. That turns the steps into smooth slopes.
//
// # Things to try
// * In Classic 2D mode, zoom in somewhere and press "Lift into 3D". You land here, looking at the same spot.
// * For full 1990 nostalgia: palette "Fractint VGA", Color scale 1 (one palette entry per iteration, as in Fractint), then "1990 mode" on the Post tab.
// * Burning Ship makes a fantastic mountain range.
// @end

// Deep zooms (zoom >= 1000): the host computes one reference orbit Z_n at the exact
// center in doubles (src/renderer.cpp, prepareLandscape), and each point iterates
// only its difference d_n = z_n - Z_n, which floats resolve at any zoom - the same
// perturbation idea as Classic 2D's deep zoom (see the Concepts tab).
layout(std430, binding = 2) readonly buffer LandRef { vec2 landZ[]; };
uniform int uLandRefLen;  // 0: iterate directly

float landDiffAbs(float c, float d) {  // |c + d| - |c| without cancellation (Burning Ship)
    float cd = c + d;
    if (c >= 0.0) return cd >= 0.0 ? d : -(2.0 * c + d);
    return cd > 0.0 ? 2.0 * c + d : -d;
}

float landHeightRef(vec2 dc, out float smoothIter, out bool inside) {
    vec2 d = formula == 4 ? dc : vec2(0.0);  // Julia: the point is the starting z
    vec2 dcc = formula == 4 ? vec2(0.0) : dc;
    if (formula == 1) dcc.y = -dcc.y;
    int m = 0;
    float bail = 256.0;
    for (int i = 0; i < iterations; i++) {
        vec2 Z = landZ[m];
        if (formula == 1) {        // Burning Ship: x' = x^2 - y^2 + cx, y' = 2|x y| + cy
            float xy = Z.x * d.y + Z.y * d.x + d.x * d.y;
            d = vec2((2.0 * Z.x + d.x) * d.x - (2.0 * Z.y + d.y) * d.y, 2.0 * landDiffAbs(Z.x * Z.y, xy)) + dcc;
        } else if (formula == 2) { // Tricorn: z' = conj(z^2) + c
            vec2 t = 2.0 * cmul(Z, d) + cmul(d, d);
            d = vec2(t.x, -t.y) + dcc;
        } else if (formula == 3) { // z^3 + c
            d = 3.0 * cmul(cmul(Z, Z), d) + 3.0 * cmul(Z, cmul(d, d)) + cmul(cmul(d, d), d) + dcc;
        } else {                   // z^2 + c (Mandelbrot and Julia)
            d = 2.0 * cmul(Z, d) + cmul(d, d) + dcc;
        }
        m++;
        vec2 z = landZ[m] + d;
        float r2 = dot(z, z);
        if (r2 > bail * bail) {
            float lr = 0.5 * log(r2);
            smoothIter = max(float(i) + 1.0 - log(lr / log(bail)) / log(formula == 3 ? 3.0 : 2.0), 0.0);
            inside = false;
            return heightScale * pow(log(1.0 + smoothIter) / log(1.0 + float(iterations)), curve);
        }
        if (m >= uLandRefLen - 1 || r2 < dot(d, d)) {  // rebase onto the start of the reference
            d = z - landZ[0];
            m = 0;
        }
    }
    smoothIter = 0.0;
    inside = true;
    return insideStyle == 0 ? heightScale : -0.02 * heightScale;
}

float landHeight(vec2 xz, out float smoothIter, out bool inside) {
    if (uLandRefLen > 1 && formula <= 4) return landHeightRef(xz / zoom, smoothIter, inside);
    vec2 c = center + vec2(xz.x, xz.y) / zoom;
#ifdef HAVE_CUSTOM_FORMULA
    if (formula == 5) {  // the user's formula (spliced in by the host)
        frm_init(c);
        for (int i = 0; i < iterations; i++) {
            if (!frm_step(i)) {
                vec2 zz = frm_z();
                float r = length(zz);
                float nu = FRM_BAILOUT > 0.0 && r > FRM_BAILOUT ? log(log(r) / log(FRM_BAILOUT)) / log(FRM_POWER) : 0.0;
                smoothIter = max(float(i) + 1.0 - nu, 0.0);
                inside = false;
                return heightScale * pow(log(1.0 + smoothIter) / log(1.0 + float(iterations)), curve);
            }
        }
        smoothIter = 0.0;
        inside = true;
        return insideStyle == 0 ? heightScale : -0.02 * heightScale;
    }
#endif
    vec2 z = vec2(0.0);
    if (formula == 4) { z = c; c = juliaC; }
    if (formula == 1) c.y = -c.y;
    float bail = 256.0;
    float pw = formula == 3 ? 3.0 : 2.0;
    for (int i = 0; i < iterations; i++) {
        if (formula == 1) z = abs(z);
        if (formula == 2) z.y = -z.y;
        z = formula == 3 ? cmul(csqr(z), z) + c : csqr(z) + c;
        float r2 = dot(z, z);
        if (r2 > bail * bail) {
            float lr = 0.5 * log(r2);
            smoothIter = float(i) + 1.0 - log(lr / log(bail)) / log(pw);
            smoothIter = max(smoothIter, 0.0);
            inside = false;
            return heightScale * pow(log(1.0 + smoothIter) / log(1.0 + float(iterations)), curve);
        }
    }
    smoothIter = 0.0;
    inside = true;
    return insideStyle == 0 ? heightScale : -0.02 * heightScale;
}

float DE(vec3 p, inout vec4 trap) {
    float it; bool ins;
    float h = landHeight(p.xz, it, ins);
    trap = ins ? vec4(0.0, 0.0, 1.0, insideStyle == 1 ? 1.0 : 0.0)  // w = 1: render as water
               : vec4(it / 256.0, it / float(iterations), it / float(iterations), 0.0);
    // A height field is not a true distance: steep cliffs make it overestimate,
    // which is why this fractal ships with a small step factor.
    return (p.y - h);
}

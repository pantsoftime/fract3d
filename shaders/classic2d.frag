// classic2d.frag — flat escape-time fractals, Fractint style.
// Writes the (smooth) iteration count, NOT a color: the display pass maps it
// through the palette. That makes palette cycling free, exactly like Fractint
// rotating the VGA DAC registers.
//   R = iteration value (>= 0 escaped, -1 inside)
//   G = outside: basin/root index (Newton);  inside: final |z| (for "zmag" coloring)
// Compiled twice: with and without FP64 (double precision for deep zooms).

layout(location = 0) out vec4 outColor;

uniform vec2   uResolution;
uniform dvec2  uCenter;
uniform double uPixelSize;     // complex-plane units per pixel
uniform dvec2  uJuliaC;
uniform int    uJulia;
uniform int    uFormula;       // see Formula list in app
uniform int    uMaxIter;
uniform float  uBailout;
uniform int    uPower;
uniform vec2   uPhoenixP;
uniform int    uBanded;        // 1: store whole iteration counts (Fractint bands), 0: smooth count

#ifdef FP64
  #define REAL double
  #define CPLX dvec2
#else
  #define REAL float
  #define CPLX vec2
#endif

CPLX cm(CPLX a, CPLX b) { return CPLX(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
CPLX cd(CPLX a, CPLX b) { return CPLX(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / dot(b, b); }
CPLX cpowi(CPLX z, int n) { CPLX r = z; for (int i = 1; i < n; i++) r = cm(r, z); return r; }

void main() {
    dvec2 pd = uCenter + dvec2(gl_FragCoord.xy - 0.5 * uResolution) * uPixelSize;
    CPLX pixel = CPLX(pd);
    CPLX c = uJulia != 0 ? CPLX(uJuliaC) : pixel;
    CPLX z = uJulia != 0 ? pixel : CPLX(0);
    REAL bail = REAL(uBailout);
    float powerF = 2.0;

    if (uFormula == 1 && uJulia == 0) c.y = -c.y;          // Burning Ship is traditionally shown upright
    if (uFormula == 6 && uJulia == 0) z = CPLX(0.5, 0.0);  // Lambda: start at the critical point
    if (uFormula == 3) powerF = float(uPower);

    // ---- Newton's method for z^3 - 1: color by which root each start point finds
    if (uFormula == 4) {
        z = pixel;
        for (int i = 0; i < uMaxIter; i++) {
            CPLX z2 = cm(z, z);
            CPLX f = cm(z2, z) - CPLX(1, 0);
            if (dot(f, f) < REAL(1e-10)) {
                float ang = atan(float(z.y), float(z.x));
                float root = mod(round(ang / (TAU / 3.0)) + 3.0, 3.0);
                // fractional part from the last step size gives smooth bands
                outColor = vec4(float(i), root, 0, 1);
                return;
            }
            z -= cd(f, REAL(3) * z2);
        }
        outColor = vec4(-1, 0, 0, 1);
        return;
    }

    CPLX zprev = CPLX(0);
    for (int i = 0; i < uMaxIter; i++) {
        switch (uFormula) {
        case 0: z = cm(z, z) + c; break;                                  // Mandelbrot
        case 1: z = abs(z); z = cm(z, z) + c; break;                      // Burning Ship
        case 2: z = cm(CPLX(z.x, -z.y), CPLX(z.x, -z.y)) + c; break;      // Tricorn (Mandelbar)
        case 3: z = cpowi(z, uPower) + c; break;                          // Multibrot
        case 5: {                                                         // Phoenix
            CPLX zn = cm(z, z) + CPLX(c.x, 0) + cm(CPLX(uPhoenixP), zprev) + CPLX(0, c.y);
            zprev = z; z = zn; break; }
        case 6: z = cm(c, cm(z, CPLX(1, 0) - z)); break;                  // Lambda (logistic map)
        case 7: {                                                         // Magnet I
            CPLX num = cm(z, z) + c - CPLX(1, 0);
            CPLX den = REAL(2) * z + c - CPLX(2, 0);
            z = cd(num, den); z = cm(z, z);
            CPLX dz = z - CPLX(1, 0);
            if (dot(dz, dz) < REAL(1e-8)) { outColor = vec4(float(i), 1, 0, 1); return; }
            break; }
        }
        REAL r2 = dot(z, z);
        if (r2 > bail * bail) {
            if (uBanded != 0) {
                // Fractint: a point escaping on the first iteration gets color 1, and so on
                outColor = vec4(float(i) + 0.5, 0, 0, 1);
                return;
            }
            // smooth ("continuous") iteration count: removes the hard color bands
            float lr = log(float(r2)) * 0.5;
            float nu = log(max(lr / log(float(bail)), 1e-6)) / log(powerF);
            outColor = vec4(max(float(i) + 1.0 - nu, 0.0), 0, 0, 1);
            return;
        }
    }
    outColor = vec4(-1, sqrt(float(dot(z, z))), 0, 1);
}

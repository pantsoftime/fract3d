// @name Kaleidoscopic IFS
// @category Folding
// @credit Knighty, 2010 (fractalforums.com)
// @camera 1.55 1.05 -1.85   0 -0.05 0
// @render stepFactor=1.0 detail=0.5 maxSteps=220 maxDist=6
// @look palette="Neon" colorScale=0.9 paletteMix=0.9 background=1 bgColor=0.01,0.005,0.03 glow=0.9 glowColor=0.55,0.25,1.0 sky=0.35 sunIntensity=2.2 specular=0.5 roughness=0.25
// @param choice fold = 1 {Tetrahedral, Octahedral} "Symmetry" -- Which set of mirror planes to fold with. Tetrahedral has 24 symmetries and Octahedral (cube) has 48.
// @param int iterations = 14 [1, 30] "Iterations" -- Number of fold-rotate-scale steps.
// @param float scale = 2.3 [1.2, 3.5] "Scale" -- How much each step zooms in.
// @param vec3 offset = (1, 0.5, 0.1) [0, 2] "Offset" -- The fixed point that each copy scales toward. This is the main shape control.
// @param vec3 rotation1 = (0, 0, 5) [-180, 180] "Rotate before fold" -- Rotation before folding. Small changes give very different forms.
// @param vec3 rotation2 = (18, 4, 0) [-180, 180] "Rotate after fold" -- Rotation after folding.
// @about
// # A kaleidoscope for space
// A kaleidoscope uses mirrors to turn a few colored beads into a symmetric pattern. A kaleidoscopic IFS (Iterated Function System) does the same thing in 3D, many times over:
// $ 1. rotate
// $ 2. fold (mirror) into one symmetric wedge
// $ 3. rotate again
// $ 4. scale up about an offset point
// $ repeat, then measure the distance
// With no rotation this gives familiar shapes: Sierpinski, Menger, octahedral flakes. Add small rotations and you get structures no one has seen before, which is why this is a playground for finding new forms.
//
// # Iterated Function Systems
// Fractint's IFS mode drew ferns and spirals by repeatedly applying a few affine maps to a point, using the chaos game. Distance-estimated KIFS turns that process around: instead of throwing points at the attractor, we fold each ray's sample point backward into it.
//
// # Things to try
// * Change the two rotations a few degrees at a time. This is where the surprises are.
// * Octahedral symmetry, scale 2, offset (1, 0, 0) and no rotation gives a Sierpinski octahedron: six half-size copies at the corners. Then rotate it.
// * Tetrahedral symmetry with offset (1, 1, 1) gives the Sierpinski pyramid.
// * Path-traced mode (Render tab) and leave it still for a few seconds.
// @end

float DE(vec3 p, inout vec4 trap) {
    mat3 R1 = rotEulerDeg(rotation1);
    mat3 R2 = rotEulerDeg(rotation2);
    vec3 z = p;
    float orb = 1e10, orb2 = 1e10;
    int n = 0;
    while (n < iterations) {
        z = R1 * z;
        if (fold == 0) tetraFold(z); else octaFold(z);
        z = R2 * z;
        z = z * scale - offset * (scale - 1.0);
        n++;  // z has now been scaled n times
        float r2 = dot(z, z);
        orb = min(orb, r2);
        orb2 = min(orb2, abs(z.y));
        if (r2 > 1e4) break;
    }
    trap = vec4(sqrt(orb) * 0.6 + orb2 * 0.8, orb2 * 0.5, float(n) / float(iterations), 0.0);
    return (length(z) - 1.0) * pow(scale, -float(n));
}

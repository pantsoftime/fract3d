// @name Mandelbox
// @category Folding
// @credit Tom Lowe (Tglad), 2010
// @camera 11 8 -15   0 0 0
// @autopilot through 0.03
// @render stepFactor=0.9 detail=0.5 maxSteps=320 maxDist=6
// @look palette="Copper Patina" colorScale=1.2 paletteMix=0.9 specular=0.3 sunAzimuth=140 sunElevation=40 floor=1 floorY=-6.1
// @param float scale = 2 [-3, 3] "Scale" -- Multiplies space after each fold. Negative scales (about -1.5) give sponge-like, organic shapes. Around 2 to 3 gives architectural boxes full of chambers.
// @param float minRadius = 0.5 [0.0, 1.5] "Min radius" -- Inside this sphere the inversion becomes a constant scaling, so points near the center don't blow up to infinity.
// @param float fixedRadius = 1.0 [0.1, 2.0] "Fixed radius" -- The sphere that points are inverted through.
// @param float foldLimit = 1.0 [0.1, 2.0] "Fold limit" -- Half the size of the box fold. Points outside +/- this value are reflected back inside.
// @param int iterations = 14 [1, 40] "Iterations" -- Number of fold-scale-translate steps.
// @param vec3 rotation = (0, 0, 0) [-180, 180] "Rotation per step" -- Rotates space a little after every fold. Small angles give twisted, alien structures.
// @param bool julia = false "Julia mode" -- Add a fixed constant instead of the original point.
// @param vec3 juliaC = (1.0, 1.0, 1.0) [-3, 3] "Julia constant" -- Constant added in Julia mode.
// @about
// # Folding space
// The Mandelbox has the same outline as the Mandelbrot formula, z -> f(z) * scale + c, but the step f uses no multiplication at all. Instead it folds space, the way you fold a sheet of paper:
// $ box fold:    if a coordinate is past +/-1, reflect it back
// $ sphere fold: if |z| < 1, turn the point inside out (invert it)
// $ z -> scale * sphereFold(boxFold(z)) + c
// Each fold is a mirror, so after many iterations the space is full of mirrored copies of itself. A point belongs to the Mandelbox if its orbit never escapes.
//
// # Why it looks built by hand
// Box folds make flat walls and right angles. Sphere folds make round holes and smooth curves. Together they give something between a city and a cathedral. Tom Lowe found it in 2010 while searching for a better 3D Mandelbrot.
//
// # Things to try
// * Scale 2 is the classic architectural box. Try -1.5 for the organic sponge look, and 2.5 or -2.0.
// * Set Min radius to 0 for sharper, more chaotic detail.
// * Add a few degrees of rotation per step.
// * Fly into one of the openings with WASD: a scale-2 box is a city of rooms inside rooms.
// @end

float DE(vec3 p, inout vec4 trap) {
    vec3 z = p;
    vec3 c = julia ? juliaC : p;
    float dr = 1.0;
    float minR2 = max(minRadius * minRadius, 1e-4);
    float fixR2 = fixedRadius * fixedRadius;
    mat3 R = rotEulerDeg(rotation);
    bool rot = dot(rotation, rotation) > 0.0;
    vec4 orb = vec4(1e10);
    for (int i = 0; i < iterations; i++) {
        boxFold(z, foldLimit);
        sphereFold(z, dr, minR2, fixR2);
        if (rot) z = R * z;
        z = z * scale + c;
        dr = dr * abs(scale) + 1.0;
        orb = min(orb, vec4(abs(z), dot(z, z)));
    }
    trap = vec4(sqrt(orb.w) * 0.35, orb.x * 0.5, orb.y * 0.5, 0.0);
    return length(z) / abs(dr);
}

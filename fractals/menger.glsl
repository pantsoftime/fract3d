// @name Menger Sponge
// @category Folding
// @credit Karl Menger, 1926 (folding formulation after Knighty and Syntopia)
// @camera 2.2 1.6 -2.6   0 0 0
// @autopilot through 0.05
// @render stepFactor=1.0 detail=0.5 maxSteps=200 maxDist=6
// @look palette="Plaster" paletteMix=1.0 specular=0.15 sunAzimuth=125 sunElevation=28 sunSize=1.2 floor=1 floorY=-1.0
// @param int iterations = 5 [0, 10] "Iterations" -- Level 0 is a solid cube. Each level removes the middle of every cube face and the center. Each step multiplies the detail by 20.
// @param float scale = 3 [1.5, 4.0] "Scale" -- 3 gives the true sponge, where each cube becomes 20 cubes a third the size. Other values overlap or spread out the copies.
// @param vec3 offset = (1, 1, 1) [0, 2] "Offset" -- Where each copy is placed after scaling. Change it to shift the holes around.
// @param vec3 preRotation = (0, 0, 0) [-180, 180] "Rotate before fold" -- Rotation applied before each fold. Even a few degrees turns the sponge into crystals.
// @param vec3 rotation = (0, 0, 0) [-180, 180] "Rotate after fold" -- Rotation applied after each fold.
// @about
// # The sponge with zero volume
// Start with a cube. Split it into 27 smaller cubes, like a Rubik's cube, and remove the center one plus the center of each face, 7 in all. Do the same to each of the 20 cubes left, and keep going forever.
// $ volume after n steps  = (20/27)^n -> 0
// $ surface after n steps -> infinity
// $ fractal dimension     = log 20 / log 3 = 2.727
// So the finished sponge has no volume but infinite surface area. Its fractal dimension of about 2.73 falls between a surface (2) and a solid (3).
//
// # Building it by folding
// We don't store 20^n cubes. For each point we fold space to exploit the cube's symmetry: abs() on every axis, then sort the coordinates. That maps all 48 symmetric copies onto one wedge. Then we scale by 3 and shift, and repeat. It works like zooming into one sub-cube at a time, then measuring the distance to a plain box at the end.
//
// # Things to try
// * Step through Iterations 0 to 6 to watch the sponge being carved.
// * Set "Rotate before fold" to about (0, 0, 30) to turn it into alien crystal.
// * Scale 2.5 or 3.5 with a non-uniform Offset.
// @end

float DE(vec3 p, inout vec4 trap) {
    mat3 R1 = rotEulerDeg(preRotation);
    mat3 R2 = rotEulerDeg(rotation);
    vec3 z = p;
    float s = 1.0;
    float orb = 1e10;
    for (int i = 0; i < iterations; i++) {
        z = R1 * z;
        octaFold(z);
        z = R2 * z;
        z = scale * z - offset * (scale - 1.0);
        if (z.z < -0.5 * offset.z * (scale - 1.0)) z.z += offset.z * (scale - 1.0);
        s *= scale;
        orb = min(orb, dot(z, z));
    }
    float d = sdBox(z, vec3(1.0)) / s;
    trap = vec4(sqrt(orb) * 0.3, length(z) * 0.3, 0.0, 0.0);
    return d;
}

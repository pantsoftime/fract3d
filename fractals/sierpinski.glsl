// @name Sierpinski Tetrahedron
// @category Folding
// @credit Wacław Sierpiński, 1915 (3D folding form after Syntopia)
// @camera 2.4 1.6 -2.6   0 0.3 0
// @render stepFactor=1.0 detail=0.5 maxSteps=220 maxDist=6
// @look palette="Ice" colorScale=1.3 paletteMix=0.95 specular=0.4 sunAzimuth=200 sunElevation=24 floor=1 floorY=-0.58
// @param int iterations = 16 [1, 30] "Iterations" -- Each iteration halves the size of the tetrahedra.
// @param float scale = 2 [1.2, 3.0] "Scale" -- 2 makes a perfect Sierpinski pyramid. Above 2 the copies separate into dust. Below 2 they overlap and merge.
// @param vec3 offset = (1, 1, 1) [0, 2] "Offset" -- The corner that each copy shrinks toward.
// @param vec3 rotation = (0, 0, 0) [-180, 180] "Rotation per step" -- Twist added at each level.
// @about
// # Four copies of itself
// The Sierpinski tetrahedron (a "tetrix") is made of 4 copies of itself, each half the size. It is the 3D version of the Sierpinski triangle that Fractint drew one random dot at a time, using the "chaos game".
// $ copies = 4, scale = 1/2
// $ dimension = log 4 / log 2 = 2 exactly!
// Surprise: its fractal dimension is exactly 2, the same as a flat sheet, even though it takes up space in 3D. At every level the total surface area stays the same.
//
// # How the folds work
// Three mirror planes, x+y=0, x+z=0 and y+z=0, reflect every point into one corner of the tetrahedron. Scaling by 2 about that corner zooms into the copy there. Repeating the fold and scale sends the point through the tree of copies.
//
// # Things to try
// * Rotation (0, 0, 20): the pyramid twists into a spiral staircase.
// * Scale 1.8 with offset (1, 1, 0.5).
// * Very low sun (Lighting tab) to throw long shadows through the holes.
// @end

float DE(vec3 p, inout vec4 trap) {
    mat3 R = rotEulerDeg(rotation);
    bool rot = dot(rotation, rotation) > 0.0;
    // Stand the pyramid on its base: this rotation takes world +y to the (1,1,1)
    // vertex direction (the angle between them is acos(1/sqrt 3) = 0.9553 rad).
    vec3 z = rotAxis(vec3(0.70710678, 0.0, -0.70710678), 0.95531662) * p;
    float orb = 1e10;
    int n = 0;
    while (n < iterations) {
        tetraFold(z);
        if (rot) z = R * z;
        z = z * scale - offset * (scale - 1.0);
        n++;  // count before the bailout test: z has now been scaled n times
        float r2 = dot(z, z);
        orb = min(orb, r2);
        if (r2 > 1e4) break;
    }
    trap = vec4(sqrt(orb) * 0.4, float(n) / float(iterations), float(n) / float(iterations), 0.0);
    return (length(z) - 1.0) * pow(scale, -float(n));
}

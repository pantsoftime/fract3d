// @name Pseudo-Kleinian
// @category Inversion
// @credit Knighty, 2011 (after Kleinian group limit sets: Mumford, Series & Wright, "Indra's Pearls")
// @camera 0.3 0.1 -0.9   0.2 0.05 0.0
// @render stepFactor=0.9 detail=0.4 maxSteps=300 maxDist=10
// @look palette="Twilight" colorScale=1.4 paletteMix=0.9 fog=0.5 fogColor=0.35,0.3,0.45 sunAzimuth=30 sunElevation=55
// @param vec3 boxSize = (0.92436, 0.90756, 0.92436) [0.4, 1.5] "Box size" -- Size of the box fold. This is the main shape control; small changes open or close whole caverns.
// @param float size = 1.0 [0.5, 1.5] "Inversion radius" -- Radius of the sphere inversion.
// @param vec3 shift = (0, 0, 0) [-0.5, 0.5] "Shift" -- Constant added after each step.
// @param float tube = 0.92784 [0.1, 1.5] "Tube radius" -- Controls the thickness of the rods in the final distance shape.
// @param int iterations = 10 [1, 30] "Iterations" -- Number of fold and invert steps.
// @about
// # Indra's pearls
// A Kleinian group is a set of transformations (inversions in spheres, turns, scalings) combined together. Apply them over and over to a point, and the places you can end up form a limit set: often an intricate lacework of spheres inside spheres. The book "Indra's Pearls" (2002) is a beautiful introduction.
//
// # The "pseudo" part
// Knighty found that a simple loop gives shapes very close to true Kleinian limit sets: box fold, then sphere inversion, repeated. That is almost the Mandelbox, but the inversion is only allowed to push points outward, never pull them in. The result is a maze of tunnels, bridges and cathedral ribs.
// $ p -> 2 * clamp(p, -box, box) - p
// $ p -> p * max(size / |p|^2, 1)
//
// # Things to try
// * You start inside a cavern. Fly around with WASD and hold the right mouse button to look around.
// * Adjust Box size a little at a time.
// * Path-traced mode shows off the sunlight falling into the caves.
// @end

float DE(vec3 p, inout vec4 trap) {
    float k_acc = 1.0;
    float orb = 1e10, orb2 = 1e10;
    for (int i = 0; i < iterations; i++) {
        p = 2.0 * clamp(p, -boxSize, boxSize) - p;
        float r2 = dot(p, p);
        float k = max(size / r2, 1.0);
        p *= k;
        k_acc *= k;
        p += shift;
        orb = min(orb, r2);
        orb2 = min(orb2, abs(p.z));
    }
    float rxy = length(p.xy);
    trap = vec4(sqrt(orb) * 1.2, orb2, 0.0, 0.0);
    return max(rxy - tube, abs(rxy * p.z) / length(p)) / k_acc;
}

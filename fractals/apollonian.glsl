// @name Apollonian Cathedral
// @category Inversion
// @credit Apollonius of Perga (c. 200 BC); this form after Inigo Quilez
// @camera 2.79 0.7 2.46   0.69 0.5 -0.79
// @render stepFactor=0.8 detail=0.4 maxSteps=300 maxDist=10
// @look palette="Dusk (cosine)" colorScale=1.0 paletteMix=0.9 fog=0.12 fogColor=0.75,0.62,0.55 sunAzimuth=250 sunElevation=20 sunColor=1.0,0.8,0.6
// @param float strength = 1.15 [0.8, 1.6] "Inversion strength" -- The s in the inversion p -> s * p / |p|^2. Around 1.1 gives airy arches. Higher values make the gaps fill in.
// @param int iterations = 8 [1, 14] "Iterations" -- Levels of nested spheres.
// @param float cell = 1.0 [0.5, 2.0] "Cell size" -- Size of the repeating lattice cell that space is wrapped into.
// @about
// # Circles in circles
// Apollonius asked: given three circles that touch each other, which circles touch all three? There are always exactly two. Fill every gap that way, forever, and you get the Apollonian gasket: an infinitely detailed packing of circles.
//
// # Inversion: turning space inside out
// The key operation is circle (or sphere) inversion:
// $ p -> s * p / |p|^2
// Points near the center get flung far away, far points come close, and spheres map to other spheres. This app uses a 3D version: wrap space into a repeating lattice, invert through a sphere, and repeat. The result is an endless cathedral of arches and domes.
//
// # A world, not an object
// Most fractals here are a single object that you orbit. This one fills all of space, so it is best explored from inside. Hold the right mouse button to look around and use WASD to fly. Movement speed adjusts to how close the walls are.
//
// # Things to try
// * Fly slowly forward through the arches.
// * Turn on Fog (Lighting tab) for depth.
// * Set Inversion strength to 1.3 or more for dense coral.
// @end

float DE(vec3 p, inout vec4 trap) {
    p /= cell;
    float scale = 1.0;
    vec4 orb = vec4(1000.0);
    for (int i = 0; i < iterations; i++) {
        p = -1.0 + 2.0 * fract(0.5 * p + 0.5);
        float r2 = dot(p, p);
        orb = min(orb, vec4(abs(p), r2));
        float k = strength / r2;
        p *= k;
        scale *= k;
    }
    trap = vec4(orb.w * 1.2, orb.y, orb.z, 0.0);
    return 0.25 * abs(p.y) / scale * cell;
}

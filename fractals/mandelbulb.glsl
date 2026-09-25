// @name Mandelbulb
// @category Escape-time 3D
// @credit Daniel White & Paul Nylander, 2009 (after Rudy Rucker's 1987 idea)
// @camera 1.85 0.95 -2.05   0 -0.05 0
// @render stepFactor=0.9 detail=0.5 maxSteps=300 maxDist=6
// @look palette="Gold" colorScale=1.1 colorOffset=0.1 paletteMix=0.95 specular=0.35 roughness=0.3 sunAzimuth=320 sunElevation=35 floor=1 floorY=-1.2
// @param float power = 8 [2, 16] "Power" -- The exponent n in z -> z^n + c. White and Nylander found n = 8 gives the famous bulb. Try 2 (a lumpy blob), 3 or 4, and in-between values for morphing shapes.
// @param int iterations = 10 [1, 30] "Iterations" -- How many times the formula is applied. More iterations = finer surface detail, but also more work per pixel.
// @param float bailout = 2 [1.2, 8] "Bailout" -- Once |z| grows past this radius we assume the point escapes to infinity.
// @param bool julia = false "Julia mode" -- Instead of c = the point being tested, use one fixed constant c for all points. Every c gives a different 3D Julia set.
// @param vec3 juliaC = (0.35, -0.45, 0.25) [-1.5, 1.5] "Julia constant c" -- The fixed c used in Julia mode.
// @param float phaseTheta = 0 [-180, 180] "Theta phase" -- Adds a constant to the polar angle after it is multiplied. Twists the bulb into new shapes.
// @param float phasePhi = 0 [-180, 180] "Phi phase" -- Adds a constant to the azimuth angle after it is multiplied.
// @about
// # The search for a 3D Mandelbrot
// The Mandelbrot set works because multiplying complex numbers rotates and scales points in the plane: squaring z doubles its angle and squares its length. There is no 3D number system that behaves the same way, so for decades nobody knew what a "true" 3D Mandelbrot would look like.
//
// In 2009 Daniel White and Paul Nylander took a shortcut. They wrote a 3D point in spherical coordinates (radius r, angles theta and phi), then "raised it to a power" by raising the radius to n and multiplying both angles by n:
// $ r -> r^n      theta -> n * theta      phi -> n * phi
// $ z -> z^n + c, repeated; points whose orbit stays bounded are inside
// Power 2 gives only a disappointing blob. Power 8 gives the Mandelbulb, with a surprising amount of detail: tendrils, valleys and coral-like towers that repeat at every scale.
//
// # How it is drawn: distance estimation
// We never build a mesh. For any point in space, the formula can estimate how far away the nearest surface is:
// $ DE = 0.5 * log(r) * r / dr
// Here dr tracks how quickly the orbit stretches (a running derivative). The ray marcher steps each ray forward by the DE, which is always a safe distance, until it is closer than about a pixel. That process is called sphere tracing.
//
// # Things to try
// * Drag Power slowly from 2 to 8 and watch the bulb grow its lobes.
// * Turn on Julia mode and adjust c: every c gives a different sculpture.
// * Fly into a crevice with WASD. The detail keeps going down as far as the Iterations allow.
// @end

float DE(vec3 pos, inout vec4 trap) {
    vec3 z = pos.xzy;               // point the bulb's axis up (+y)
    vec3 c = julia ? juliaC : z;
    float dr = 1.0;
    float r = length(z);
    float minR = 1e10, minPlane = 1e10;
    int i = 0;
    for (; i < iterations && r < bailout; i++) {
        float theta = acos(clamp(z.z / max(r, 1e-12), -1.0, 1.0)) * power + radians(phaseTheta);
        float phi   = atan(z.y, z.x) * power + radians(phasePhi);
        dr = pow(r, power - 1.0) * power * dr + (julia ? 0.0 : 1.0);
        z = pow(r, power) * vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta)) + c;
        r = length(z);
        minR = min(minR, r);
        minPlane = min(minPlane, abs(z.z));
    }
    trap = vec4(minR, minPlane * 2.0, float(i) / float(iterations), 0.0);
    return 0.5 * log(max(r, 1e-12)) * r / dr;
}

// probe.frag — small passes read back by the CPU, or shown in the cockpit.
//
// uMode 0, the sensor row (one float per pixel, read back):
//   pixel 0:     distance estimate at the camera (drives fly speed, the autopilot's clearance)
//   pixel 1:     distance along uProbeDir to the surface (click-to-focus / orbit pick), -1 on miss
//   pixels 2-5:  the fractal's distance estimate (without the floor) at four points around
//                the camera (a tetrahedron of size uGradH): their weighted sum is the gradient,
//                pointing away from the fractal's surface
//   pixel 6:     the fractal's own distance estimate at the camera (without the floor: the
//                autopilot circles the object, not the ground)
//   pixels 7-:   the autopilot's whiskers: free distance along uWhisker[i] (floor included),
//                -1 when nothing is hit within uWhiskerRange
// uMode 1, the cockpit map: a slice through the fractal in the plane spanned by uMapRight
//   and uMapFwd, centered on uMapCenter and uMapSpan across, drawn like a radar screen.
layout(location = 0) out vec4 outColor;

uniform vec3  uCamPos;
uniform vec3  uProbeDir;
uniform float uPixelAngle;
uniform float uDetail;
uniform float uStepFactor;
uniform int   uMaxSteps;
uniform float uMaxT;
uniform int   uFloor;          // the ground plane counts as a surface too
uniform float uFloorY;

uniform int   uMode;
uniform float uGradH;
uniform vec3  uWhisker[40];
uniform int   uNumWhiskers;
uniform float uWhiskerRange;
uniform vec3  uMapCenter, uMapRight, uMapFwd;
uniform float uMapSpan;
uniform vec2  uMapRes;

float sceneDE(vec3 p, inout vec4 trap) {
    float d = DE(p, trap);
    return uFloor != 0 ? min(d, abs(p.y - uFloorY)) : d;
}

// distance along a ray to the surface, -1 when nothing is hit before maxT
float march(vec3 ro, vec3 rd, float maxT) {
    vec4 trap;
    float t = 0.0, prevT = 0.0;
    for (int i = 0; i < uMaxSteps; i++) {
        float d = sceneDE(ro + rd * t, trap);
        if (d < max(t * uPixelAngle * uDetail, 1e-7)) {
            if (d < 0.0 && i > 0) {
                // overshot into the surface (height fields can): bisect back to the crossing,
                // so the camera is never told there's more room ahead than there is
                float a = prevT, b = t;
                for (int k = 0; k < 12; k++) {
                    float m = 0.5 * (a + b);
                    if (sceneDE(ro + rd * m, trap) < 0.0) b = m; else a = m;
                }
                t = a;
            }
            return t;
        }
        prevT = t;
        t += d * uStepFactor;
        if (t > maxT) break;
    }
    return -1.0;
}

vec3 mapColor(vec2 frag) {
    vec2 uv = (frag / uMapRes - 0.5) * uMapSpan;
    vec3 p = uMapCenter + uMapRight * uv.x + uMapFwd * uv.y;
    vec4 trap;
    float d = DE(p, trap);  // (no floor: a horizontal slice through it would fill the map)
    float px = uMapSpan / uMapRes.x;
    if (d < px * 0.75) {  // solid: amber, with a bright rim
        float rim = smoothstep(-px * 3.0, px * 0.75, d);
        return mix(vec3(0.55, 0.32, 0.05), vec3(1.0, 0.78, 0.30), rim);
    }
    // open space: dark green, glowing near surfaces, with distance contours every eighth of the map
    float glow = exp(-d / (uMapSpan * 0.06));
    float step = uMapSpan / 8.0, f = fract(d / step);
    float line = 1.0 - smoothstep(0.0, px * 1.2 / step, min(f, 1.0 - f));
    return vec3(0.01, 0.05, 0.03) + vec3(0.05, 0.35, 0.18) * glow + vec3(0.03, 0.16, 0.08) * line;
}

void main() {
    if (uMode == 1) {
        outColor = vec4(mapColor(gl_FragCoord.xy), 1.0);
        return;
    }
    vec4 trap;
    int k = int(gl_FragCoord.x);
    if (k == 0) {
        outColor = vec4(sceneDE(uCamPos, trap), 0, 0, 1);
    } else if (k == 1) {
        outColor = vec4(march(uCamPos, uProbeDir, uMaxT), 0, 0, 1);
    } else if (k <= 5) {
        const vec3 tet[4] = vec3[](vec3(1, -1, -1), vec3(-1, -1, 1), vec3(-1, 1, -1), vec3(1, 1, 1));
        outColor = vec4(DE(uCamPos + tet[k - 2] * uGradH, trap), 0, 0, 1);
    } else if (k == 6) {
        outColor = vec4(DE(uCamPos, trap), 0, 0, 1);
    } else if (k - 7 < uNumWhiskers) {
        outColor = vec4(march(uCamPos, uWhisker[k - 7], uWhiskerRange), 0, 0, 1);
    } else {
        outColor = vec4(-1, 0, 0, 1);
    }
}

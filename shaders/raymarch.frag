// raymarch.frag — the 3D renderer.  Assembled at runtime as:
//   #version + common.glsl + fractal uniforms + fractals/<name>.glsl + this file
// The fractal file provides:  float DE(vec3 p, inout vec4 trap)
//
// Each invocation renders ONE jittered sample per pixel; the host accumulates
// samples with additive blending (alpha = sample count), so the image refines
// (anti-aliasing, soft shadows, depth of field, global illumination) while the
// camera is still.

layout(location = 0) out vec4 outColor;

uniform vec2  uResolution;
uniform int   uFrame;          // sample index since last reset
uniform float uTime;

// camera
uniform vec3  uCamPos, uCamRight, uCamUp, uCamFwd;
uniform float uTanHalfFov;
uniform float uAperture;       // lens radius as a fraction of focus distance
uniform float uFocusDist;
uniform float uSceneScale;     // distance camera->orbit target; keeps effects scale-invariant

// marching
uniform int   uMaxSteps;
uniform float uDetail;         // surface threshold in pixels (smaller = finer detail)
uniform float uStepFactor;     // DE multiplier (<1 for fractals whose DE overestimates)
uniform float uMaxDist;        // in units of uSceneScale

// lighting
uniform int   uRenderMode;     // 0 = real-time, 1 = path traced
uniform int   uBounces;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunSize;        // angular radius (radians) -> shadow softness
uniform int   uShadows;
uniform vec3  uSkyZenith, uSkyHorizon;
uniform float uSkyIntensity;
uniform int   uBackground;     // 0 sky, 1 solid color
uniform vec3  uBgColor;
uniform float uAOStrength;
uniform float uFogDensity;
uniform vec3  uFogColor;
uniform float uGlowStrength;
uniform vec3  uGlowColor;
uniform int   uFloor;          // optional ground plane that catches shadows
uniform float uFloorY;
uniform float uFloorSide;      // +1 camera above the ground plane, -1 below (it's two-sided)
uniform vec3  uFloorColor;

// material / coloring
uniform sampler2D uPalette;
uniform float uColorScale, uColorOffset;
uniform int   uColorMode;      // 0 trap.x, 1 trap.y, 2 trap.z (iterations), 3 normal, 4 flat
uniform float uPaletteMix;
uniform vec3  uBaseColor;
uniform float uSpecular, uRoughness;

float g_pixelAngle;

// ------------------------------------------------------------------ environment
vec3 sky(vec3 rd, bool withSun) {
    if (uBackground == 1) return uBgColor;
    float h = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(uSkyHorizon, uSkyZenith, pow(max(h, 0.0), 0.5));
    col = mix(col, uSkyHorizon * 0.35, smoothstep(0.0, -0.4, h));    // darker below horizon
    col *= uSkyIntensity;
    if (withSun) {
        float sd = dot(rd, uSunDir);
        float cosR = cos(max(uSunSize, 0.004));
        col += uSunColor * smoothstep(cosR - 0.0005, cosR + 0.0005, sd) * 8.0;
        col += uSunColor * pow(max(sd, 0.0), 64.0) * 0.25;             // halo
    }
    return col;
}

// ------------------------------------------------------------------ marching
// The fractal, plus the ground plane if enabled (flagged with trap.w = -1).
float sceneDE(vec3 p, inout vec4 trap) {
    float d = DE(p, trap);
    if (uFloor != 0) {
        // signed toward the camera's side, so normals stay well defined on the plane
        float fd = (p.y - uFloorY) * uFloorSide;
        if (fd < d) { d = fd; trap = vec4(0, 0, 0, -1); }
    }
    return d;
}
float mapDE(vec3 p) { vec4 t; return sceneDE(p, t); }

struct Hit { bool hit; float t; int steps; vec4 trap; };

// Marches the fractal only; the ground plane is intersected analytically, so it
// costs nothing and extends all the way to the horizon.
Hit march(vec3 ro, vec3 rd, float tStart, float maxT, float epsScale) {
    Hit h; h.hit = false; h.t = tStart; h.steps = 0; h.trap = vec4(0);
    float tFloor = 1e30;
    if (uFloor != 0 && abs(rd.y) > 1e-7) {  // from above or below
        float tf = (uFloorY - ro.y) / rd.y;
        if (tf > tStart) tFloor = tf;
    }
    float tEnd = min(maxT, tFloor);
    for (int i = 0; i < uMaxSteps; i++) {
        vec4 trap;
        float d = DE(ro + rd * h.t, trap);
        float eps = max(h.t * g_pixelAngle * uDetail * epsScale, 1e-7);
        h.steps = i;
        if (d < eps) { h.hit = true; h.trap = trap; return h; }
        h.t += d * uStepFactor;
        if (h.t > tEnd) break;
    }
    if (tFloor < 1e29) { h.hit = true; h.t = tFloor; h.trap = vec4(0, 0, 0, -1); }
    return h;
}

// Far away, the ground dissolves into the horizon instead of ending at an edge.
vec3 floorFade(vec3 col, vec3 rd, float t, vec4 trap) {
    if (trap.w > -0.5) return col;
    return mix(col, sky(rd, false), 1.0 - exp(-t / (uSceneScale * 12.0)));
}

vec3 calcNormal(vec3 p, float e) {
    const vec2 k = vec2(1, -1);
    return normalize(k.xyy * mapDE(p + k.xyy * e) + k.yyx * mapDE(p + k.yyx * e) +
                     k.yxy * mapDE(p + k.yxy * e) + k.xxx * mapDE(p + k.xxx * e));
}

// Soft shadow by tracking the narrowest gap the shadow ray passes through (Quilez).
float softShadow(vec3 ro, vec3 rd, float tmin, float tmax, float k) {
    float res = 1.0, t = tmin;
    for (int i = 0; i < 160; i++) {
        float h = mapDE(ro + rd * t);
        res = min(res, k * h / t);
        t += clamp(h * uStepFactor, tmin * 0.5, tmax * 0.1);
        if (res < 0.002 || t > tmax) break;
    }
    return clamp(res, 0.0, 1.0);
}

// Hard occlusion test for the path tracer.
bool occluded(vec3 ro, vec3 rd, float tmin, float tmax, float eps) {
    float t = tmin;
    for (int i = 0; i < uMaxSteps; i++) {
        float h = mapDE(ro + rd * t);
        if (h < eps) return true;
        t += h * uStepFactor;
        if (t > tmax) break;
    }
    return false;
}

// Ambient occlusion: sample the DE along the normal; if the field is smaller
// than the distance travelled, something is nearby.
float ambientOcclusion(vec3 p, vec3 n, float radius) {
    float occ = 0.0, w = 1.0;
    for (int i = 1; i <= 5; i++) {
        float h = radius * float(i) / 5.0;
        occ += w * max(h - mapDE(p + n * h), 0.0) / h;
        w *= 0.6;
    }
    return clamp(1.0 - occ * 0.45, 0.0, 1.0);
}

// ------------------------------------------------------------------ material
// trap.w selects a special material: -1 = ground plane, 1 = water (glossy, dark)
float g_spec, g_rough;
void surfaceMaterial(vec4 trap) {
    g_spec = uSpecular; g_rough = uRoughness;
    if (trap.w > 0.5) { g_spec = 0.9; g_rough = 0.04; }
    else if (trap.w < -0.5) { g_spec = 0.1; g_rough = 0.6; }
}

vec3 albedo(vec4 trap, vec3 n) {
    if (trap.w > 0.5) return vec3(0.02, 0.03, 0.05);
    if (trap.w < -0.5) return uFloorColor;
    float v;
    if      (uColorMode == 0) v = trap.x;
    else if (uColorMode == 1) v = trap.y;
    else if (uColorMode == 2) v = trap.z;
    else if (uColorMode == 3) return mix(uBaseColor, n * 0.5 + 0.5, uPaletteMix);
    else return uBaseColor;
    vec3 pal = texture(uPalette, vec2(v * uColorScale + uColorOffset, 0.5)).rgb;
    return mix(uBaseColor, pal, uPaletteMix);
}

vec3 sampleCone(vec3 dir, float angle) {
    vec2 r = rand2();
    float cosT = mix(1.0, cos(angle), r.x);
    float sinT = sqrt(1.0 - cosT * cosT);
    float phi = TAU * r.y;
    vec3 t = normalize(cross(abs(dir.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0), dir));
    vec3 b = cross(dir, t);
    return normalize(t * cos(phi) * sinT + b * sin(phi) * sinT + dir * cosT);
}

vec3 cosineHemisphere(vec3 n) {
    vec2 r = rand2();
    float phi = TAU * r.x, s = sqrt(r.y);
    vec3 t = normalize(cross(abs(n.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0), n));
    vec3 b = cross(n, t);
    return normalize(t * cos(phi) * s + b * sin(phi) * s + n * sqrt(1.0 - r.y));
}

// ------------------------------------------------------------------ shading
vec3 shadeRealtime(vec3 ro, vec3 rd, Hit h, float maxT) {
    float stepRatio = float(h.steps) / float(uMaxSteps);
    vec3 glow = uGlowColor * uGlowStrength * pow(stepRatio, 1.5) * 2.0;

    if (!h.hit) return sky(rd, true) + glow;

    vec3 p = ro + rd * h.t;
    float eps = max(h.t * g_pixelAngle * uDetail, 1e-7);
    vec3 n = calcNormal(p, eps * 0.5);
    vec3 alb = albedo(h.trap, n);
    surfaceMaterial(h.trap);

    float sh = 1.0;
    if (uShadows != 0)
        sh = softShadow(p + n * eps * 4.0, uSunDir, eps * 4.0, uSceneScale * 4.0, 1.0 / max(uSunSize, 0.005));
    float ao = mix(1.0, ambientOcclusion(p, n, h.t * 0.06), uAOStrength);
    ao *= mix(1.0, 1.0 - stepRatio, uAOStrength * 0.6);              // step-count AO (the classic look)

    float ndl = max(dot(n, uSunDir), 0.0);
    vec3 skyAmb = mix(uSkyHorizon, uSkyZenith, n.y * 0.5 + 0.5) * uSkyIntensity;
    if (uBackground == 1) skyAmb = mix(uBgColor, vec3(0.5), 0.5) * uSkyIntensity;
    vec3 col = alb * (uSunColor * ndl * sh + skyAmb * ao * 0.6);
    col += alb * uSunColor * 0.08 * max(dot(n, -uSunDir), 0.0) * ao;  // fake bounce light

    vec3 hv = normalize(uSunDir - rd);
    float shin = exp2(10.0 * (1.0 - g_rough) + 1.0);
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, -rd), 0.0), 5.0);
    col += g_spec * uSunColor * sh * pow(max(dot(n, hv), 0.0), shin) * (shin + 8.0) / 25.0 * ndl;
    col += g_spec * fres * sky(reflect(rd, n), false) * ao * 0.5;

    col = floorFade(col, rd, h.t, h.trap);
    col += glow;
    return col;
}

vec3 tracePath(vec3 ro, vec3 rd, out float firstT) {
    vec3 radiance = vec3(0), throughput = vec3(1);
    firstT = -1.0;
    float maxT = uMaxDist * uSceneScale;
    float eps0 = 0.0;
    for (int b = 0; b <= uBounces; b++) {
        Hit h = march(ro, rd, 0.0, maxT, b == 0 ? 1.0 : 2.0);
        if (b == 0) {
            float stepRatio = float(h.steps) / float(uMaxSteps);
            radiance += uGlowColor * uGlowStrength * pow(stepRatio, 1.5) * 2.0;
        }
        if (!h.hit) {
            radiance += throughput * sky(rd, b == 0);
            break;
        }
        vec3 p = ro + rd * h.t;
        if (b == 0) { firstT = h.t; eps0 = max(h.t * g_pixelAngle * uDetail, 1e-7); }
        float fade = h.trap.w < -0.5 ? 1.0 - exp(-h.t / (uSceneScale * 12.0)) : 0.0;
        if (fade > 0.0) { radiance += throughput * fade * sky(rd, false); throughput *= 1.0 - fade; }
        vec3 n = calcNormal(p, eps0 * 0.5);
        vec3 alb = albedo(h.trap, n);
        surfaceMaterial(h.trap);
        vec3 pOff = p + n * eps0 * 4.0;

        bool glossy = rand() < g_spec;
        if (!glossy) {
            // next-event estimation toward the sun
            vec3 L = sampleCone(uSunDir, max(uSunSize, 0.002));
            float ndl = dot(n, L);
            if (ndl > 0.0 && (uShadows == 0 || !occluded(pOff, L, eps0 * 2.0, maxT, eps0)))
                radiance += throughput * alb * uSunColor * ndl;
            throughput *= alb;
            rd = cosineHemisphere(n);
        } else {
            vec3 r = reflect(rd, n);
            rd = normalize(mix(r, cosineHemisphere(n), g_rough * g_rough));
            if (dot(rd, n) < 0.0) break;
            float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, -rd), 0.0), 5.0);
            throughput *= mix(vec3(fres), alb, 0.3);
        }
        ro = pOff;
        maxT = uMaxDist * uSceneScale;
        // Russian roulette after two bounces
        if (b >= 2) {
            float q = max(throughput.r, max(throughput.g, throughput.b));
            if (rand() > q) break;
            throughput /= q;
        }
    }
    return radiance;
}

// ------------------------------------------------------------------ main
void main() {
    uvec2 pix = uvec2(gl_FragCoord.xy);
    rngSeed(pix, uint(uFrame));

    vec2 jitter = uFrame == 0 ? vec2(0.5) : rand2();
    vec2 uv = (gl_FragCoord.xy - 0.5 + jitter - 0.5 * uResolution) / uResolution.y;
    g_pixelAngle = 2.0 * uTanHalfFov / uResolution.y;

    vec3 ro = uCamPos;
    vec3 rd = normalize(uCamFwd + (uv.x * uCamRight + uv.y * uCamUp) * 2.0 * uTanHalfFov);

    // thin-lens depth of field
    if (uAperture > 0.0 && uFrame > 0) {
        vec3 focusP = ro + rd * (uFocusDist / dot(rd, uCamFwd));
        vec2 lens = rand2();
        float r = sqrt(lens.x) * uAperture * uFocusDist * 0.1, a = TAU * lens.y;
        ro += (uCamRight * cos(a) + uCamUp * sin(a)) * r;
        rd = normalize(focusP - ro);
    }

    vec3 col;
    float t;
    float maxT = uMaxDist * uSceneScale;
    if (uRenderMode == 1) {
        col = tracePath(ro, rd, t);
    } else {
        Hit h = march(ro, rd, 0.0, maxT, 1.0);
        col = shadeRealtime(ro, rd, h, maxT);
        t = h.hit ? h.t : -1.0;
    }

    // distance fog (relative to scene scale so it behaves at any zoom level)
    if (uFogDensity > 0.0) {
        float fd = t < 0.0 ? 1e9 : t / uSceneScale;
        col = mix(uFogColor, col, exp(-fd * uFogDensity));
    }

    // guard against NaN/Inf fireflies poisoning the accumulation buffer
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0);
    col = min(col, vec3(64.0));
    outColor = vec4(col, 1.0);
}

// probe.frag — tiny 2x1 pass read back by the CPU.
//   pixel 0: distance estimate at the camera (drives automatic fly speed)
//   pixel 1: distance along uProbeDir to the surface (click-to-focus / orbit pick), -1 on miss
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

float sceneDE(vec3 p, inout vec4 trap) {
    float d = DE(p, trap);
    return uFloor != 0 ? min(d, abs(p.y - uFloorY)) : d;
}

void main() {
    vec4 trap;
    if (int(gl_FragCoord.x) == 0) {
        outColor = vec4(sceneDE(uCamPos, trap), 0, 0, 1);
        return;
    }
    float t = 0.0;
    for (int i = 0; i < uMaxSteps; i++) {
        float d = sceneDE(uCamPos + uProbeDir * t, trap);
        if (d < max(t * uPixelAngle * uDetail, 1e-7)) { outColor = vec4(t, 0, 0, 1); return; }
        t += d * uStepFactor;
        if (t > uMaxT) break;
    }
    outColor = vec4(-1, 0, 0, 1);
}

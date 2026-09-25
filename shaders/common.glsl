// common.glsl — helpers shared by every fractal and render shader.
// Fractal files (fractals/*.glsl) can use anything defined here.

#define PI  3.14159265358979
#define TAU 6.28318530717959
#define PHI 1.61803398874989

// ---------------------------------------------------------------- rotations
mat2 rot2(float a) { float c = cos(a), s = sin(a); return mat2(c, s, -s, c); }

mat3 rotX(float a) { float c = cos(a), s = sin(a); return mat3(1, 0, 0,  0, c, s,  0, -s, c); }
mat3 rotY(float a) { float c = cos(a), s = sin(a); return mat3(c, 0, -s,  0, 1, 0,  s, 0, c); }
mat3 rotZ(float a) { float c = cos(a), s = sin(a); return mat3(c, s, 0,  -s, c, 0,  0, 0, 1); }

// Rotation about a unit `axis` (Rodrigues). Note GLSL mat3() is column-major, so as
// written this is the transpose: it rotates vectors by -angle.
mat3 rotAxis(vec3 k, float angle) {
    float c = cos(angle), s = sin(angle), t = 1.0 - c;
    return mat3(t * k.x * k.x + c,       t * k.x * k.y + s * k.z, t * k.x * k.z - s * k.y,
                t * k.x * k.y - s * k.z, t * k.y * k.y + c,       t * k.y * k.z + s * k.x,
                t * k.x * k.z + s * k.y, t * k.y * k.z - s * k.x, t * k.z * k.z + c);
}

// Euler angles in degrees -> rotation matrix (Z * Y * X)
mat3 rotEulerDeg(vec3 deg) {
    vec3 r = radians(deg);
    return rotZ(r.z) * rotY(r.y) * rotX(r.x);
}

// ---------------------------------------------------------------- folds
// "Folding" = reflecting space across a plane so both halves map onto one.
// Repeating folds + scaling is how most 3D IFS fractals are built.

// Box fold: reflect anything outside [-limit, limit] back inside.
void boxFold(inout vec3 z, float limit) { z = clamp(z, -limit, limit) * 2.0 - z; }

// Sphere fold: invert points inside a sphere (the heart of the Mandelbox).
void sphereFold(inout vec3 z, inout float dz, float minR2, float fixedR2) {
    float r2 = dot(z, z);
    if (r2 < minR2) {
        float t = fixedR2 / minR2; z *= t; dz *= t;
    } else if (r2 < fixedR2) {
        float t = fixedR2 / r2; z *= t; dz *= t;
    }
}

// Tetrahedral symmetry folds (Sierpinski tetrahedron).
void tetraFold(inout vec3 z) {
    if (z.x + z.y < 0.0) z.xy = -z.yx;
    if (z.x + z.z < 0.0) z.xz = -z.zx;
    if (z.y + z.z < 0.0) z.zy = -z.yz;
}

// Octahedral / cubic symmetry folds (Menger sponge, octahedral KIFS).
void octaFold(inout vec3 z) {
    z = abs(z);
    if (z.x < z.y) z.xy = z.yx;
    if (z.x < z.z) z.xz = z.zx;
    if (z.y < z.z) z.yz = z.zy;
}

// ---------------------------------------------------------------- complex numbers (vec2 = a + bi)
vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 cdiv(vec2 a, vec2 b) { return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / dot(b, b); }
vec2 csqr(vec2 a) { return vec2(a.x * a.x - a.y * a.y, 2.0 * a.x * a.y); }
vec2 cpow(vec2 z, float n) {
    float r = length(z), a = atan(z.y, z.x);
    return pow(r, n) * vec2(cos(a * n), sin(a * n));
}

// ---------------------------------------------------------------- quaternions (vec4 = x + yi + zj + wk)
vec4 qmul(vec4 a, vec4 b) {
    return vec4(a.x * b.x - dot(a.yzw, b.yzw),
                a.x * b.yzw + b.x * a.yzw + cross(a.yzw, b.yzw));
}
vec4 qsqr(vec4 a) { return vec4(a.x * a.x - dot(a.yzw, a.yzw), 2.0 * a.x * a.yzw); }

// ---------------------------------------------------------------- distance primitives
float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// ---------------------------------------------------------------- random numbers
uint pcgHash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
uint g_rng;
void rngSeed(uvec2 pixel, uint frame) { g_rng = pcgHash(pixel.x + pcgHash(pixel.y + pcgHash(frame))); }
float rand() { g_rng = pcgHash(g_rng); return float(g_rng) * (1.0 / 4294967296.0); }
vec2 rand2() { return vec2(rand(), rand()); }

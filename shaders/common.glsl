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

// Rotation by `angle` radians (right-handed) about a unit axis k — Rodrigues'
// formula. mat3() takes columns, so each group of three below is one column.
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

// Monte Carlo sampling for the renderer. Each pair of dimensions (sub-pixel
// jitter, lens, sun direction, bounce direction, ...) takes the next point of a
// 2D Sobol sequence, Owen-scrambled per dimension (Burley 2020, "Practical
// hash-based Owen scrambling"), so every pixel's samples are well stratified
// over time and converge much faster than independent random numbers. Each
// pixel then shifts its points by a blue-noise value (Cranley-Patterson
// rotation), which spreads the remaining error as fine, even grain instead of
// clumps: the image looks better long before it converges.
//
// The mask is 64x64, so on its own every 64th pixel would get the very same
// samples and the noise would repeat as a faint grid. Each 64x64 tile of the
// screen therefore gets its own scrambling seed and its own offset into the mask.
uniform sampler2D uBlueNoise;  // 64x64, two independent void-and-cluster masks
uint g_sampleIndex, g_dim, g_tileSeed;
ivec2 g_pixel, g_tileShift;

uint lkPermute(uint x, uint seed) {  // Laine-Karras style hash permutation
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
    return x;
}
uint nestedScramble(uint x, uint seed) { return bitfieldReverse(lkPermute(bitfieldReverse(x), seed)); }
uint sobolDim1(uint i) {  // second Sobol dimension (primitive polynomial x + 1)
    uint r = 0u, v = 1u << 31;
    for (; i != 0u; i >>= 1, v ^= v >> 1)
        if ((i & 1u) != 0u) r ^= v;
    return r;
}

void rngSeed(uvec2 pixel, uint frame) {
    g_pixel = ivec2(pixel);
    g_sampleIndex = frame;
    g_dim = 0u;
    uvec2 tile = pixel >> 6u;
    g_tileSeed = pcgHash(tile.x * 0x8da6b343u ^ pcgHash(tile.y + 0x68e31da4u));
    g_tileShift = ivec2(g_tileSeed & 63u, (g_tileSeed >> 6u) & 63u);
}
vec2 rand2() {
    uint seed = pcgHash(g_dim * 0x9E3779B9u + 0x5bd1e995u ^ g_tileSeed);
    uint idx = nestedScramble(g_sampleIndex, seed);  // shuffle: decorrelates dimension pairs
    uvec2 s = uvec2(bitfieldReverse(idx), sobolDim1(idx));
    s.x = nestedScramble(s.x, pcgHash(seed ^ 0xa511e9b3u));
    s.y = nestedScramble(s.y, pcgHash(seed ^ 0x63d83595u));
    // a different, fixed window of the blue-noise tile for every dimension pair
    vec2 shift = texelFetch(uBlueNoise, (g_pixel + g_tileShift + ivec2(int(g_dim) * 23, int(g_dim) * 41)) & 63, 0).rg;
    g_dim++;
    return fract(vec2(s) * (1.0 / 4294967296.0) + shift);
}
float rand() { return rand2().x; }

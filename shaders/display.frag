// display.frag — turns the render buffer into screen pixels.
//   mode 0 (3D): accumulated radiance / sample count -> exposure -> tonemap -> sRGB
//   mode 1 (2D): iteration buffer (value + aux) -> palette (with cycling) -> supersample resolve
// Then the optional retro filter: pixelate, quantize to the VGA/EGA palette with
// ordered dithering, and CRT scanlines.
in vec2 vUV;
layout(location = 0) out vec4 outColor;

uniform int   uMode;
uniform sampler2D uAccum;        // mode 0: RGBA32F, rgb = sum, a = sample count
uniform sampler2D uIndex;        // mode 1: R32F iteration values at uSS x resolution
uniform sampler2D uAux;          // mode 1: R8 aux values (root, zmag, angle, stripe, trap)
uniform sampler2D uPalette;      // 256x1 sRGB texture (sampled as linear)
uniform sampler3D uRetroLut;     // 32^3 nearest-VGA/EGA-color table
uniform vec2  uOutSize;          // output size in pixels
uniform float uExposure;
uniform int   uTonemap;          // 0 ACES, 1 Reinhard, 2 none
uniform float uVignette;
uniform float uSaturation;

// 2D
uniform int   uSS;
uniform float uIndexScale;   // index size / output size (< 1: a reduced preview, drawn while the view moves)
uniform int   uBanded;           // 1 = classic integer iteration bands (nearest palette entry)
uniform int   uColoring;         // outside coloring mode (see kColoringModes)
uniform float uCycleOffset;      // palette rotation, in palette entries
uniform float uColorDensity;     // palette entries per iteration
uniform int   uInsideMode;       // 0 black, 1 zmag, 2 solid color
uniform vec3  uInsideColor;
uniform float uRootSpread;       // palette offset per Newton root

// retro
uniform int   uRetro;            // 0 off, 1 VGA 256, 2 EGA 16
uniform int   uPixelSize;
uniform float uScanlines;

vec3 srgbToLinear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }
vec3 linearToSrgb(vec3 c) { c = max(c, 0.0); return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 paletteColor(float v, float aux) {
    if (v < -1.5) return vec3(0);                       // not computed yet (progressive render)
    if (v < 0.0) {
        if (uInsideMode == 0) return vec3(0);
        if (uInsideMode == 2) return srgbToLinear(uInsideColor);
        float e = aux * 256.0 + uCycleOffset;           // Fractint "zmag" inside coloring
        return texelFetch(uPalette, ivec2(int(mod(floor(e), 256.0)), 0), 0).rgb;
    }
    float e;
    bool bands = uBanded == 1;
    if (uColoring == 0 || uColoring == 6) {             // escape time (+ Newton root offset)
        e = v * uColorDensity + floor(aux * 4.0) * uRootSpread + uCycleOffset;
    } else if (uColoring == 1) {                        // decomposition: half a palette apart
        e = v * uColorDensity + aux * 128.0 + uCycleOffset;
    } else {                                            // angle / stripes / traps: aux spans the palette
        e = aux * 255.0 * uColorDensity + uCycleOffset;
        bands = false;
    }
    if (bands) {
        // index 0 is reserved for "inside" in Fractint; escaped pixels start at 1
        int idx = int(mod(floor(e), 255.0)) + 1;  // e = escape iteration (0-based) when density = 1
        return texelFetch(uPalette, ivec2(idx, 0), 0).rgb;
    }
    return texture(uPalette, vec2((e + 0.5) / 256.0, 0.5)).rgb;
}

vec3 indexColor(ivec2 p) { return paletteColor(texelFetch(uIndex, p, 0).r, texelFetch(uAux, p, 0).r); }

vec3 sceneColor(vec2 fragPx) {
    if (uMode == 0) {
        vec4 acc = texture(uAccum, fragPx / uOutSize);
        vec3 c = acc.rgb / max(acc.a, 1.0) * uExposure;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = max(mix(vec3(l), c, uSaturation), 0.0);
        if (uTonemap == 0) c = aces(c);
        else if (uTonemap == 1) c = c / (1.0 + c);
        return c;
    }
    if (uSS == 1 && uIndexScale < 0.999) {  // reduced preview: blend the four nearest colors
        vec2 sp = fragPx * uIndexScale - 0.5;
        ivec2 i0 = ivec2(floor(sp)), hi = textureSize(uIndex, 0) - 1;
        vec2 f = sp - vec2(i0);
        vec3 c00 = indexColor(clamp(i0, ivec2(0), hi)), c10 = indexColor(clamp(i0 + ivec2(1, 0), ivec2(0), hi));
        vec3 c01 = indexColor(clamp(i0 + ivec2(0, 1), ivec2(0), hi)), c11 = indexColor(clamp(i0 + ivec2(1, 1), ivec2(0), hi));
        return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
    }
    ivec2 base = ivec2(fragPx) * uSS;
    vec3 sum = vec3(0);
    for (int j = 0; j < uSS; j++)
        for (int i = 0; i < uSS; i++)
            sum += paletteColor(texelFetch(uIndex, base + ivec2(i, j), 0).r, texelFetch(uAux, base + ivec2(i, j), 0).r);
    return sum / float(uSS * uSS);
}

const float bayer4[16] = float[](0., 8., 2., 10., 12., 4., 14., 6., 3., 11., 1., 9., 15., 7., 13., 5.);

vec3 retroQuantize(vec3 srgb, ivec2 cell) {
    float d = (bayer4[(cell.y & 3) * 4 + (cell.x & 3)] + 0.5) / 16.0 - 0.5;
    srgb += d * (uRetro == 2 ? 0.30 : 0.06);
    return texture(uRetroLut, clamp(srgb, 0.0, 1.0)).rgb;   // nearest palette color, precomputed
}

void main() {
    vec2 px = gl_FragCoord.xy;
    int ps = max(uPixelSize, 1);
    ivec2 cell = ivec2(px) / ps;
    if (uRetro != 0 || ps > 1) px = (vec2(cell) + 0.5) * float(ps);

    vec3 lin = sceneColor(uMode == 1 ? floor(px) : px);

    if (uMode == 0 && uVignette > 0.0) {
        vec2 q = vUV * 2.0 - 1.0;
        lin *= 1.0 - uVignette * 0.5 * dot(q, q);
    }

    vec3 c = linearToSrgb(lin);
    if (uRetro != 0) c = retroQuantize(c, cell);
    if (uScanlines > 0.0) {
        float line = mod(gl_FragCoord.y, float(max(ps, 2)));
        c *= 1.0 - uScanlines * step(float(max(ps, 2)) * 0.5, line);
    }
    // tiny blue-noise-ish dither to hide 8-bit banding in smooth gradients
    if (uRetro == 0) {
        float n = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
        c += (n - 0.5) / 255.0;
    }
    outColor = vec4(c, 1.0);
}

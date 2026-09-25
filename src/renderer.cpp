#include "renderer.h"

#include "bluenoise.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "stb_image_write.h"

namespace fs = std::filesystem;

bool writePngRaw(const std::string& path, int w, int h, const uint8_t* rgba) {
    stbi_flip_vertically_on_write(1);
    return stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) != 0;
}

void paramEffective(const Param& p, float t, float out[4]) {
    for (int k = 0; k < 4; k++) out[k] = p.value[k];
    if (!p.animate) return;
    if (p.type == ParamType::Bool || p.type == ParamType::Choice || p.type == ParamType::Int) return;
    float range = p.maxV - p.minV;
    for (int k = 0; k < p.components(); k++) {
        float s = std::sin(6.2831853f * p.animSpeed * t + k * 1.7f);
        float v = p.value[k] + s * p.animDepth * range * 0.5f;
        out[k] = std::clamp(v, p.minV, p.maxV);
    }
}

// ------------------------------------------------------------------ init / core shaders
bool Renderer::init(const fs::path& dataDir, std::string& err) {
    dataDir_ = dataDir;
    glCreateVertexArrays(1, &vao_);

    glCreateTextures(GL_TEXTURE_2D, 1, &paletteTex_);
    glTextureStorage2D(paletteTex_, 1, GL_SRGB8_ALPHA8, 256, 1);
    glTextureParameteri(paletteTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(paletteTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(paletteTex_, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(paletteTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    buildRetroLuts();

    // two independent blue-noise masks (x and y shifts for the sampler in common.glsl)
    std::vector<float> a = makeBlueNoise(64, 0x1234567u), b = makeBlueNoise(64, 0x89abcdefu), rg(64 * 64 * 2);
    for (int i = 0; i < 64 * 64; i++) rg[i * 2] = a[i], rg[i * 2 + 1] = b[i];
    glCreateTextures(GL_TEXTURE_2D, 1, &blueNoise_);
    glTextureStorage2D(blueNoise_, 1, GL_RG32F, 64, 64);
    glTextureSubImage2D(blueNoise_, 0, 0, 0, 64, 64, GL_RG, GL_FLOAT, rg.data());
    glTextureParameteri(blueNoise_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(blueNoise_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    probeRT_.ensure(2, 1, GL_RG32F, GL_NEAREST);
    glCreateBuffers(2, probePbo_);
    for (auto b : probePbo_) glNamedBufferStorage(b, 16, nullptr, GL_MAP_READ_BIT);

    if (!loadCore(err)) return false;
    coreTime_ = newestShaderTime();
    return true;
}

// The retro filter snaps colors to the VGA (or EGA) palette. Instead of searching
// 248 entries per pixel, precompute the nearest entry for a 32^3 grid of colors.
void Renderer::buildRetroLuts() {
    const int N = 32;
    Palette vga = vgaDefaultPalette();
    for (int which = 0; which < 2; which++) {
        int count = which == 0 ? 248 : 16;
        std::vector<uint8_t> lut((size_t)N * N * N * 4);
        for (int b = 0; b < N; b++)
            for (int g = 0; g < N; g++)
                for (int r = 0; r < N; r++) {
                    float c[3] = {(r + 0.5f) / N, (g + 0.5f) / N, (b + 0.5f) / N};
                    int best = 0;
                    float bestD = 1e9f;
                    for (int i = 0; i < count; i++) {
                        float d = 0;
                        const float wts[3] = {0.30f, 0.59f, 0.11f};
                        for (int k = 0; k < 3; k++) {
                            float e = (c[k] - vga.rgba[i * 4 + k] / 255.0f) * wts[k];
                            d += e * e;
                        }
                        if (d < bestD) bestD = d, best = i;
                    }
                    size_t o = (((size_t)b * N + g) * N + r) * 4;
                    for (int k = 0; k < 4; k++) lut[o + k] = vga.rgba[best * 4 + k];
                }
        glCreateTextures(GL_TEXTURE_3D, 1, &retroLut_[which]);
        glTextureStorage3D(retroLut_[which], 1, GL_RGBA8, N, N, N);
        glTextureSubImage3D(retroLut_[which], 0, 0, 0, 0, N, N, N, GL_RGBA, GL_UNSIGNED_BYTE, lut.data());
        glTextureParameteri(retroLut_[which], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(retroLut_[which], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
            glTextureParameteri(retroLut_[which], wrap, GL_CLAMP_TO_EDGE);
    }
}

void Renderer::shutdown() {
    // GL objects must go while the context still exists (move-assign swaps, the temporary deletes)
    progs_.clear();
    displayProg_ = Program();
    for (auto& row : classic_)
        for (auto& p : row) p = Program();
    classicDeep_ = Program();
    if (refSsbo_) glDeleteBuffers(1, &refSsbo_);
    accum.release();
    index2D.release();
    for (auto& s : state2D_) s.release();
    probeRT_.release();
    shotRT_.release();
    for (auto& f : probeFence_)
        if (f) glDeleteSync(f), f = nullptr;
    glDeleteBuffers(2, probePbo_);
    glDeleteTextures(1, &paletteTex_);
    glDeleteTextures(2, retroLut_);
    glDeleteTextures(1, &blueNoise_);
    glDeleteVertexArrays(1, &vao_);
}

fs::file_time_type Renderer::newestShaderTime() const {
    fs::file_time_type t = fs::file_time_type::min();  // NB: file_time_type{} is libstdc++'s epoch in 2174
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dataDir_ / "shaders", ec)) {
        auto m = fs::last_write_time(e.path(), ec);
        if (!ec && m > t) t = m;
    }
    return t;
}

bool Renderer::loadCore(std::string& err) {
    auto rd = [&](const char* name, std::string& out) {
        bool ok = false;
        std::string s = readTextFile((dataDir_ / "shaders" / name).string(), &ok);
        if (!ok) {
            err += std::string("missing shaders/") + name + "\n";
            return false;
        }
        out = std::move(s);
        return true;
    };
    std::string vert, common, raymarch, probe, classic, display;
    if (!rd("fullscreen.vert", vert) || !rd("common.glsl", common) || !rd("raymarch.frag", raymarch) ||
        !rd("probe.frag", probe) || !rd("classic2d.comp", classic) || !rd("display.frag", display))
        return false;

    Program disp;
    if (!disp.build(vert, {{"header", "#version 460\n"}, {"common.glsl", common}, {"display.frag", display}})) {
        err += disp.error();
        return false;
    }
    // Check the classic compute shader compiles before accepting the new sources.
    Program c32;
    if (!c32.buildCompute({{"header", "#version 460\n"}, {"common.glsl", common}, {"classic2d.comp", classic}})) {
        err += c32.error();
        return false;
    }
    // swap in only after everything compiled, so a typo during live editing keeps the old shaders running
    std::swap(displayProg_, disp);
    for (auto& row : classic_)
        for (auto& p : row) p = Program();
    std::memset(classicBuilt_, 0, sizeof classicBuilt_);
    classic_[0][0] = std::move(c32);
    classicBuilt_[0][0] = true;
    classicDeep_ = Program();
    classicDeepBuilt_ = false;
    vert_ = vert;
    common_ = common;
    raymarch_ = raymarch;
    probeSrc_ = probe;
    classicSrc_ = classic;
    return true;
}

Program* Renderer::classicProgram(bool fp64, bool custom) {
    if (custom && customGlsl_.empty()) return nullptr;
    Program& p = classic_[fp64][custom];
    if (!classicBuilt_[fp64][custom]) {
        classicBuilt_[fp64][custom] = true;
        std::string header = "#version 460\n";
        if (fp64) header += "#define FP64\n";
        if (custom) header += "#define CUSTOM_FORMULA\n";
        std::vector<ShaderChunk> chunks = {{"header", header}, {"common.glsl", common_}};
        if (custom) chunks.push_back({"(your formula)", customGlsl_});
        chunks.push_back({"classic2d.comp", classicSrc_});
        if (!p.buildCompute(chunks) && custom) customError_ = p.error();
    }
    return p.valid() ? &p : nullptr;
}

void Renderer::setCustomFormula(const std::string& glsl) {
    customGlsl_ = glsl;
    customError_.clear();
    for (int fp = 0; fp < 2; fp++) {
        classic_[fp][1] = Program();
        classicBuilt_[fp][1] = false;
    }
    if (!glsl.empty()) classicProgram(false, true);  // compile now so errors show immediately
    progs_.erase("landscape");                       // it embeds the formula too
}

void Renderer::setFormulaParams(const float p1[2], const float p2[2], const float p3[2]) {
    float v[6] = {p1[0], p1[1], p2[0], p2[1], p3[0], p3[1]};
    std::copy(v, v + 6, formulaP_);
}

bool Renderer::reloadCoreIfChanged() {
    auto t = newestShaderTime();
    if (t == coreTime_) return false;
    coreTime_ = t;
    std::string err;
    if (loadCore(err)) {
        coreError.clear();
        progs_.clear();
    } else {
        coreError = err;
    }
    return true;
}

void Renderer::setPalette(const Palette& p) {
    glTextureSubImage2D(paletteTex_, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.rgba.data());
}

// ------------------------------------------------------------------ fractal programs
Renderer::FractalPrograms& Renderer::programs(const Fractal& f) {
    auto& slot = progs_[f.key];
    if (!slot) slot = std::make_unique<FractalPrograms>();
    FractalPrograms& fp = *slot;
    if (fp.started) return fp;
    fp.started = true;
    fp.error.clear();
    std::string fname = f.path.filename().string();
    // The landscape can use the user's 2D formula: splice it in when there is one.
    bool withFormula = f.key == "landscape" && hasCustomFormula();
    std::vector<ShaderChunk> trace = {{"header", withFormula ? "#version 460\n#define HAVE_CUSTOM_FORMULA\n" : "#version 460\n"},
                                      {"common.glsl", common_}};
    if (withFormula) trace.push_back({"(your formula)", customGlsl_});
    trace.push_back({"(parameters of " + fname + ")", f.uniformDecls()});
    trace.push_back({fname, f.code});
    trace.push_back({"raymarch.frag", raymarch_});
    fp.trace.buildAsync(vert_, trace);
    auto probe = trace;
    probe.back() = {"probe.frag", probeSrc_};
    fp.probe.buildAsync(vert_, probe);
    return fp;
}

Renderer::ProgStatus Renderer::status(const Fractal& f) {
    FractalPrograms& fp = programs(f);
    BuildState a = fp.trace.poll(), b = fp.probe.poll();
    if (a == BuildState::Pending || b == BuildState::Pending) return ProgStatus::Compiling;
    if (a == BuildState::Failed || b == BuildState::Failed) {
        fp.error = a == BuildState::Failed ? fp.trace.error() : fp.probe.error();
        return ProgStatus::Error;
    }
    return ProgStatus::Ready;
}

void Renderer::warmUp(const std::vector<Fractal>& all) {
    for (auto& f : all) programs(f);
}

// ------------------------------------------------------------------ 3D
static void setFractalParams(Program& prog, const Fractal& f, float animTime) {
    for (auto& p : f.params) {
        float v[4];
        paramEffective(p, animTime, v);
        GLint l = prog.loc(p.id.c_str());
        if (l < 0) continue;
        switch (p.type) {
        case ParamType::Float: glProgramUniform1f(prog.id(), l, v[0]); break;
        case ParamType::Int:
        case ParamType::Choice:
        case ParamType::Bool: glProgramUniform1i(prog.id(), l, (int)std::lround(v[0])); break;
        case ParamType::Vec2: glProgramUniform2f(prog.id(), l, v[0], v[1]); break;
        case ParamType::Vec3:
        case ParamType::Color: glProgramUniform3f(prog.id(), l, v[0], v[1], v[2]); break;
        case ParamType::Vec4: glProgramUniform4f(prog.id(), l, v[0], v[1], v[2], v[3]); break;
        }
    }
}

static Vec3 sunDirection(const RenderSettings& rs) {
    float az = rs.sunAzimuth * 0.0174533f, el = rs.sunElevation * 0.0174533f;
    return Vec3(std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el)).normalized();
}

void Renderer::clear3D(RenderTarget& t) {
    const float zero[4] = {0, 0, 0, 0};
    glClearNamedFramebufferfv(t.fbo, GL_COLOR, 0, zero);
}

bool Renderer::renderSample3D(RenderTarget& target, int sampleIndex, const Fractal& f, const RenderSettings& rs,
                              const View3D& v, const int* scissor) {
    if (status(f) != ProgStatus::Ready) return false;
    Program& p = programs(f).trace;

    p.set("uResolution", (float)v.fullW, (float)v.fullH);
    p.set("uFrame", sampleIndex);
    p.set("uTime", v.time);
    p.set3("uCamPos", v.pos.data());
    p.set3("uCamRight", v.right.data());
    p.set3("uCamUp", v.up.data());
    p.set3("uCamFwd", v.fwd.data());
    p.set("uTanHalfFov", v.tanHalfFov);
    p.set("uAperture", rs.aperture);
    p.set("uFocusDist", v.focusDist);
    p.set("uSceneScale", v.sceneScale);
    p.set("uMaxSteps", rs.maxSteps);
    p.set("uDetail", rs.detail);
    p.set("uStepFactor", rs.stepFactor);
    p.set("uMaxDist", rs.maxDist);
    p.set("uRenderMode", rs.renderMode);
    p.set("uBounces", rs.bounces);
    Vec3 sd = sunDirection(rs);
    p.set3("uSunDir", sd.data());
    p.set("uSunColor", rs.sunColor[0] * rs.sunIntensity, rs.sunColor[1] * rs.sunIntensity, rs.sunColor[2] * rs.sunIntensity);
    p.set("uSunSize", rs.sunSize * 0.0174533f);
    p.set("uShadows", (int)rs.shadows);
    p.set3("uSkyZenith", rs.skyZenith);
    p.set3("uSkyHorizon", rs.skyHorizon);
    p.set("uSkyIntensity", rs.skyIntensity);
    p.set("uBackground", rs.background);
    p.set3("uBgColor", rs.bgColor);
    p.set("uAOStrength", rs.aoStrength);
    p.set("uFogDensity", rs.fogDensity);
    p.set3("uFogColor", rs.fogColor);
    p.set("uGlowStrength", rs.glowStrength);
    p.set3("uGlowColor", rs.glowColor);
    p.set("uFloor", (int)rs.floorOn);
    p.set("uFloorY", rs.floorY);
    p.set("uFloorSide", v.pos.y >= rs.floorY ? 1.0f : -1.0f);
    p.set3("uFloorColor", rs.floorColor);
    p.set("uColorScale", rs.colorScale);
    p.set("uColorOffset", rs.colorOffset);
    p.set("uColorMode", rs.colorMode);
    p.set("uPaletteMix", rs.paletteMix);
    p.set3("uBaseColor", rs.baseColor);
    p.set("uSpecular", rs.specular);
    p.set("uRoughness", rs.roughness);
    p.set("uPalette", 0);
    p.set("uBlueNoise", 5);
    p.set("uP1", formulaP_[0], formulaP_[1]);  // only the landscape's custom formula uses these
    p.set("uP2", formulaP_[2], formulaP_[3]);
    p.set("uP3", formulaP_[4], formulaP_[5]);
    setFractalParams(p, f, v.animTime);

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.w, target.h);
    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBindTextureUnit(0, paletteTex_);
    glBindTextureUnit(5, blueNoise_);
    glBindVertexArray(vao_);
    p.use();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    return true;
}

// ------------------------------------------------------------------ 2D
bool Renderer::classicUsesDeep(const Classic2DSettings& cs, int targetH) const {
    if (cs.formula != 0) return false;  // perturbation is implemented for z^2 + c
    if (cs.fp64 == 3) return true;
    if (cs.fp64 != 2) return false;
    return cs.height / std::max(targetH, 1) < 1e-13;  // doubles start to run out here
}

void Renderer::setReferenceOrbit(const std::vector<float>& xy) {
    if (!refSsbo_) glCreateBuffers(1, &refSsbo_);
    glNamedBufferData(refSsbo_, (GLsizeiptr)std::max<size_t>(xy.size(), 2) * sizeof(float), xy.empty() ? nullptr : xy.data(),
                      GL_STATIC_DRAW);
    refLen_ = (int)(xy.size() / 2);
}

bool Renderer::classicUsesFp64(const Classic2DSettings& cs, int targetH) const {
    if (cs.formula == kCustomFormula) return false;  // user formulas use transcendental functions: float only
    if (cs.fp64 == 0) return false;
    if (cs.fp64 == 1) return true;
    double pixel = cs.height / std::max(targetH, 1);
    // A float has 24 mantissa bits; near |c| ~ 2 its spacing is ~2.4e-7. Switch
    // while a pixel still spans a couple of float steps.
    return pixel < 4e-7;
}

int Renderer::bandRowsFor(int width) const {
    const int kBandPixels = 1 << 20;  // 1M pixels of orbit state = 64 MB
    return std::max(1, kBandPixels / std::max(width, 1));
}

bool Renderer::dispatch2D(IndexTarget& out, const Classic2DSettings& cs, int y0, int rows, int chunk, bool first,
                          int stateSlot) {
    StateImages& state = state2D_[stateSlot & 1];
    bool custom = cs.formula == kCustomFormula;
    bool deep = classicUsesDeep(cs, out.h);
    Program* pp;
    if (deep) {
        if (!classicDeepBuilt_) {
            classicDeepBuilt_ = true;
            classicDeep_.buildCompute({{"header", "#version 460\n#define PERTURB\n"}, {"common.glsl", common_}, {"classic2d.comp", classicSrc_}});
            if (!classicDeep_.valid()) fprintf(stderr, "fract3d: deep zoom shader: %s\n", classicDeep_.error().c_str());
        }
        if (!classicDeep_.valid() || refLen_ < 2) return false;
        pp = &classicDeep_;
    } else {
        pp = classicProgram(classicUsesFp64(cs, out.h), custom);
    }
    if (!pp) return false;
    Program& p = *pp;
    if (!state.ensure(out.w, bandRowsFor(out.w))) return false;
    p.set("uBandOrigin", 0, y0);
    p.set("uBandSize", out.w, rows);
    p.set("uFirstPass", first ? 1 : 0);
    p.set("uChunk", std::max(chunk, 1));
    p.set("uResolution", (float)out.w, (float)out.h);
    p.setd("uCenter", cs.cx, cs.cy);
    p.setd("uPixelSize", cs.height / out.h);
    p.setd("uJuliaC", cs.jx, cs.jy);
    p.set("uJulia", (int)cs.julia);
    p.set("uFormula", cs.formula);
    p.set("uMaxIter", cs.maxIter);
    // Bands use the bailout as given (Fractint's |z| > 2); smooth coloring needs a big radius to look smooth.
    float bail = std::max(cs.bailout, 2.0f);
    if (!cs.banded) bail = std::max(bail, 64.0f);
    p.set("uBailout", bail);
    p.set("uBanded", (int)cs.banded);
    p.set("uPower", cs.power);
    p.set("uPhoenixP", cs.phoenixP[0], cs.phoenixP[1]);
    p.set("uColoring", cs.coloring);
    p.set("uTrapSize", cs.trapSize);
    p.set("uP1", cs.p1[0], cs.p1[1]);
    p.set("uP2", cs.p2[0], cs.p2[1]);
    p.set("uP3", cs.p3[0], cs.p3[1]);
    if (deep) {
        // pixel size as mantissa * 2^exponent: at 10^100x it's far below the smallest float
        double pixel = cs.height / out.h;
        int pe = 0;
        double pm = std::frexp(pixel, &pe);
        p.set("uPixelMant", (float)pm);
        p.set("uPixelExp", pe);
        p.set("uRefOffsetPx", (float)(deepOffset_[0] / pixel), (float)(deepOffset_[1] / pixel));
        p.set("uRefLen", refLen_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, refSsbo_);
    }
    for (int i = 0; i < 4; i++) glBindImageTexture(i, state.tex[i], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32UI);
    glBindImageTexture(4, out.value, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
    glBindImageTexture(5, out.aux, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);
    p.use();
    glDispatchCompute((out.w + 15) / 16, (rows + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    return true;
}

// ------------------------------------------------------------------ display
void Renderer::display(ViewMode mode, const RenderTarget* accum, const IndexTarget* index, const RenderSettings& rs,
                       const Classic2DSettings& cs, float cycleOffset, int outW, int outH, GLuint fbo) {
    Program& p = displayProg_;
    if (!p.valid()) return;
    bool m2d = mode == ViewMode::Classic2D;
    int ss = m2d && index && index->w > 0 ? std::max(index->w / std::max(outW, 1), 1) : 1;
    p.set("uMode", m2d ? 1 : 0);
    p.set("uAccum", 0);
    p.set("uIndex", 1);
    p.set("uPalette", 2);
    p.set("uRetroLut", 3);
    p.set("uAux", 4);
    p.set("uOutSize", (float)outW, (float)outH);
    p.set("uExposure", rs.exposure);
    p.set("uTonemap", rs.tonemap);
    p.set("uVignette", rs.vignette);
    p.set("uSaturation", rs.saturation);
    p.set("uSS", ss);
    p.set("uBanded", (int)cs.banded);
    p.set("uColoring", cs.coloring);
    p.set("uCycleOffset", cycleOffset);
    p.set("uColorDensity", cs.colorDensity);
    p.set("uInsideMode", cs.insideMode);
    p.set3("uInsideColor", cs.insideColor);
    p.set("uRootSpread", cs.rootSpread);
    p.set("uRetro", rs.retro);
    p.set("uPixelSize", rs.pixelSize);
    p.set("uScanlines", rs.scanlines);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, outW, outH);
    glBindTextureUnit(0, !m2d && accum ? accum->tex : 0);
    glBindTextureUnit(1, m2d && index ? index->value : 0);
    glBindTextureUnit(4, m2d && index ? index->aux : 0);
    glBindTextureUnit(2, paletteTex_);
    glBindTextureUnit(3, retroLut_[rs.retro == 2 ? 1 : 0]);
    glBindVertexArray(vao_);
    p.use();
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ------------------------------------------------------------------ probe
bool Renderer::probe(const Fractal& f, const RenderSettings& rs, const View3D& v, const Vec3& dir, float pixelAngle) {
    if (status(f) != ProgStatus::Ready) return false;
    FractalPrograms& fp = programs(f);
    int slot = probeIdx_;
    if (probeFence_[slot]) return false;  // both slots still in flight
    Program& p = fp.probe;
    p.set3("uCamPos", v.pos.data());
    p.set3("uProbeDir", dir.data());
    p.set("uPixelAngle", pixelAngle);
    p.set("uDetail", rs.detail);
    p.set("uStepFactor", rs.stepFactor);
    p.set("uMaxSteps", rs.maxSteps);
    p.set("uMaxT", rs.maxDist * v.sceneScale);
    p.set("uFloor", (int)rs.floorOn);
    p.set("uFloorY", rs.floorY);
    p.set("uP1", formulaP_[0], formulaP_[1]);
    p.set("uP2", formulaP_[2], formulaP_[3]);
    p.set("uP3", formulaP_[4], formulaP_[5]);
    setFractalParams(p, f, v.animTime);
    glBindFramebuffer(GL_FRAMEBUFFER, probeRT_.fbo);
    glViewport(0, 0, 2, 1);
    glBindVertexArray(vao_);
    p.use();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, probePbo_[slot]);
    glReadPixels(0, 0, 2, 1, GL_RG, GL_FLOAT, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    probeFence_[slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    probeIdx_ ^= 1;
    return true;
}

bool Renderer::fetchProbe(float& deAtCam, float& hitT) {
    // Slots alternate, so the one we'd write next is the oldest outstanding readback.
    for (int k = 0; k < 2; k++) {
        int slot = probeIdx_ ^ k;
        GLsync& fence = probeFence_[slot];
        if (!fence) continue;
        GLenum r = glClientWaitSync(fence, 0, 0);
        if (r != GL_ALREADY_SIGNALED && r != GL_CONDITION_SATISFIED) return false;
        glDeleteSync(fence);
        fence = nullptr;
        float v[4];
        glGetNamedBufferSubData(probePbo_[slot], 0, 16, v);
        deAtCam = v[0];
        hitT = v[2];
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ screenshots
bool Renderer::readImage(ViewMode mode, const RenderTarget* accum, const IndexTarget* index, const RenderSettings& rs,
                         const Classic2DSettings& cs, float cycleOffset, int w, int h, std::vector<uint8_t>& px) {
    if (!shotRT_.ensure(w, h, GL_RGBA8, GL_NEAREST)) return false;
    display(mode, accum, index, rs, cs, cycleOffset, w, h, shotRT_.fbo);
    px.resize((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, shotRT_.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    shotRT_.release();  // posters can be huge; don't keep the memory
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    return true;
}

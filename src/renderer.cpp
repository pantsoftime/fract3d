#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
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

    Palette vga = vgaDefaultPalette();
    glCreateTextures(GL_TEXTURE_2D, 1, &retroTex_);
    glTextureStorage2D(retroTex_, 1, GL_RGBA8, 256, 1);
    glTextureSubImage2D(retroTex_, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, vga.rgba.data());
    glTextureParameteri(retroTex_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(retroTex_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    probeRT_.ensure(2, 1, GL_RG32F, GL_NEAREST);
    glCreateBuffers(2, probePbo_);
    for (auto b : probePbo_) glNamedBufferStorage(b, 16, nullptr, GL_MAP_READ_BIT);

    if (!loadCore(err)) return false;
    coreTime_ = newestShaderTime();
    return true;
}

void Renderer::shutdown() {
    // GL objects must go while the context still exists (move-assign swaps, the temporary deletes)
    progs_.clear();
    displayProg_ = Program();
    classic32_ = Program();
    classic64_ = Program();
    accum.release();
    index2D.release();
    probeRT_.release();
    shotRT_.release();
    for (auto& f : probeFence_)
        if (f) glDeleteSync(f), f = nullptr;
    glDeleteBuffers(2, probePbo_);
    glDeleteTextures(1, &paletteTex_);
    glDeleteTextures(1, &retroTex_);
    glDeleteVertexArrays(1, &vao_);
}

fs::file_time_type Renderer::newestShaderTime() const {
    fs::file_time_type t{};
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
        !rd("probe.frag", probe) || !rd("classic2d.frag", classic) || !rd("display.frag", display))
        return false;

    Program disp, c32, c64;
    if (!disp.build(vert, {{"header", "#version 460\n"}, {"display.frag", display}})) {
        err += disp.error();
        return false;
    }
    if (!c32.build(vert, {{"header", "#version 460\n"}, {"common.glsl", common}, {"classic2d.frag", classic}})) {
        err += c32.error();
        return false;
    }
    if (!c64.build(vert, {{"header", "#version 460\n#define FP64\n"}, {"common.glsl", common}, {"classic2d.frag", classic}})) {
        err += c64.error();
        return false;
    }
    // swap in only after everything compiled, so a typo during live editing keeps the old shaders running
    std::swap(displayProg_, disp);
    std::swap(classic32_, c32);
    std::swap(classic64_, c64);
    vert_ = vert;
    common_ = common;
    raymarch_ = raymarch;
    probeSrc_ = probe;
    classic_ = classic;
    display_ = display;
    return true;
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

Renderer::FractalPrograms& Renderer::programs(const Fractal& f) {
    auto& slot = progs_[f.key];
    if (!slot) slot = std::make_unique<FractalPrograms>();
    FractalPrograms& fp = *slot;
    if (fp.attempted) return fp;
    fp.attempted = true;
    fp.error.clear();
    std::string fname = f.path.filename().string();
    std::vector<ShaderChunk> trace = {{"header", "#version 460\n"},
                                      {"common.glsl", common_},
                                      {"(parameters of " + fname + ")", f.uniformDecls()},
                                      {fname, f.code},
                                      {"raymarch.frag", raymarch_}};
    if (!fp.trace.build(vert_, trace)) {
        fp.error = fp.trace.error();
        return fp;
    }
    auto probe = trace;
    probe.back() = {"probe.frag", probeSrc_};
    if (!fp.probe.build(vert_, probe)) fp.error = fp.probe.error();
    return fp;
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
    FractalPrograms& fp = programs(f);
    if (!fp.trace.valid() || !fp.error.empty()) return false;
    Program& p = fp.trace;

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
    p.set("uShadows", rs.shadows);
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
    p.set("uFloor", rs.floorOn);
    p.set("uFloorY", rs.floorY);
    p.set3("uFloorColor", rs.floorColor);
    p.set("uColorScale", rs.colorScale);
    p.set("uColorOffset", rs.colorOffset);
    p.set("uColorMode", rs.colorMode);
    p.set("uPaletteMix", rs.paletteMix);
    p.set3("uBaseColor", rs.baseColor);
    p.set("uSpecular", rs.specular);
    p.set("uRoughness", rs.roughness);
    p.set("uPalette", 0);
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
    glBindVertexArray(vao_);
    p.use();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    return true;
}

// ------------------------------------------------------------------ 2D
bool Renderer::classicUsesFp64(const Classic2DSettings& cs, int outH) const {
    if (cs.fp64 == 0) return false;
    if (cs.fp64 == 1) return true;
    double pixel = cs.height / std::max(outH, 1);
    return pixel < 2e-6;  // float runs out of mantissa around here
}

bool Renderer::render2D(RenderTarget& target, const Classic2DSettings& cs, int outW, int outH, const int* scissor) {
    Program& p = classicUsesFp64(cs, outH) ? classic64_ : classic32_;
    if (!p.valid()) return false;
    int ss = std::clamp(cs.supersample, 1, 4);
    p.set("uResolution", (float)(outW * ss), (float)(outH * ss));
    p.setd("uCenter", cs.cx, cs.cy);
    p.setd("uPixelSize", cs.height / (outH * ss));
    p.setd("uJuliaC", cs.jx, cs.jy);
    p.set("uJulia", cs.julia);
    p.set("uFormula", cs.formula);
    p.set("uMaxIter", cs.maxIter);
    // Bands use the bailout as given (Fractint's |z| > 2); smooth coloring needs a big radius to look smooth.
    float bail = std::max(cs.bailout, 2.0f);
    if (!cs.banded) bail = std::max(bail, 64.0f);
    p.set("uBailout", bail);
    p.set("uBanded", cs.banded);
    p.set("uPower", cs.power);
    p.set("uPhoenixP", cs.phoenixP[0], cs.phoenixP[1]);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.w, target.h);
    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    }
    glBindVertexArray(vao_);
    p.use();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_SCISSOR_TEST);
    return true;
}

// ------------------------------------------------------------------ display
void Renderer::display(ViewMode mode, const RenderTarget& src, const RenderSettings& rs, const Classic2DSettings& cs,
                       float cycleOffset, int outW, int outH, GLuint fbo) {
    Program& p = displayProg_;
    if (!p.valid()) return;
    bool m2d = mode == ViewMode::Classic2D;
    p.set("uMode", m2d ? 1 : 0);
    p.set("uAccum", 0);
    p.set("uIndex", 1);
    p.set("uPalette", 2);
    p.set("uRetroPalette", 3);
    p.set("uOutSize", (float)outW, (float)outH);
    p.set("uExposure", rs.exposure);
    p.set("uTonemap", rs.tonemap);
    p.set("uVignette", rs.vignette);
    p.set("uSaturation", rs.saturation);
    p.set("uSS", std::clamp(cs.supersample, 1, 4));
    p.set("uBanded", cs.banded);
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
    glBindTextureUnit(0, m2d ? 0 : src.tex);
    glBindTextureUnit(1, m2d ? src.tex : 0);
    glBindTextureUnit(2, paletteTex_);
    glBindTextureUnit(3, retroTex_);
    glBindVertexArray(vao_);
    p.use();
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ------------------------------------------------------------------ probe
bool Renderer::probe(const Fractal& f, const RenderSettings& rs, const View3D& v, const Vec3& dir, float pixelAngle) {
    FractalPrograms& fp = programs(f);
    if (!fp.probe.valid() || !fp.error.empty()) return false;
    int slot = probeIdx_;
    if (probeFence_[slot]) return false;  // both slots still in flight  // previous readback in this slot not consumed yet
    Program& p = fp.probe;
    p.set3("uCamPos", v.pos.data());
    p.set3("uProbeDir", dir.data());
    p.set("uPixelAngle", pixelAngle);
    p.set("uDetail", rs.detail);
    p.set("uStepFactor", rs.stepFactor);
    p.set("uMaxSteps", rs.maxSteps);
    p.set("uMaxT", rs.maxDist * v.sceneScale);
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
bool Renderer::writePng(const std::string& path, ViewMode mode, const RenderTarget& src, const RenderSettings& rs,
                        const Classic2DSettings& cs, float cycleOffset, int w, int h) {
    shotRT_.ensure(w, h, GL_RGBA8, GL_NEAREST);
    display(mode, src, rs, cs, cycleOffset, w, h, shotRT_.fbo);
    std::vector<uint8_t> px((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, shotRT_.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    shotRT_.release();  // posters can be huge; don't keep the memory
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    stbi_flip_vertically_on_write(1);
    return stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4) != 0;
}

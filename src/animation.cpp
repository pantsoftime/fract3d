// animation.cpp — camera paths (keyframed views), playback and video export.
//
// A keyframe is a complete view as PAR text. Between keyframes every numeric
// setting is interpolated with Catmull-Rom splines (via the settings table), the
// camera turns the shortest way round, 2D zooms move at a constant rate while
// keeping the target in place, and switches (fractal, mode, toggles) change at
// the keyframe. Export renders each frame at full quality and pipes it to ffmpeg.
#include "app.h"
#include "pngmeta.h"
#include "settings.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace fs = std::filesystem;

// ------------------------------------------------------------------ keyframes
void App::addKeyframe() {
    Keyframe k;
    k.par = parText();
    k.label = historyLabel();
    camPath.keys.push_back(k);
    camPath.parsed = false;
    toast("Keyframe " + std::to_string(camPath.keys.size()) + " added (Animate window)", 2);
}

// Turns each keyframe's PAR text into settings structs, by loading it into the
// live state and reading it back; the current view is restored afterwards.
void App::parseKeyframes() {
    if (camPath.parsed) return;
    std::string current = parText();
    // loading keyframes resets their fractals' parameters: keep every fractal's as they are
    std::vector<std::vector<Param>> keepParams;
    for (auto& f : lib_.all()) keepParams.push_back(f.params);
    for (auto& k : camPath.keys) {
        loadParText(k.par, "keyframe", true);
        syncCenter();  // every keyframe gets its exact 2D center (deep ones from classic.centerHP)
        k.v = view;
        k.fractalKey = fractal().key;
        k.params.clear();
        for (auto& p : fractal().params) k.params.push_back({p.value[0], p.value[1], p.value[2], p.value[3]});
    }
    for (size_t i = 0; i < keepParams.size() && i < lib_.all().size(); i++)
        if (lib_.all()[i].params.size() == keepParams[i].size()) lib_.all()[i].params = keepParams[i];
    loadParText(current, "view", true);
    camPath.parsed = true;
}

float App::pathDuration() const {
    float d = 0;
    for (size_t i = 0; i + 1 < camPath.keys.size(); i++) d += std::max(camPath.keys[i].duration, 0.01f);
    return d;
}

static float catmull(float p0, float p1, float p2, float p3, float u) {
    float u2 = u * u, u3 = u2 * u;
    return 0.5f * (2 * p1 + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u2 + (-p0 + 3 * p1 - 3 * p2 + p3) * u3);
}
static double catmull(double p0, double p1, double p2, double p3, double u) {
    double u2 = u * u, u3 = u2 * u;
    return 0.5 * (2 * p1 + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u2 + (-p0 + 3 * p1 - 3 * p2 + p3) * u3);
}

// Every float/double field of a settings struct, spline-interpolated through
// four keyframes (fields a..d at the same offset), and ints that are quantities
// (kCount: iteration limits, step counts; kLogScale ones in log space), rounded.
// Choices and toggles keep b's value.
template <class S, class Visit>
static void splineFields(S& out, const S& a, const S& b, const S& c, const S& d, float u, Visit visit) {
    out = b;
    visit(out, [&](const char*, const char*, auto* ptr, int n, unsigned flags) {
        using T = std::remove_reference_t<decltype(*ptr)>;
        size_t off = reinterpret_cast<const char*>(ptr) - reinterpret_cast<const char*>(&out);
        auto at = [&](const S& s, int i) { return reinterpret_cast<const T*>(reinterpret_cast<const char*>(&s) + off)[i]; };
        if constexpr (std::is_floating_point_v<T>) {
            for (int i = 0; i < n; i++) ptr[i] = catmull(at(a, i), at(b, i), at(c, i), at(d, i), (T)u);
        } else if constexpr (std::is_same_v<T, int>) {
            if (!(flags & kCount)) return;
            bool lg = flags & kLogScale;
            auto f = [&](const S& s, int i) { return lg ? std::log(std::max((double)at(s, i), 1.0)) : (double)at(s, i); };
            for (int i = 0; i < n; i++) {
                double v = catmull(f(a, i), f(b, i), f(c, i), f(d, i), (double)u);
                double lo = std::min(at(b, i), at(c, i)), hi = std::max(at(b, i), at(c, i));  // no overshoot
                ptr[i] = (int)std::lround(std::clamp(lg ? std::exp(v) : v, lo, hi));
            }
        }
    });
}

void App::applyPathTime(float t) {
    parseKeyframes();
    auto& keys = camPath.keys;
    if (keys.empty()) return;
    disengageAutopilot("Autopilot off - you have the controls");  // scrubbing or playing a path is flying it yourself
    int n = (int)keys.size(), seg = 0;
    float u = 0;
    if (n > 1) {
        t = std::clamp(t, 0.0f, pathDuration());
        while (seg < n - 2 && t > keys[seg].duration) t -= keys[seg].duration, seg++;
        u = std::clamp(t / std::max(keys[seg].duration, 0.01f), 0.0f, 1.0f);
        if (u >= 1.0f) seg = n - 1, u = 0.0f;  // the very end: exactly the last keyframe, switches included
        else if (keys[seg].ease) u = u * u * (3.0f - 2.0f * u);  // slow in and out of this segment
    }
    const Keyframe& B = keys[seg];
    const Keyframe& C = keys[std::min(seg + 1, n - 1)];
    const Keyframe& A = keys[std::max(seg - 1, 0)];
    const Keyframe& D = keys[std::min(seg + 2, n - 1)];
    bool sameScene = B.fractalKey == C.fractalKey && B.v.mode == C.v.mode;
    // neighbours showing a different scene don't bend this segment's spline
    const Keyframe& A2 = A.fractalKey == B.fractalKey && A.v.mode == B.v.mode ? A : B;
    const Keyframe& D2 = D.fractalKey == C.fractalKey && D.v.mode == C.v.mode ? D : C;

    ViewState v = B.v;
    if (sameScene) {
        splineFields(v.rs, A2.v.rs, B.v.rs, C.v.rs, D2.v.rs, u, [](RenderSettings& x, auto&& f) { visitRender(x, f); });
        splineFields(v.cs, A2.v.cs, B.v.cs, C.v.cs, D2.v.cs, u, [](Classic2DSettings& x, auto&& f) { visitClassic(x, f); });
        // 2D zooms: constant zoom rate (log height), and the destination stays put on screen
        double hb = B.v.cs.height, hc = C.v.cs.height;
        double h = u <= 0.0f ? hb : std::exp(std::log(hb) + (std::log(hc) - std::log(hb)) * u);  // (exact at keyframes)
        double w = std::abs(hb - hc) > 1e-12 * hb ? (hb - h) / (hb - hc) : u;
        v.cs.height = h;
        v.cs.cx = B.v.cs.cx + (C.v.cs.cx - B.v.cs.cx) * w;
        v.cs.cy = B.v.cs.cy + (C.v.cs.cy - B.v.cs.cy) * w;
        if (std::min(hb, hc) / 1000 < 1e-13 && !B.v.hpRe.empty() && !C.v.hpRe.empty()) {
            // deep zoom path: interpolate the exact centers
            int bits = hp::bitsForPixel(std::min(h, std::min(hb, hc)) / 1000);
            v.hpRe = hp::lerp(B.v.hpRe, C.v.hpRe, w, bits);
            v.hpIm = hp::lerp(B.v.hpIm, C.v.hpIm, w, bits);
            v.cs.cx = v.hpShadow[0] = hp::toDouble(v.hpRe);
            v.cs.cy = v.hpShadow[1] = hp::toDouble(v.hpIm);
        }
        // camera: splined position, angles unwrapped around B's (shortest turn), distance in log space
        const Camera *ca = &A2.v.cam, *cb = &B.v.cam, *cc = &C.v.cam, *cd = &D2.v.cam;
        v.cam.pos = Vec3(catmull(ca->pos.x, cb->pos.x, cc->pos.x, cd->pos.x, u), catmull(ca->pos.y, cb->pos.y, cc->pos.y, cd->pos.y, u),
                         catmull(ca->pos.z, cb->pos.z, cc->pos.z, cd->pos.z, u));
        auto unwrap = [](float ref, float a, float turn = 6.2831853f) { return ref + std::remainder(a - ref, turn); };
        float yc = unwrap(cb->yaw, cc->yaw);
        v.cam.yaw = catmull(unwrap(cb->yaw, ca->yaw), cb->yaw, yc, unwrap(yc, cd->yaw), u);
        // the sun turns the short way round too (350 -> 10 degrees is 20 degrees, not 340)
        float sb = B.v.rs.sunAzimuth, sc = unwrap(sb, C.v.rs.sunAzimuth, 360.0f);
        float sun = catmull(unwrap(sb, A2.v.rs.sunAzimuth, 360.0f), sb, sc, unwrap(sc, D2.v.rs.sunAzimuth, 360.0f), u);
        v.rs.sunAzimuth = std::fmod(std::fmod(sun, 360.0f) + 360.0f, 360.0f);
        v.cam.pitch = std::clamp(catmull(ca->pitch, cb->pitch, cc->pitch, cd->pitch, u), -1.55f, 1.55f);
        v.cam.distance = std::exp(catmull(std::log(ca->distance), std::log(cb->distance), std::log(cc->distance), std::log(cd->distance), u));
        for (int k = 0; k < 3; k++) {
            v.cosine.a[k] = catmull(A2.v.cosine.a[k], B.v.cosine.a[k], C.v.cosine.a[k], D2.v.cosine.a[k], u);
            v.cosine.b[k] = catmull(A2.v.cosine.b[k], B.v.cosine.b[k], C.v.cosine.b[k], D2.v.cosine.b[k], u);
            v.cosine.c[k] = catmull(A2.v.cosine.c[k], B.v.cosine.c[k], C.v.cosine.c[k], D2.v.cosine.c[k], u);
            v.cosine.d[k] = catmull(A2.v.cosine.d[k], B.v.cosine.d[k], C.v.cosine.d[k], D2.v.cosine.d[k], u);
        }
    }
    // apply: fractal, formula (changes at keyframes), palette, parameters
    bool formulaChanged = v.formulaSource != view.formulaSource || std::memcmp(v.fn, view.fn, sizeof v.fn) != 0;
    int fractalIdx = lib_.indexOf(B.fractalKey);
    view = v;
    if (fractalIdx >= 0) view.fractal = fractalIdx;
    if (formulaChanged && !view.formulaSource.empty()) compileFormula();
    applyPalette();
    auto& params = fractal().params;
    // the path drives the parameters now; their own animation comes back afterwards
    auto saved = std::find_if(camPath.savedAnim.begin(), camPath.savedAnim.end(),
                              [&](auto& e) { return e.first == fractal().key; });
    if (saved == camPath.savedAnim.end()) {
        std::vector<char> flags;
        for (auto& p : params) flags.push_back(p.animate);
        camPath.savedAnim.push_back({fractal().key, flags});
    }
    for (size_t i = 0; i < params.size() && i < B.params.size(); i++) {
        auto& p = params[i];
        bool smooth = sameScene && i < C.params.size() && p.type != ParamType::Int && p.type != ParamType::Bool &&
                      p.type != ParamType::Choice;
        for (int k = 0; k < 4; k++) {
            if (!smooth) {
                p.value[k] = B.params[i][k];
                continue;
            }
            float pa = i < A2.params.size() ? A2.params[i][k] : B.params[i][k];
            float pd = i < D2.params.size() ? D2.params[i][k] : C.params[i][k];
            p.value[k] = std::clamp(catmull(pa, B.params[i][k], C.params[i][k], pd, u), p.minV, p.maxV);
        }
        p.animate = false;
    }
}

void App::endPathPreview() {
    for (auto& [key, flags] : camPath.savedAnim) {
        int idx = lib_.indexOf(key);
        if (idx < 0) continue;
        auto& params = lib_.all()[idx].params;
        for (size_t i = 0; i < params.size() && i < flags.size(); i++) params[i].animate = flags[i];
    }
    camPath.savedAnim.clear();
}

void App::updatePathPlayback() {
    if (!camPath.playing) return;
    camPath.time = (float)(now - camPath.playStart);
    float dur = pathDuration();
    if (camPath.time > dur) {
        if (camPath.loop && dur > 0) {
            camPath.playStart = now;
            camPath.time = 0;
        } else {
            camPath.playing = false;
            camPath.time = dur;
            applyPathTime(camPath.time);
            endPathPreview();
            return;
        }
    }
    applyPathTime(camPath.time);
}

// ------------------------------------------------------------------ path files
// Text: "---- keyframe <seconds>" lines, each followed by that keyframe's PAR text.
bool App::savePath(const fs::path& p) {
    std::ofstream o(p);
    if (!o) return false;
    o << "; Fract3D camera path: keyframes (views) and the seconds to the next one\n";
    for (auto& k : camPath.keys)
        o << "---- keyframe " << k.duration << (k.ease ? " ease" : "") << " " << k.label << "\n" << k.par;
    toast("Saved " + p.string());
    return true;
}

bool App::loadPath(const fs::path& p) {
    bool ok = false;
    std::istringstream is(readTextFile(p.string(), &ok));
    if (!ok) return false;
    std::vector<Keyframe> keys;
    std::string line;
    while (std::getline(is, line)) {
        if (line.rfind("---- keyframe", 0) == 0) {
            Keyframe k;
            std::istringstream ls(line.substr(13));
            ls >> k.duration;
            if (!std::isfinite(k.duration) || k.duration <= 0) k.duration = 3.0f;
            ls >> std::ws;
            if (ls.peek() == 'e') {  // optional "ease" flag before the label
                std::streampos at = ls.tellg();
                std::string word;
                ls >> word;
                if (word == "ease") k.ease = true;
                else ls.seekg(at);
            }
            std::getline(ls >> std::ws, k.label);
            keys.push_back(k);
        } else if (!keys.empty()) {
            keys.back().par += line + "\n";
        }
    }
    if (keys.empty()) return false;
    camPath.keys = keys;
    camPath.parsed = false;
    toast("Loaded camera path with " + std::to_string(keys.size()) + " keyframes");
    return true;
}

// ------------------------------------------------------------------ video export
// "frame-%05d.png": one %d (optionally %0Nd) makes an image sequence. Formatted by
// hand, so a stray % in a file name can never reach printf.
static bool sequenceName(const std::string& pattern, int frame, std::string* out) {
    size_t at = std::string::npos, len = 0;
    int width = 0;
    for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] != '%') continue;
        size_t j = i + 1;
        int w = 0;
        while (j < pattern.size() && isdigit((unsigned char)pattern[j])) w = w * 10 + (pattern[j++] - '0');
        if (j >= pattern.size() || pattern[j] != 'd' || at != std::string::npos || w > 12) return false;
        at = i, len = j + 1 - i, width = w;
        i = j;
    }
    if (at == std::string::npos) return false;
    if (out) {
        std::string n = std::to_string(frame);
        if ((int)n.size() < width) n.insert(0, width - n.size(), '0');
        *out = pattern.substr(0, at) + n + pattern.substr(at + len);
    }
    return true;
}
bool isImageSequence(const std::string& path) {
    return path.size() > 4 && path.compare(path.size() - 4, 4, ".png") == 0 && sequenceName(path, 0, nullptr);
}

// Frames are rendered with the high-res renderer and piped (raw RGBA) to an
// ffmpeg child process started without a shell.
bool App::startVideo(const std::string& out) {
    parseKeyframes();
    if (camPath.keys.size() < 2) {
        toast("A camera path needs at least two keyframes (press K to add the current view)", 4);
        return false;
    }
    if (isImageSequence(out)) {  // lossless PNG frames, each carrying its view; no ffmpeg needed
        std::error_code ec;
        fs::create_directories(fs::path(out).parent_path(), ec);
        if (video.finishing) pollVideoEncoder(true);
        video = VideoJob();
        video.sequence = true;
        video.active = true;
        video.out = out;
        video.frames = std::max(2, (int)std::ceil(pathDuration() * camPath.fps) + 1);
        video.restorePar = parText();
        video.started = glfwGetTime();
        camPath.playing = false;
        nextVideoFrame();
        return true;
    }
    int fds[2];
    if (pipe(fds) != 0) return false;
    char size[32], fps[16];
    snprintf(size, sizeof size, "%dx%d", camPath.videoW, camPath.videoH);
    snprintf(fps, sizeof fps, "%g", camPath.fps);
    std::vector<std::string> args = {"ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba",
                                     "-s", size, "-r", fps, "-i", "-", "-vf", "vflip"};
    if (camPath.encoder == 1) args.insert(args.end(), {"-c:v", "h264_nvenc", "-preset", "p6", "-cq", "18"});
    else args.insert(args.end(), {"-c:v", "libx264", "-preset", "slow", "-crf", "16"});
    args.insert(args.end(), {"-pix_fmt", "yuv420p", out});
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[0], 0);
    posix_spawn_file_actions_addclose(&fa, fds[1]);
    pid_t pid;
    int rc = posix_spawnp(&pid, "ffmpeg", &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[0]);
    if (rc != 0) {
        close(fds[1]);
        toast("Video export needs ffmpeg (not found on PATH)", 5);
        return false;
    }
    if (video.finishing) pollVideoEncoder(true);  // the previous export's encoder is still flushing
    video = VideoJob();
    video.oldSigpipe = signal(SIGPIPE, SIG_IGN);  // if ffmpeg dies, write() fails instead of killing us
    video.active = true;
    video.fd = fds[1];
    video.pid = pid;
    video.out = out;
    video.frames = std::max(2, (int)std::ceil(pathDuration() * camPath.fps) + 1);
    video.restorePar = parText();
    video.started = glfwGetTime();
    camPath.playing = false;
    nextVideoFrame();
    return true;
}

void App::nextVideoFrame() {
    applyPathTime(video.frame / camPath.fps);
    startPoster(camPath.videoW, camPath.videoH, camPath.videoSamples);
    poster.toVideo = true;
}

void App::videoFrameRendered(const std::vector<uint8_t>& rgba) {
    if (video.sequence) {
        std::string name;
        sequenceName(video.out, video.frame, &name);
        if (!writePngWithText(name, camPath.videoW, camPath.videoH, rgba.data(), poster.par)) {
            finishVideo(false);
            return;
        }
        if (++video.frame >= video.frames) finishVideo(true);
        else if (video.frame == session.cli.cancelAfterFrames) cancelVideo();
        else nextVideoFrame();
        return;
    }
    size_t left = rgba.size();
    const uint8_t* p = rgba.data();
    while (left > 0) {
        ssize_t n = write(video.fd, p, left);
        if (n <= 0) {
            finishVideo(false);
            return;
        }
        p += n;
        left -= (size_t)n;
    }
    if (++video.frame >= video.frames) finishVideo(true);
    else if (video.frame == session.cli.cancelAfterFrames) cancelVideo();
    else nextVideoFrame();
}

// All frames are written (ok), writing failed, or the user cancelled. The view comes
// back right away; ffmpeg may still need a while to flush a long video, so its exit
// is picked up by pollVideoEncoder from the frame loop instead of blocking the UI.
void App::finishVideo(bool ok) {
    if (!video.active) return;
    video.active = false;
    if (!video.sequence) {
        close(video.fd);
        video.fd = -1;
        signal(SIGPIPE, video.oldSigpipe ? video.oldSigpipe : SIG_DFL);
        if (!ok) kill(video.pid, SIGTERM);  // no point letting it encode a broken or abandoned file
    }
    video.ok = ok;
    video.finishing = true;
    endPathPreview();
    loadParText(video.restorePar, "view", true);
    if (ok && !video.sequence) toast("Finishing " + video.out + " ...", 60);
    pollVideoEncoder();
}

void App::pollVideoEncoder(bool wait) {
    if (!video.finishing) return;
    bool ok = video.ok;
    if (!video.sequence) {
        int status = 0;
        pid_t r = waitpid(video.pid, &status, wait ? 0 : WNOHANG);
        if (r == 0) return;  // still encoding
        ok = ok && r == video.pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    video.finishing = false;
    char buf[512];
    if (video.cancelled)
        snprintf(buf, sizeof buf, "Video export cancelled after %d frames", video.frame);
    else
        snprintf(buf, sizeof buf, "%s %s (%d frames, %.0fs)", ok ? "Saved" : "Video export FAILED:", video.out.c_str(), video.frame,
                 glfwGetTime() - video.started);
    toast(buf, 8);
    if (!ok && !video.cancelled) exitCode = 1;
    if (!session.cli.shotPath.empty()) quit = true;
}

void App::cancelVideo() {
    if (!video.active) return;
    poster.active = false;
    poster.target.release();
    poster.index.release();
    video.cancelled = true;
    finishVideo(false);
}

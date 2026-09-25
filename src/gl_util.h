#pragma once
#include <epoxy/gl.h>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

std::string readTextFile(const std::string& path, bool* ok = nullptr);

// Routes GL_KHR_debug messages to stderr (errors and performance warnings;
// everything with `verbose`). Enabled by --gl-debug, and in debug builds.
void installGlDebugOutput(bool verbose);

// Largest texture edge the driver accepts.
int maxTextureSize();

// A compiled + linked GLSL program with a uniform-location cache.
// Sources are given as named chunks so compile errors can name the file they came from.
struct ShaderChunk {
    std::string name;
    std::string text;
};

enum class BuildState { Empty, Pending, Ready, Failed };

class Program {
public:
    Program() = default;
    ~Program();
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    Program(Program&& o) noexcept { swap(o); }
    Program& operator=(Program&& o) noexcept {
        swap(o);
        return *this;
    }
    void swap(Program& o) noexcept;

    // Synchronous builds. Return false and fill error() on failure; the previous
    // program, if any, keeps working.
    bool build(const std::string& vertSrc, const std::vector<ShaderChunk>& fragChunks);
    bool buildCompute(const std::vector<ShaderChunk>& chunks);

    // Asynchronous builds: with GL_KHR_parallel_shader_compile the driver compiles on
    // its own threads and poll() reports when the result is ready, so switching to a
    // new fractal never hitches the frame. Without the extension poll() just blocks.
    void buildAsync(const std::string& vertSrc, const std::vector<ShaderChunk>& fragChunks);
    BuildState poll();

    bool valid() const { return id_ != 0; }
    GLuint id() const { return id_; }
    const std::string& error() const { return error_; }
    void use() const { glUseProgram(id_); }

    GLint loc(const char* name);
    void set(const char* n, int v) { glProgramUniform1i(id_, loc(n), v); }
    void set(const char* n, float v) { glProgramUniform1f(id_, loc(n), v); }
    void set(const char* n, float a, float b) { glProgramUniform2f(id_, loc(n), a, b); }
    void set(const char* n, float a, float b, float c) { glProgramUniform3f(id_, loc(n), a, b, c); }
    void set(const char* n, float a, float b, float c, float d) { glProgramUniform4f(id_, loc(n), a, b, c, d); }
    void set(const char* n, int a, int b) { glProgramUniform2i(id_, loc(n), a, b); }
    void set3(const char* n, const float* v) { glProgramUniform3fv(id_, loc(n), 1, v); }
    void setd(const char* n, double v) { glProgramUniform1d(id_, loc(n), v); }
    void setd(const char* n, double a, double b) { glProgramUniform2d(id_, loc(n), a, b); }

private:
    GLuint link(GLuint a, GLuint b, bool wait);
    bool finish(GLuint p);

    struct SvHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    GLuint id_ = 0;
    GLuint pending_ = 0;
    std::vector<ShaderChunk> pendingChunks_;  // for error messages of an async build
    BuildState state_ = BuildState::Empty;
    std::string error_;
    std::unordered_map<std::string, GLint, SvHash, std::equal_to<>> locs_;
};

// Render target: one color texture + framebuffer.
struct RenderTarget {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    GLenum format = 0;
    // Returns false (and leaves the target empty) if the size is unsupported or
    // the framebuffer is incomplete, e.g. out of video memory.
    RenderTarget() = default;
    RenderTarget(const RenderTarget&) = delete;  // owns GL names: copies would delete them twice
    RenderTarget& operator=(const RenderTarget&) = delete;
    bool ensure(int width, int height, GLenum internalFormat, GLenum filter = GL_LINEAR);
    void release();
    void swap(RenderTarget& o) {
        std::swap(fbo, o.fbo);
        std::swap(tex, o.tex);
        std::swap(w, o.w);
        std::swap(h, o.h);
        std::swap(format, o.format);
    }
    ~RenderTarget() { release(); }
};

// 2D iteration buffer, written by the classic2d compute shader:
//   value: R32F iteration count (-1 inside, -2 not computed yet)
//   aux:   R8 extra coloring value (root index, zmag, angle, stripe, trap...)
struct IndexTarget {
    GLuint value = 0, aux = 0;
    int w = 0, h = 0;
    IndexTarget() = default;
    IndexTarget(const IndexTarget&) = delete;
    IndexTarget& operator=(const IndexTarget&) = delete;
    bool ensure(int width, int height);
    void clear();  // everything "not computed yet"
    void release();
    void swap(IndexTarget& o) {
        std::swap(value, o.value);
        std::swap(aux, o.aux);
        std::swap(w, o.w);
        std::swap(h, o.h);
    }
    ~IndexTarget() { release(); }
};

// Per-pixel orbit state for one band of a resumable 2D render (4 x RGBA32UI).
struct StateImages {
    GLuint tex[4] = {0, 0, 0, 0};
    int w = 0, h = 0;
    StateImages() = default;
    StateImages(const StateImages&) = delete;
    StateImages& operator=(const StateImages&) = delete;
    bool ensure(int width, int height);
    void release();
    ~StateImages() { release(); }
};

// Times a stream of GPU passes without waiting for them: each pass gets a timer
// query, and results are collected a frame or two later to keep a running
// estimate of the cost per unit of work. Callers plan each frame's work from the
// estimate, so the CPU never has to stall until the GPU is done.
struct PassTimer {
    static constexpr int kSlots = 64;  // passes in flight at most
    GLuint q[kSlots] = {};
    float work[kSlots] = {};
    int head = 0, tail = 0;  // in flight: [tail, head)
    double msPerWork = 0.002;  // running estimate
    PassTimer() = default;
    PassTimer(const PassTimer&) = delete;
    PassTimer& operator=(const PassTimer&) = delete;
    bool full() const { return (head + 1) % kSlots == tail; }
    void begin(float amountOfWork);
    void end();
    void poll();  // folds finished passes into the estimate
    void release();
};

struct GpuTimer {
    GLuint q[4] = {0, 0, 0, 0};
    int idx = 0;
    bool pending[4] = {false, false, false, false};
    double lastMs = 0.0;
    float tag[4][2] = {};    // caller data stored with each query (e.g. scale, sample count)
    float lastTag[2] = {0, 0};
    bool fresh = false;      // a new result arrived during the last poll()
    void begin(float tagA = 0, float tagB = 0);
    void end();
    void poll();  // updates lastMs from the oldest finished query
};

#pragma once
#include <epoxy/gl.h>

#include <string>
#include <unordered_map>
#include <vector>

std::string readTextFile(const std::string& path, bool* ok = nullptr);

// A compiled + linked GLSL program with a uniform-location cache.
// Sources are given as named chunks so compile errors can name the file they came from.
struct ShaderChunk {
    std::string name;
    std::string text;
};

class Program {
public:
    Program() = default;
    ~Program();
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    Program(Program&& o) noexcept : id_(o.id_), error_(std::move(o.error_)), locs_(std::move(o.locs_)) { o.id_ = 0; }
    Program& operator=(Program&& o) noexcept {
        std::swap(id_, o.id_);
        std::swap(error_, o.error_);
        std::swap(locs_, o.locs_);
        return *this;
    }

    // Builds from a vertex source and a list of fragment chunks. Returns false
    // and fills error() on failure (the previous program, if any, is kept).
    bool build(const std::string& vertSrc, const std::vector<ShaderChunk>& fragChunks);
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
    void set3(const char* n, const float* v) { glProgramUniform3fv(id_, loc(n), 1, v); }
    void setd(const char* n, double v) { glProgramUniform1d(id_, loc(n), v); }
    void setd(const char* n, double a, double b) { glProgramUniform2d(id_, loc(n), a, b); }

private:
    GLuint id_ = 0;
    std::string error_;
    std::unordered_map<std::string, GLint> locs_;
};

// Render target: one color texture + framebuffer.
struct RenderTarget {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    GLenum format = 0;
    void ensure(int width, int height, GLenum internalFormat, GLenum filter = GL_LINEAR);
    void release();
    ~RenderTarget() { release(); }
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

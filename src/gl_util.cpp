#include "gl_util.h"

#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>

std::string readTextFile(const std::string& path, bool* ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (ok) *ok = false;
        return {};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (ok) *ok = true;
    return ss.str();
}

// ------------------------------------------------------------------ debug output
static void GLAPIENTRY debugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* msg,
                                     const void* user) {
    bool verbose = user != nullptr;
    if (!verbose && severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    if (!verbose && type != GL_DEBUG_TYPE_ERROR && type != GL_DEBUG_TYPE_PERFORMANCE &&
        type != GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR)
        return;
    const char* t = type == GL_DEBUG_TYPE_ERROR ? "ERROR" : type == GL_DEBUG_TYPE_PERFORMANCE ? "perf" : "info";
    fprintf(stderr, "fract3d: GL %s [%u]: %s\n", t, id, msg);
}

void installGlDebugOutput(bool verbose) {
    if (!epoxy_has_gl_extension("GL_KHR_debug") && epoxy_gl_version() < 43) return;
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(debugCallback, verbose ? (void*)1 : nullptr);
}

int maxTextureSize() {
    static GLint m = 0;
    if (!m) glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m);
    return m;
}

static bool hasParallelCompile() {
    static int has = -1;
    if (has < 0) {
        has = epoxy_has_gl_extension("GL_KHR_parallel_shader_compile") ? 1 : 0;
        if (has) glMaxShaderCompilerThreadsKHR(0xFFFFFFFFu);  // let the driver pick
    }
    return has == 1;
}

// ------------------------------------------------------------------ programs
Program::~Program() {
    if (id_) glDeleteProgram(id_);
    if (pending_) glDeleteProgram(pending_);
}

void Program::swap(Program& o) noexcept {
    std::swap(id_, o.id_);
    std::swap(pending_, o.pending_);
    std::swap(pendingChunks_, o.pendingChunks_);
    std::swap(state_, o.state_);
    std::swap(error_, o.error_);
    std::swap(locs_, o.locs_);
}

// NVIDIA logs look like "2(57) : error C1008: ..." where 2 is the #line source
// string number. Replace the number with the chunk's file name.
static std::string prettifyLog(const std::string& log, const std::vector<ShaderChunk>& chunks) {
    std::regex re(R"((^|\n)(\d+)\((\d+)\))");
    std::string out;
    auto begin = std::sregex_iterator(log.begin(), log.end(), re);
    size_t last = 0;
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        out += log.substr(last, m.position() - last);
        int idx = std::stoi(m[2].str());
        std::string name = idx >= 0 && idx < (int)chunks.size() ? chunks[idx].name : m[2].str();
        out += m[1].str() + name + ":" + m[3].str();
        last = m.position() + m.length();
    }
    out += log.substr(last);
    return out;
}

static std::string shaderLog(GLuint s) {
    GLint len = 0;
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
    std::string log(len > 0 ? len : 1, '\0');
    glGetShaderInfoLog(s, len, nullptr, log.data());
    return log;
}

static std::string programLog(GLuint p) {
    GLint len = 0;
    glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
    std::string log(len > 0 ? len : 1, '\0');
    glGetProgramInfoLog(p, len, nullptr, log.data());
    return log;
}

// Compiles without checking the result (so drivers can compile in parallel).
static GLuint compileChunks(GLenum stage, const std::vector<ShaderChunk>& chunks) {
    // Prefix every chunk with a #line directive so errors map back to files.
    std::vector<std::string> texts;
    texts.reserve(chunks.size());
    for (size_t i = 0; i < chunks.size(); i++) {
        if (i == 0) texts.push_back(chunks[i].text);  // chunk 0 holds #version, must come first
        else texts.push_back("#line 1 " + std::to_string(i) + "\n" + chunks[i].text + "\n");
    }
    std::vector<const char*> ptrs;
    for (auto& t : texts) ptrs.push_back(t.c_str());
    GLuint s = glCreateShader(stage);
    glShaderSource(s, (GLsizei)ptrs.size(), ptrs.data(), nullptr);
    glCompileShader(s);
    return s;
}

GLuint Program::link(GLuint a, GLuint b, bool wait) {
    GLuint p = glCreateProgram();
    glAttachShader(p, a);
    if (b) glAttachShader(p, b);
    glLinkProgram(p);
    // Shaders can be flagged for deletion right away; the program keeps them alive.
    glDeleteShader(a);
    if (b) glDeleteShader(b);
    (void)wait;
    return p;
}

// Checks a linked program; on success it replaces the current one.
bool Program::finish(GLuint p) {
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        // Prefer the compile log of whichever shader failed.
        GLuint shaders[2];
        GLsizei n = 0;
        glGetAttachedShaders(p, 2, &n, shaders);
        std::string log;
        for (GLsizei i = 0; i < n; i++) {
            GLint c = 0;
            glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &c);
            if (!c) log += shaderLog(shaders[i]);
        }
        if (log.empty()) log = "link:\n" + programLog(p);
        error_ = prettifyLog(log, pendingChunks_);
        glDeleteProgram(p);
        state_ = BuildState::Failed;
        return false;
    }
    if (id_) glDeleteProgram(id_);
    id_ = p;
    locs_.clear();
    error_.clear();
    state_ = BuildState::Ready;
    return true;
}

void Program::buildAsync(const std::string& vertSrc, const std::vector<ShaderChunk>& fragChunks) {
    if (pending_) glDeleteProgram(pending_);
    pendingChunks_ = fragChunks;
    GLuint v = compileChunks(GL_VERTEX_SHADER, {{"header", vertSrc}});
    GLuint f = compileChunks(GL_FRAGMENT_SHADER, fragChunks);
    pending_ = link(v, f, false);
    state_ = BuildState::Pending;
}

BuildState Program::poll() {
    if (state_ != BuildState::Pending) return state_;
    if (hasParallelCompile()) {
        GLint done = 0;
        glGetProgramiv(pending_, GL_COMPLETION_STATUS_KHR, &done);
        if (!done) return BuildState::Pending;
    }
    GLuint p = pending_;
    pending_ = 0;
    finish(p);
    return state_;
}

bool Program::build(const std::string& vertSrc, const std::vector<ShaderChunk>& fragChunks) {
    buildAsync(vertSrc, fragChunks);
    GLuint p = pending_;
    pending_ = 0;
    return finish(p);
}

bool Program::buildCompute(const std::vector<ShaderChunk>& chunks) {
    if (pending_) glDeleteProgram(pending_), pending_ = 0;
    pendingChunks_ = chunks;
    GLuint c = compileChunks(GL_COMPUTE_SHADER, chunks);
    return finish(link(c, 0, true));
}

GLint Program::loc(const char* name) {
    auto it = locs_.find(std::string_view(name));  // heterogeneous lookup: no allocation
    if (it != locs_.end()) return it->second;
    GLint l = glGetUniformLocation(id_, name);
    locs_.emplace(name, l);
    return l;
}

// ------------------------------------------------------------------ targets
bool RenderTarget::ensure(int width, int height, GLenum internalFormat, GLenum filter) {
    width = width < 1 ? 1 : width;
    height = height < 1 ? 1 : height;
    if (fbo && w == width && h == height && format == internalFormat) return true;
    release();
    if (width > maxTextureSize() || height > maxTextureSize()) {
        fprintf(stderr, "fract3d: %dx%d exceeds the GPU's texture size limit (%d)\n", width, height, maxTextureSize());
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {}
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    glTextureStorage2D(tex, 1, internalFormat, width, height);
    if (glGetError() == GL_OUT_OF_MEMORY) {
        fprintf(stderr, "fract3d: out of video memory for a %dx%d buffer\n", width, height);
        glDeleteTextures(1, &tex);
        tex = 0;
        return false;
    }
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, filter);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, filter);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, tex, 0);
    GLenum st = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "fract3d: framebuffer %dx%d incomplete (0x%x)\n", width, height, st);
        release();
        return false;
    }
    w = width;
    h = height;
    format = internalFormat;
    return true;
}

void RenderTarget::release() {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (tex) glDeleteTextures(1, &tex);
    fbo = tex = 0;
    w = h = 0;
}

static GLuint makeTexture(GLenum fmt, int w, int h, GLenum filter) {
    GLuint t = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &t);
    glTextureStorage2D(t, 1, fmt, w, h);
    glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, filter);
    glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, filter);
    glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

bool IndexTarget::ensure(int width, int height) {
    width = width < 1 ? 1 : width;
    height = height < 1 ? 1 : height;
    if (value && w == width && h == height) return true;
    release();
    if (width > maxTextureSize() || height > maxTextureSize()) {
        fprintf(stderr, "fract3d: %dx%d exceeds the GPU's texture size limit (%d)\n", width, height, maxTextureSize());
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {}
    value = makeTexture(GL_R32F, width, height, GL_NEAREST);
    aux = makeTexture(GL_R8, width, height, GL_NEAREST);
    if (glGetError() == GL_OUT_OF_MEMORY) {
        fprintf(stderr, "fract3d: out of video memory for a %dx%d iteration buffer\n", width, height);
        release();
        return false;
    }
    w = width;
    h = height;
    return true;
}

void IndexTarget::clear() {
    const float pending = -2.0f, zero = 0.0f;
    if (value) glClearTexImage(value, 0, GL_RED, GL_FLOAT, &pending);
    if (aux) glClearTexImage(aux, 0, GL_RED, GL_FLOAT, &zero);
}

void IndexTarget::release() {
    if (value) glDeleteTextures(1, &value);
    if (aux) glDeleteTextures(1, &aux);
    value = aux = 0;
    w = h = 0;
}

bool StateImages::ensure(int width, int height) {
    if (tex[0] && w >= width && h >= height) return true;
    release();
    while (glGetError() != GL_NO_ERROR) {}
    for (auto& t : tex) t = makeTexture(GL_RGBA32UI, width, height, GL_NEAREST);
    if (glGetError() == GL_OUT_OF_MEMORY) {
        release();
        return false;
    }
    w = width;
    h = height;
    return true;
}

void StateImages::release() {
    for (auto& t : tex)
        if (t) glDeleteTextures(1, &t), t = 0;
    w = h = 0;
}

// ------------------------------------------------------------------ timers
void PassTimer::begin(float amountOfWork, bool worstCase) {
    if (!q[0]) glGenQueries(kSlots, q);
    work[head] = amountOfWork;
    worst[head] = worstCase;
    glBeginQuery(GL_TIME_ELAPSED, q[head]);
}

void PassTimer::end() {
    glEndQuery(GL_TIME_ELAPSED);
    head = (head + 1) % kSlots;
}

void PassTimer::poll() {
    while (tail != head) {
        GLint avail = 0;
        glGetQueryObjectiv(q[tail], GL_QUERY_RESULT_AVAILABLE, &avail);
        if (!avail) break;
        GLuint64 ns = 0;
        glGetQueryObjectui64v(q[tail], GL_QUERY_RESULT, &ns);
        if (work[tail] > 0) {
            msPerWork = msPerWork * 0.5 + (ns / 1e6 / work[tail]) * 0.5;
            lastMs = ns / 1e6;
            lastWork = work[tail];
            fresh++;
            if (worst[tail]) worstMsPerWork = ns / 1e6 / work[tail];
        }
        tail = (tail + 1) % kSlots;
    }
}

void PassTimer::release() {
    if (q[0]) glDeleteQueries(kSlots, q);
    for (auto& x : q) x = 0;
    head = tail = 0;
}

void GpuTimer::begin(float tagA, float tagB) {
    if (!q[0]) glGenQueries(4, q);
    tag[idx][0] = tagA;
    tag[idx][1] = tagB;
    glBeginQuery(GL_TIME_ELAPSED, q[idx]);
}

void GpuTimer::end() {
    glEndQuery(GL_TIME_ELAPSED);
    pending[idx] = true;
    idx = (idx + 1) & 3;
}

void GpuTimer::poll() {
    fresh = false;
    for (int k = 0; k < 4; k++) {
        int i = (idx + k) & 3;  // oldest first
        if (!pending[i]) continue;
        GLint avail = 0;
        glGetQueryObjectiv(q[i], GL_QUERY_RESULT_AVAILABLE, &avail);
        if (!avail) break;
        GLuint64 ns = 0;
        glGetQueryObjectui64v(q[i], GL_QUERY_RESULT, &ns);
        lastMs = ns / 1e6;
        lastTag[0] = tag[i][0];
        lastTag[1] = tag[i][1];
        fresh = true;
        pending[i] = false;
    }
}

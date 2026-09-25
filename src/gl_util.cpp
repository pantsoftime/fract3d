#include "gl_util.h"

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

Program::~Program() {
    if (id_) glDeleteProgram(id_);
}

static GLuint compileStage(GLenum stage, const std::vector<const char*>& srcs, std::string& log) {
    GLuint s = glCreateShader(stage);
    glShaderSource(s, (GLsizei)srcs.size(), srcs.data(), nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        log.resize(len > 0 ? len : 1);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        glDeleteShader(s);
        return 0;
    }
    return s;
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

bool Program::build(const std::string& vertSrc, const std::vector<ShaderChunk>& fragChunks) {
    error_.clear();
    std::string log;
    const char* vs = vertSrc.c_str();
    GLuint v = compileStage(GL_VERTEX_SHADER, {vs}, log);
    if (!v) {
        error_ = "vertex shader:\n" + log;
        return false;
    }
    // Prefix every chunk with a #line directive so errors map back to files.
    std::vector<std::string> texts;
    texts.reserve(fragChunks.size());
    for (size_t i = 0; i < fragChunks.size(); i++) {
        if (i == 0)
            texts.push_back(fragChunks[i].text);  // chunk 0 holds #version, must come first
        else
            texts.push_back("#line 1 " + std::to_string(i) + "\n" + fragChunks[i].text + "\n");
    }
    std::vector<const char*> ptrs;
    for (auto& t : texts) ptrs.push_back(t.c_str());
    GLuint f = compileStage(GL_FRAGMENT_SHADER, ptrs, log);
    if (!f) {
        glDeleteShader(v);
        error_ = prettifyLog(log, fragChunks);
        return false;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        log.resize(len > 0 ? len : 1);
        glGetProgramInfoLog(p, len, nullptr, log.data());
        glDeleteProgram(p);
        error_ = "link:\n" + prettifyLog(log, fragChunks);
        return false;
    }
    if (id_) glDeleteProgram(id_);
    id_ = p;
    locs_.clear();
    return true;
}

GLint Program::loc(const char* name) {
    auto it = locs_.find(name);
    if (it != locs_.end()) return it->second;
    GLint l = glGetUniformLocation(id_, name);
    locs_.emplace(name, l);
    return l;
}

void RenderTarget::ensure(int width, int height, GLenum internalFormat, GLenum filter) {
    width = width < 1 ? 1 : width;
    height = height < 1 ? 1 : height;
    if (fbo && w == width && h == height && format == internalFormat) return;
    release();
    w = width;
    h = height;
    format = internalFormat;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    glTextureStorage2D(tex, 1, internalFormat, w, h);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, filter);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, filter);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, tex, 0);
}

void RenderTarget::release() {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (tex) glDeleteTextures(1, &tex);
    fbo = tex = 0;
    w = h = 0;
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

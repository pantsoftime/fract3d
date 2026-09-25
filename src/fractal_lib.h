#pragma once
#include <filesystem>
#include <string>
#include <vector>

enum class ParamType { Float, Int, Bool, Vec2, Vec3, Vec4, Choice, Color };

struct Param {
    ParamType type = ParamType::Float;
    std::string id, label, desc;
    float def[4] = {0, 0, 0, 0};
    float value[4] = {0, 0, 0, 0};
    float minV = 0.0f, maxV = 1.0f;
    bool logScale = false;
    // "hires": the value is also kept in double precision (exact), for parameters like
    // the landscape's center that deep zooms need to the last digit. Anything that sets
    // only `value` (sliders, animation, camera paths) makes `value` win again - see precise().
    bool hires = false;
    double exact[4] = {0, 0, 0, 0};
    double precise(int k) const { return hires && (float)exact[k] == value[k] ? exact[k] : (double)value[k]; }
    void setPrecise(int k, double v) { exact[k] = v, value[k] = (float)v; }
    std::vector<std::string> choices;
    // animation: a slow sine sweep across the parameter's range
    bool animate = false;
    float animSpeed = 0.15f;  // Hz
    float animDepth = 0.25f;  // fraction of range
    float animBase[4] = {0, 0, 0, 0};

    int components() const;
    const char* glslType() const;
    void reset();
};

struct RenderHints {
    float stepFactor = 0.9f, detail = 0.5f, maxDist = 6.0f;
    int maxSteps = 300;
};

struct Fractal {
    std::string key;  // file stem, e.g. "mandelbulb"
    std::string name, category, credit;
    std::string about;  // lesson text (lines, lightly marked up)
    std::string code;   // GLSL body
    std::vector<Param> params;
    float camPos[3] = {0, 0, -3}, camTarget[3] = {0, 0, 0};
    RenderHints hints;
    // "@look key=value ..." — curated colors/lighting applied when the fractal is selected
    std::vector<std::pair<std::string, std::string>> look;
    std::filesystem::path path;
    std::filesystem::file_time_type mtime{};
    std::string parseError;

    Param* find(const std::string& id);
    const Param* find(const std::string& id) const;
    std::string uniformDecls() const;
};

class FractalLibrary {
public:
    void load(const std::filesystem::path& dir);
    // Re-reads files whose modification time changed; keeps current param values
    // when the parameter still exists. Returns indices of reloaded fractals.
    std::vector<int> reloadChanged();
    std::vector<Fractal>& all() { return fractals_; }
    const std::vector<Fractal>& all() const { return fractals_; }
    int indexOf(const std::string& key) const;

private:
    std::filesystem::path dir_;
    std::vector<Fractal> fractals_;
};

bool parseFractalFile(const std::filesystem::path& path, Fractal& out);

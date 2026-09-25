#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void usage() {
    printf(
        "fract3d - a 3D fractal explorer in the spirit of Fractint\n\n"
        "usage: fract3d [options] [file.par]\n"
        "  --fractal KEY        start on a fractal (mandelbulb, mandelbox, menger, ...)\n"
        "  --par FILE           load a saved view\n"
        "  --2d                 start in Classic 2D mode\n"
        "  --pt / --rt          path traced / real-time rendering\n"
        "  --render OUT.png     render an image and exit (no window shown)\n"
        "  --size WxH           image size for --render (default 1920x1080)\n"
        "  --samples N          samples per pixel for --render\n"
        "  --theme modern|fractint   UI theme\n"
        "  --gl-debug           print OpenGL errors and performance warnings\n"
        "  --formula NAME       start in Classic 2D with a formula from formulas/*.frm\n");
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);  // status lines appear promptly even when piped
    CliOptions o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for %s\n", a.c_str());
                exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--fractal") o.fractal = next();
        else if (a == "--par") o.parFile = next();
        else if (a == "--2d") o.mode2d = 1;
        else if (a == "--pt") o.pathTrace = 1;
        else if (a == "--rt") o.pathTrace = 0;
        else if (a == "--render") { o.shotPath = next(); o.hidden = true; }
        else if (a == "--size") {
            std::string s = next();
            if (sscanf(s.c_str(), "%dx%d", &o.shotW, &o.shotH) != 2) { fprintf(stderr, "bad --size\n"); return 2; }
        }
        else if (a == "--samples") o.shotSamples = atoi(next().c_str());
        else if (a == "--ui-shot") { o.uiShotPath = next(); o.hidden = true; }
        else if (a == "--frames") o.uiShotFrames = atoi(next().c_str());
        else if (a == "--theme") o.theme = next() == "fractint" ? 1 : 0;
        else if (a == "--gl-debug") o.glDebug = true;
        else if (a == "--formula") o.formula = next();
        else if (a == "--mouse") {  // testing: pretend the cursor is here (window coordinates)
            std::string s = next();
            if (sscanf(s.c_str(), "%f,%f", &o.fakeMouse[0], &o.fakeMouse[1]) != 2) { fprintf(stderr, "bad --mouse\n"); return 2; }
        }
        else if (a == "--inset") o.insetOn = true;
        else if (a == "--orbit") o.orbitOn = true;
        else if (a == "--gl-debug-verbose") o.glDebug = o.glDebugVerbose = true;
        else if (a.size() > 4 && a.substr(a.size() - 4) == ".par") o.parFile = a;
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
    }
    if (o.hidden && !o.shotW) { o.shotW = 1920; o.shotH = 1080; }

    App app;
    if (!app.init(o)) return 1;
    app.run();
    app.shutdown();
    return app.exitCode;
}

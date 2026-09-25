# Vendored dependencies

Copied in-tree (not submodules) so the project builds offline from a single
clone. Each is pinned; update by replacing the directory wholesale.

| Library | Version | Source | Used for |
|---|---|---|---|
| Dear ImGui | v1.92.9 (tag) | https://github.com/ocornut/imgui | overlay UI (`imgui/`, only the GLFW + OpenGL3 backends are compiled) |
| stb_image_write | 1.16 | https://github.com/nothings/stb | PNG output |
| stb_image | 2.30 | https://github.com/nothings/stb | PNG input (tests/imgdiff, PAR-in-PNG loading) |

Only what the build uses is kept of ImGui: the core (`imgui/*.h`, `*.cpp`), the GLFW and
OpenGL 3 backends (`backends/imgui_impl_glfw.*`, `imgui_impl_opengl3*`), `misc/cpp/`
(std::string input widgets) and `LICENSE.txt`. To update it:

```bash
rm -rf third_party/imgui
git clone --depth 1 --branch vX.Y.Z https://github.com/ocornut/imgui.git third_party/imgui
cd third_party/imgui
rm -rf .git .github docs examples .editorconfig .gitattributes misc/fonts misc/freetype misc/debuggers misc/single_file misc/README.txt
find backends -type f ! -name 'imgui_impl_glfw.*' ! -name 'imgui_impl_opengl3*' -delete
find backends -type d -empty -delete
cd ../.. && cmake --build build && ctest --test-dir build
```

Both are MIT/public-domain licensed; their license files are kept alongside.

cmake_minimum_required(VERSION 3.24)
project(fract3d LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
  ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC PkgConfig::GLFW)

add_executable(fract3d
  src/main.cpp
  src/app.cpp
  src/ui.cpp
  src/gl_util.cpp
  src/fractal_lib.cpp
  src/camera.cpp
  src/renderer.cpp
  src/palettes.cpp
  src/params_io.cpp)
target_include_directories(fract3d PRIVATE src)
target_include_directories(fract3d SYSTEM PRIVATE third_party/stb)
target_link_libraries(fract3d PRIVATE imgui PkgConfig::GLFW PkgConfig::EPOXY)
target_compile_options(fract3d PRIVATE -Wall -Wextra -Wno-unused-parameter)
# Data directory is found at runtime relative to the executable, falling back to the source tree.
target_compile_definitions(fract3d PRIVATE FRACT3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# `cmake --install build --prefix ~/.local` puts the binary in bin/ and the data
# (shaders, fractals, presets, docs) in share/fract3d/, where the app looks for it.
include(GNUInstallDirs)
install(TARGETS fract3d RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY shaders fractals presets palettes DESTINATION ${CMAKE_INSTALL_DATADIR}/fract3d)
install(DIRECTORY docs DESTINATION ${CMAKE_INSTALL_DATADIR}/fract3d PATTERN "images" EXCLUDE)
install(FILES packaging/fract3d.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/fract3d.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)

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
  src/params_io.cpp
  src/sanitize.cpp
  src/bluenoise.cpp
  src/formula.cpp)
target_include_directories(fract3d PRIVATE src)
target_include_directories(fract3d SYSTEM PRIVATE third_party/stb)
target_link_libraries(fract3d PRIVATE imgui PkgConfig::GLFW PkgConfig::EPOXY)
target_compile_options(fract3d PRIVATE -Wall -Wextra -Wno-unused-parameter)

option(FRACT3D_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(FRACT3D_SANITIZE)
  foreach(t fract3d imgui)
    target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${t} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()
# Data directory is found at runtime relative to the executable, falling back to the source tree.
target_compile_definitions(fract3d PRIVATE FRACT3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# `cmake --install build --prefix ~/.local` puts the binary in bin/ and the data
# (shaders, fractals, presets, docs) in share/fract3d/, where the app looks for it.
include(GNUInstallDirs)
install(TARGETS fract3d RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY shaders fractals presets palettes formulas DESTINATION ${CMAKE_INSTALL_DATADIR}/fract3d)
install(DIRECTORY docs DESTINATION ${CMAKE_INSTALL_DATADIR}/fract3d PATTERN "images" EXCLUDE)
install(FILES packaging/fract3d.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/fract3d.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)

# ---------------------------------------------------------------- tests
# Render regression tests. They need a GPU and a display (a hidden window is
# created). Run with: ctest --test-dir build   (golden images: tests/golden/,
# regenerate after intentional visual changes with: cmake --build build -t update-golden)
option(FRACT3D_TESTS "Register render regression tests" ON)
if(FRACT3D_TESTS)
  enable_testing()
  add_executable(imgdiff tools/imgdiff.cpp)
  target_include_directories(imgdiff SYSTEM PRIVATE third_party/stb)

  set(TEST_OUT ${CMAKE_BINARY_DIR}/test-out)
  file(MAKE_DIRECTORY ${TEST_OUT})
  set(GOLDEN_SIZE 160x90)
  set(GOLDEN_SAMPLES 8)

  # every fractal's shaders must compile and render
  file(GLOB FRACTAL_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/fractals/*.glsl)
  foreach(f ${FRACTAL_FILES})
    get_filename_component(name ${f} NAME_WE)
    add_test(NAME compile.${name}
             COMMAND fract3d --fractal ${name} --render ${TEST_OUT}/compile-${name}.png --size 64x36 --samples 1)
  endforeach()

  # every preset against its golden image
  file(GLOB PRESET_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/presets/*.par)
  set(update_cmds)
  foreach(p ${PRESET_FILES})
    get_filename_component(name ${p} NAME_WE)
    set(args -DAPP=$<TARGET_FILE:fract3d> -DDIFF=$<TARGET_FILE:imgdiff> -DPAR=${p}
             -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/${name}.png -DOUT=${TEST_OUT}/golden-${name}.png
             -DSIZE=${GOLDEN_SIZE} -DSAMPLES=${GOLDEN_SAMPLES})
    add_test(NAME golden.${name} COMMAND ${CMAKE_COMMAND} ${args} -P ${CMAKE_SOURCE_DIR}/tests/render_compare.cmake)
    list(APPEND update_cmds COMMAND ${CMAKE_COMMAND} ${args} -DUPDATE=1 -P ${CMAKE_SOURCE_DIR}/tests/render_compare.cmake)
  endforeach()
  add_custom_target(update-golden ${update_cmds} DEPENDS fract3d imgdiff
                    COMMENT "Re-rendering golden images into tests/golden/")

  # saved PARs must reproduce their image exactly (one 3D and one 2D view)
  foreach(p 02-seahorse-valley 10-menger-crystal)
    add_test(NAME roundtrip.${p}
             COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d> -DDIFF=$<TARGET_FILE:imgdiff>
                     -DPAR=${CMAKE_SOURCE_DIR}/presets/${p}.par -DOUT=${TEST_OUT} -DNAME=roundtrip-${p}
                     -DSIZE=160x90 -DSAMPLES=4 -P ${CMAKE_SOURCE_DIR}/tests/par_roundtrip.cmake)
  endforeach()

  # malformed or hostile PAR files must load safely (the render may fall back)
  file(GLOB HOSTILE_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/hostile/*.par)
  foreach(p ${HOSTILE_FILES})
    get_filename_component(name ${p} NAME_WE)
    add_test(NAME hostile.${name}
             COMMAND fract3d --par ${p} --render ${TEST_OUT}/hostile-${name}.png --size 64x36 --samples 1)
  endforeach()

  # formula parser/transpiler unit tests, and every shipped formula on the GPU
  add_executable(formula_test tests/formula_test.cpp src/formula.cpp)
  target_include_directories(formula_test PRIVATE src)
  add_test(NAME formula.unit COMMAND formula_test ${CMAKE_SOURCE_DIR}/formulas/classics.frm)
  file(STRINGS ${CMAKE_SOURCE_DIR}/formulas/classics.frm FRM_HEADERS REGEX "^[A-Za-z0-9_]+ *[({]")
  foreach(h ${FRM_HEADERS})
    string(REGEX MATCH "^[A-Za-z0-9_]+" fname "${h}")
    add_test(NAME formula.${fname} COMMAND fract3d --formula ${fname} --render ${TEST_OUT}/formula-${fname}.png --size 96x54)
  endforeach()
  add_test(NAME formula.in-3d-landscape
           COMMAND fract3d --par ${CMAKE_SOURCE_DIR}/tests/custom-landscape.par --render ${TEST_OUT}/formula-3d.png --size 96x54 --samples 1)
  add_test(NAME ui.julia-inset-and-orbit COMMAND fract3d --2d --inset --orbit --mouse 700,330 --ui-shot ${TEST_OUT}/ui-inset.png --size 1280x720 --frames 30)
  add_test(NAME ui.formula-2d COMMAND fract3d --formula Spider --ui-shot ${TEST_OUT}/ui-formula.png --size 1280x720 --frames 20)

  # the overlay UI (both themes, both modes) must draw without errors
  add_test(NAME ui.modern-3d COMMAND fract3d --fractal mandelbulb --ui-shot ${TEST_OUT}/ui-3d.png --size 1280x720 --frames 30)
  add_test(NAME ui.fractint-2d COMMAND fract3d --2d --theme fractint --ui-shot ${TEST_OUT}/ui-2d.png --size 1280x720 --frames 30)

  # an impossible render must fail cleanly with a non-zero exit code
  add_test(NAME oversize_render_fails
           COMMAND fract3d --2d --render ${TEST_OUT}/oversize.png --size 40000x100)
  set_tests_properties(oversize_render_fails PROPERTIES WILL_FAIL TRUE)
endif()

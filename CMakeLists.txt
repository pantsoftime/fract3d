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
pkg_check_modules(MPFR REQUIRED IMPORTED_TARGET mpfr gmp)

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
  src/formula.cpp
  src/fractint_par.cpp
  src/pngmeta.cpp
  src/animation.cpp
  src/deepzoom.cpp)
target_include_directories(fract3d PRIVATE src)
target_include_directories(fract3d SYSTEM PRIVATE third_party/stb)
target_link_libraries(fract3d PRIVATE imgui PkgConfig::GLFW PkgConfig::EPOXY PkgConfig::MPFR)
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
  add_executable(itercheck tools/itercheck.cpp src/formula.cpp src/gl_util.cpp)
  target_include_directories(itercheck PRIVATE src)
  target_link_libraries(itercheck PRIVATE PkgConfig::MPFR PkgConfig::EPOXY)

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

  # every preset (and every test view) against its golden image
  file(GLOB PRESET_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/presets/*.par ${CMAKE_SOURCE_DIR}/tests/views/*.par)
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

  # saved PARs must reproduce their image exactly: two presets, plus one view per
  # feature that PAR files carry (gradients, cosine palettes, formulas, animation, deep centers)
  file(GLOB ROUNDTRIP_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/roundtrip/*.par)
  foreach(p ${CMAKE_SOURCE_DIR}/presets/02-seahorse-valley.par ${CMAKE_SOURCE_DIR}/presets/10-menger-crystal.par ${ROUNDTRIP_FILES})
    get_filename_component(name ${p} NAME_WE)
    add_test(NAME roundtrip.${name}
             COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d> -DDIFF=$<TARGET_FILE:imgdiff>
                     -DPAR=${p} -DOUT=${TEST_OUT} -DNAME=roundtrip-${name}
                     -DSIZE=160x90 -DSAMPLES=4 -P ${CMAKE_SOURCE_DIR}/tests/par_roundtrip.cmake)
  endforeach()

  # malformed or hostile PAR files must be handled safely: repaired and rendered, or
  # refused with exit code 1 - never a crash
  file(GLOB HOSTILE_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/hostile/*.par)
  foreach(p ${HOSTILE_FILES})
    get_filename_component(name ${p} NAME_WE)
    add_test(NAME hostile.${name}
             COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d>
                     "-DARGS=--par;${p};--render;${TEST_OUT}/hostile-${name}.png;--size;64x36;--samples;1"
                     -P ${CMAKE_SOURCE_DIR}/tests/no_crash.cmake)
  endforeach()

  # Fractint .PAR import: every supported sample entry renders; an unsupported one fails
  # with a clear message; the chooser window draws; an import survives a round trip
  foreach(entry Blue_Classic Seahorse_Rainbow Dragon_Julia Spider_Formula)
    add_test(NAME fractint.render-${entry}
             COMMAND fract3d --par ${CMAKE_SOURCE_DIR}/tests/fractint/sample.par --par-entry ${entry}
                     --render ${TEST_OUT}/fractint-${entry}.png --size 160x90)
  endforeach()
  add_test(NAME fractint.unsupported-type
           COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d>
                   "-DARGS=--par;${CMAKE_SOURCE_DIR}/tests/fractint/sample.par;--par-entry;Not_Supported;--render;${TEST_OUT}/fractint-bad.png;--size;64x36"
                   "-DEXPECT=type 'lsystem' isn't supported" -P ${CMAKE_SOURCE_DIR}/tests/expect_failure.cmake)
  add_test(NAME ui.fractint-import COMMAND fract3d --par ${CMAKE_SOURCE_DIR}/tests/fractint/sample.par
                                           --ui-shot ${TEST_OUT}/ui-fractint.png --size 1600x900 --frames 20)
  add_test(NAME roundtrip.fractint-import
           COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d> -DDIFF=$<TARGET_FILE:imgdiff>
                   -DPAR=${CMAKE_SOURCE_DIR}/tests/fractint/sample.par -DOUT=${TEST_OUT} -DNAME=roundtrip-fractint
                   -DSIZE=160x90 -DSAMPLES=1 -P ${CMAKE_SOURCE_DIR}/tests/par_roundtrip.cmake)

  # app logic that normally needs input: undo/redo, PAR round trips, sanitizing
  add_test(NAME app.self-test COMMAND fract3d --self-test)

  # formula parser/transpiler/interpreter unit tests, and every shipped formula on the GPU
  # checked against the interpreter
  add_executable(formula_test tests/formula_test.cpp src/formula.cpp)
  target_include_directories(formula_test PRIVATE src)
  add_executable(fractint_test tests/fractint_test.cpp src/fractint_par.cpp src/palettes.cpp src/gl_util.cpp)
  target_include_directories(fractint_test PRIVATE src)
  target_link_libraries(fractint_test PRIVATE PkgConfig::EPOXY)
  add_test(NAME fractint.unit COMMAND fractint_test ${CMAKE_SOURCE_DIR}/tests/fractint/sample.par)
  set_tests_properties(fractint.unit PROPERTIES LABELS nogpu)
  add_test(NAME formula.unit COMMAND formula_test ${CMAKE_SOURCE_DIR}/formulas/classics.frm)
  set_tests_properties(formula.unit PROPERTIES LABELS nogpu)  # runs in CI (no GPU there)
  file(STRINGS ${CMAKE_SOURCE_DIR}/formulas/classics.frm FRM_HEADERS REGEX "^[A-Za-z0-9_]+ *[({]")
  foreach(h ${FRM_HEADERS})
    string(REGEX MATCH "^[A-Za-z0-9_]+" fname "${h}")
    add_test(NAME formula.${fname}
             COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d> -DCHECK=$<TARGET_FILE:itercheck>
                     -DFRM=${CMAKE_SOURCE_DIR}/formulas/classics.frm -DNAME=${fname} -DOUT=${TEST_OUT}
                     -P ${CMAKE_SOURCE_DIR}/tests/formula_check.cmake)
  endforeach()
  file(STRINGS ${CMAKE_SOURCE_DIR}/tests/formulas/language.frm LANG_HEADERS REGEX "^[A-Za-z0-9_]+ *[({]")
  foreach(h ${LANG_HEADERS})
    string(REGEX MATCH "^[A-Za-z0-9_]+" fname "${h}")
    add_test(NAME formula.language-${fname}
             COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d> -DCHECK=$<TARGET_FILE:itercheck>
                     -DFRM=${CMAKE_SOURCE_DIR}/tests/formulas/language.frm -DNAME=${fname} -DOUT=${TEST_OUT}
                     -P ${CMAKE_SOURCE_DIR}/tests/formula_check.cmake)
  endforeach()
  add_test(NAME ui.formula-inset-and-orbit COMMAND fract3d --formula FnMandel --inset --orbit --mouse 820,300
                                                   --ui-shot ${TEST_OUT}/ui-formula-inset.png --size 1600x900 --frames 30)
  add_test(NAME formula.in-3d-landscape
           COMMAND fract3d --par ${CMAKE_SOURCE_DIR}/tests/custom-landscape.par --render ${TEST_OUT}/formula-3d.png --size 96x54 --samples 1)
  add_test(NAME ui.julia-inset-and-orbit COMMAND fract3d --2d --inset --orbit --mouse 700,330 --ui-shot ${TEST_OUT}/ui-inset.png --size 1280x720 --frames 30)
  add_test(NAME ui.windows COMMAND fract3d --formula Nova --open gradient --open formula --open help --open render
                                   --ui-shot ${TEST_OUT}/ui-windows.png --size 1920x1080 --frames 30)
  add_test(NAME ui.formula-2d COMMAND fract3d --formula Spider --ui-shot ${TEST_OUT}/ui-formula.png --size 1280x720 --frames 20)

  # the overlay UI (both themes, both modes) must draw without errors
  add_test(NAME ui.modern-3d COMMAND fract3d --fractal mandelbulb --ui-shot ${TEST_OUT}/ui-3d.png --size 1280x720 --frames 30)
  add_test(NAME ui.fractint-2d COMMAND fract3d --2d --theme fractint --ui-shot ${TEST_OUT}/ui-2d.png --size 1280x720 --frames 30)

  # camera path -> video (only when ffmpeg is installed)
  find_program(FFMPEG_EXE ffmpeg)
  if(FFMPEG_EXE)
    add_test(NAME video.export
             COMMAND fract3d --path ${CMAKE_SOURCE_DIR}/tests/menger-twist.f3dpath --render ${TEST_OUT}/path.mp4
                     --size 160x90 --samples 1 --fps 5)
    # cancelling stops the encoder and reports it (exit code 0: a cancel isn't an error)
    add_test(NAME video.cancel
             COMMAND fract3d --path ${CMAKE_SOURCE_DIR}/tests/menger-twist.f3dpath --render ${TEST_OUT}/cancel.mp4
                     --size 160x90 --samples 1 --fps 5 --cancel-after-frames 3)
    set_tests_properties(video.cancel PROPERTIES PASS_REGULAR_EXPRESSION "cancelled after 3 frames")
  endif()
  # camera paths arrive exactly at their keyframes (both ends of a deep-to-shallow zoom)
  foreach(end first:0 last:1)
    string(REPLACE ":" ";" parts ${end})
    list(GET parts 0 which)
    list(GET parts 1 t)
    add_test(NAME path.deep-to-shallow-${which}
             COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d> -DDIFF=$<TARGET_FILE:imgdiff>
                     -DPATHFILE=${CMAKE_SOURCE_DIR}/tests/paths/deep-to-shallow.f3dpath -DTIME=${t}
                     -DPAR=${CMAKE_SOURCE_DIR}/tests/paths/deep-to-shallow-${which}.par -DOUT=${TEST_OUT}
                     -DNAME=path-${which} -DSIZE=160x90 -P ${CMAKE_SOURCE_DIR}/tests/path_endpoint.cmake)
  endforeach()
  # a camera path as numbered PNG images (no ffmpeg needed); each frame carries its view
  add_test(NAME video.png-sequence
           COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d> -DPATHFILE=${CMAKE_SOURCE_DIR}/tests/menger-twist.f3dpath
                   -DOUT=${TEST_OUT}/sequence -P ${CMAKE_SOURCE_DIR}/tests/png_sequence.cmake)
  add_test(NAME ui.path-window COMMAND fract3d --fractal menger --path ${CMAKE_SOURCE_DIR}/tests/menger-twist.f3dpath
                                       --open path --ui-shot ${TEST_OUT}/ui-path.png --size 1600x900 --frames 20)

  # deep zoom: perturbation checked against exact MPFR arithmetic (tools/itercheck). The
  # allowances sit just above each view's own conditioning: moving the 1e-20 view's center
  # by a millionth of a pixel already changes 0.42% of its pixels.
  foreach(spec "seahorse-5e-10|-0.743643887037151|0.131825904205330|5e-10|8000|0.005"
               "seahorse-1e-20|-0.743643887037158704752191506114774|0.131825904205311970493132056385139|1e-20|20000|0.01"
               "dendrite-perturb|-0.10109636384562|0.95628651080914|1e-10|4000|0.0005")
    string(REPLACE "|" ";" f "${spec}")
    list(GET f 0 name)
    list(GET f 1 re)
    list(GET f 2 im)
    list(GET f 3 h)
    list(GET f 4 it)
    list(GET f 5 tol)
    add_test(NAME deep.exact-${name}
             COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d> -DCHECK=$<TARGET_FILE:itercheck>
                     -DPAR=${CMAKE_SOURCE_DIR}/tests/deep/${name}.par -DOUT=${TEST_OUT} -DNAME=deep-${name} -DSIZE=240x135
                     -DRE=${re} -DIM=${im} -DHEIGHT=${h} -DMAXITER=${it} -DMAXMISMATCH=${tol}
                     -P ${CMAKE_SOURCE_DIR}/tests/iter_check.cmake)
  endforeach()
  # skipping ahead (BLA) changes nothing visible: the 10^32x minibrot with and without it
  add_test(NAME deep.bla-agrees
           COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d> -DDIFF=$<TARGET_FILE:imgdiff>
                   -DPAR=${CMAKE_SOURCE_DIR}/presets/18-minibrot-1e32.par
                   -DGOLDEN_PAR=${CMAKE_SOURCE_DIR}/tests/deep/minibrot-no-bla.par -DOUT=${TEST_OUT} -DSIZE=240x135
                   -P ${CMAKE_SOURCE_DIR}/tests/render_pair.cmake)
  # ...and agrees with plain doubles where both are accurate (a well-conditioned view)
  add_test(NAME deep.agrees-with-fp64
           COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d> -DDIFF=$<TARGET_FILE:imgdiff>
                   -DPAR=${CMAKE_SOURCE_DIR}/tests/deep/dendrite-perturb.par
                   -DGOLDEN_PAR=${CMAKE_SOURCE_DIR}/tests/deep/dendrite-fp64.par -DOUT=${TEST_OUT} -DSIZE=240x135
                   -P ${CMAKE_SOURCE_DIR}/tests/render_pair.cmake)

  # an impossible render must fail cleanly: a non-zero exit code AND the size-limit message
  # (so a crash doesn't count as passing)
  add_test(NAME oversize_render_fails
           COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:fract3d> "-DARGS=--2d;--render;${TEST_OUT}/oversize.png;--size;40000x100"
                   "-DEXPECT=exceeds the GPU's texture size limit" -P ${CMAKE_SOURCE_DIR}/tests/expect_failure.cmake)
endif()

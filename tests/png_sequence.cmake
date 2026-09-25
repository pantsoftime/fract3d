# Exports a camera path as numbered PNGs and checks the frames.   -DAPP -DPATHFILE -DOUT=dir
file(REMOVE_RECURSE ${OUT})
execute_process(COMMAND ${APP} --path ${PATHFILE} --render ${OUT}/f-%03d.png --size 64x36 --samples 1 --fps 5
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "export failed (exit ${rc})")
endif()
file(GLOB frames ${OUT}/f-*.png)
list(LENGTH frames n)
if(NOT n EQUAL 21)  # 4 seconds at 5 fps, both ends included
  message(FATAL_ERROR "expected 21 frames, got ${n}")
endif()
if(NOT EXISTS ${OUT}/f-000.png OR NOT EXISTS ${OUT}/f-020.png)
  message(FATAL_ERROR "frames aren't numbered f-000 ... f-020")
endif()
# every frame carries its view: re-render the middle one from its own PNG
execute_process(COMMAND ${APP} --par ${OUT}/f-010.png --render ${OUT}/check.png --size 64x36 --samples 1
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "a frame's embedded view doesn't load (exit ${rc})")
endif()

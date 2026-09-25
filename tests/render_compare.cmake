# Renders a PAR file and compares the image with its golden copy.
#   -DAPP=fract3d -DDIFF=imgdiff -DPAR=... -DGOLDEN=... -DOUT=... -DSIZE=WxH -DSAMPLES=N [-DUPDATE=1]
execute_process(COMMAND ${APP} --par ${PAR} --render ${OUT} --size ${SIZE} --samples ${SAMPLES}
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "render of ${PAR} failed (exit ${rc})")
endif()
if(UPDATE)
  file(COPY_FILE ${OUT} ${GOLDEN})
  message(STATUS "updated ${GOLDEN}")
  return()
endif()
if(NOT EXISTS ${GOLDEN})
  message(FATAL_ERROR "no golden image ${GOLDEN} - build the update-golden target")
endif()
execute_process(COMMAND ${DIFF} ${GOLDEN} ${OUT} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "${OUT} differs from ${GOLDEN}")
endif()

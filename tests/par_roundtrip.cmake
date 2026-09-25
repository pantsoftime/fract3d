# A render embeds its PAR in the PNG; rendering from that PNG must reproduce the
# image exactly. Catches settings that aren't saved or aren't restored, and
# checks the PNG metadata round trip.
#   -DAPP=fract3d -DDIFF=imgdiff -DPAR=... -DOUT=dir -DNAME=... -DSIZE=WxH -DSAMPLES=N
set(first ${OUT}/${NAME}-a.png)
set(second ${OUT}/${NAME}-b.png)
execute_process(COMMAND ${APP} --par ${PAR} --render ${first} --size ${SIZE} --samples ${SAMPLES}
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "first render failed (exit ${rc})")
endif()
execute_process(COMMAND ${APP} --par ${first} --render ${second} --size ${SIZE} --samples ${SAMPLES}
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "render of the saved PAR failed (exit ${rc})")
endif()
execute_process(COMMAND ${DIFF} ${first} ${second} --exact RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the saved PAR did not reproduce the image")
endif()

# A render writes a PAR next to its PNG; rendering that PAR must reproduce the
# image exactly. Catches settings that aren't saved or aren't restored.
#   -DAPP=fract3d -DDIFF=imgdiff -DPAR=... -DOUT=dir -DNAME=... -DSIZE=WxH -DSAMPLES=N
set(first ${OUT}/${NAME}-a.png)
set(second ${OUT}/${NAME}-b.png)
execute_process(COMMAND ${APP} --par ${PAR} --render ${first} --size ${SIZE} --samples ${SAMPLES}
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "first render failed (exit ${rc})")
endif()
string(REPLACE ".png" ".par" saved ${first})
execute_process(COMMAND ${APP} --par ${saved} --render ${second} --size ${SIZE} --samples ${SAMPLES}
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "render of the saved PAR failed (exit ${rc})")
endif()
execute_process(COMMAND ${DIFF} ${first} ${second} --exact RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the saved PAR did not reproduce the image")
endif()

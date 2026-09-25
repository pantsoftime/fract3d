# Renders a camera path at time TIME and the keyframe PAR expected there; the two
# images must match (the path must arrive exactly at its keyframes).
#   -DAPP -DDIFF -DPATHFILE -DTIME -DPAR -DOUT=dir -DNAME -DSIZE
set(a ${OUT}/${NAME}-path.png)
set(b ${OUT}/${NAME}-key.png)
execute_process(COMMAND ${APP} --path ${PATHFILE} --path-time ${TIME} --render ${a} --size ${SIZE} --samples 2
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "path render failed (exit ${rc})")
endif()
execute_process(COMMAND ${APP} --par ${PAR} --render ${b} --size ${SIZE} --samples 2 RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "keyframe render failed (exit ${rc})")
endif()
execute_process(COMMAND ${DIFF} ${b} ${a} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the path at t=${TIME} doesn't match ${PAR}")
endif()

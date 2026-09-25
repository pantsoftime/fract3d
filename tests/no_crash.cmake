# Runs the app on hostile input: it may render (exit 0) or refuse cleanly (exit 1),
# but it must not crash.   -DAPP -DARGS=a;b;c
execute_process(COMMAND ${APP} ${ARGS} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT (rc STREQUAL "0" OR rc STREQUAL "1"))
  message(FATAL_ERROR "the app crashed or failed badly (${rc}):\n${out}${err}")
endif()

# Runs the app, expecting it to fail with a particular message (not to crash).
#   -DAPP -DARGS=a;b;c -DEXPECT=text
execute_process(COMMAND ${APP} ${ARGS} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 0)
  message(FATAL_ERROR "expected a failure, but the app succeeded")
endif()
if(NOT rc MATCHES "^[0-9]+$")
  message(FATAL_ERROR "the app crashed (${rc}) instead of failing cleanly")
endif()
string(FIND "${out}${err}" "${EXPECT}" at)
if(at LESS 0)
  message(FATAL_ERROR "exit ${rc}, but the output doesn't mention '${EXPECT}':\n${out}${err}")
endif()

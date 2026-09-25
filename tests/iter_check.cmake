# Renders a 2D PAR and checks its iteration counts against exact MPFR arithmetic.
#   -DAPP -DCHECK=itercheck -DPAR -DOUT=dir -DNAME -DSIZE -DRE -DIM -DHEIGHT -DMAXITER -DMAXMISMATCH
execute_process(COMMAND ${APP} --par ${PAR} --render ${OUT}/${NAME}.png --size ${SIZE} --dump-iterations ${OUT}/${NAME}.iter
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "render failed (exit ${rc})")
endif()
execute_process(COMMAND ${CHECK} ${OUT}/${NAME}.iter ${RE} ${IM} ${HEIGHT} ${MAXITER} --max-mismatch ${MAXMISMATCH} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "iteration counts differ from exact arithmetic")
endif()

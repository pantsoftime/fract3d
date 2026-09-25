# Renders a formula on the GPU and checks its iteration counts against the CPU
# interpreter (tools/itercheck --frm): the transpiled GLSL must mean what the
# formula says. Rendered without supersampling, so every sample is a pixel center.
#   -DAPP -DCHECK -DFRM -DNAME -DOUT
file(WRITE ${OUT}/formula-${NAME}.par "mode = 2d\nclassic.formula = 8\nformula.name = ${NAME}\nclassic.supersample = 1\n")
execute_process(COMMAND ${APP} --frm ${FRM} --formula ${NAME} --par ${OUT}/formula-${NAME}.par --render ${OUT}/formula-${NAME}.png
                        --size 320x180 --dump-iterations ${OUT}/formula-${NAME}.iter RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "render failed (exit ${rc})")
endif()
execute_process(COMMAND ${CHECK} ${OUT}/formula-${NAME}.iter --frm ${FRM} ${NAME} --max-mismatch 0.03 RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the GPU and the interpreter disagree on ${NAME}")
endif()

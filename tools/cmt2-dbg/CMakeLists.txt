add_llvm_executable(cmt2-dbg
  cmt2-dbg.cpp
  Interpreter.cpp
)

llvm_update_compile_flags(cmt2-dbg)
target_link_libraries(cmt2-dbg PRIVATE
  CIRCTCmt2
  CIRCTCmt2Interpreter
  CIRCTCmt2Transforms
  CIRCTComb
  CIRCTFIRRTL
  CIRCTHW
  CIRCTSupport
  MLIRArithDialect
  MLIRFuncDialect
  MLIRIR
  MLIRParser
  MLIRSupport
  MLIRTransforms
)

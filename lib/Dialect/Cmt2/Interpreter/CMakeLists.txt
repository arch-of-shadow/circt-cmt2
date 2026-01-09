add_circt_library(CIRCTCmt2Interpreter
  ControlFlowPlugin.cpp
  ModuleInterpreter.cpp
  ModuleInterpreterRegistry.cpp
  OpHandlerRegistry.cpp
  Scheduler.cpp
  StateManager.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_MAIN_INCLUDE_DIR}/circt/Dialect/Cmt2/Interpreter

  LINK_COMPONENTS
  Support

  LINK_LIBS PUBLIC
  CIRCTCmt2
  CIRCTComb
  CIRCTFIRRTL
  CIRCTHW
  MLIRIR
  )

# ECMT2 Embedded DSL Library

add_circt_dialect_library(CIRCTECMT2
  Signal.cpp
  SignalHelpers.cpp
  Module.cpp
  FunctionLike.cpp
  Instance.cpp
  Interface.cpp
  Circuit.cpp
  HighLevel/Circuit.cpp
  HighLevel/Module.cpp
  HighLevel/Instance.cpp
  HighLevel/FunctionLike.cpp
  HighLevel/InterfaceWrapper.cpp
  HighLevel/InterfaceAPI.cpp
  STLLibrary.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_MAIN_INCLUDE_DIR}/circt/Dialect/Cmt2/ECMT2
  ${CIRCT_MAIN_INCLUDE_DIR}/circt/Dialect/Cmt2/ECMT2/HighLevel

  DEPENDS
  MLIRCmt2IncGen

  LINK_LIBS PUBLIC
  CIRCTCmt2
  CIRCTCmt2ModuleLib
  CIRCTCmt2Transforms
  CIRCTFIRRTL
  CIRCTSeq
  MLIRIR
  MLIRPass
  MLIRSupport
  MLIRParser
  )

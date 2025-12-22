add_circt_conversion_library(CIRCTCmt2ToFIRRTL
  Cmt2ToFIRRTL.cpp
  # Include analysis sources directly to break circular dependency with CIRCTCmt2Transforms
  ${CMAKE_CURRENT_SOURCE_DIR}/../../Dialect/Cmt2/Transforms/Scheduler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../Dialect/Cmt2/Transforms/ConflictMatrix.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../Dialect/Cmt2/Transforms/InstanceGraph.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../Dialect/Cmt2/Transforms/CallInfo.cpp

  DEPENDS
  CIRCTConversionPassIncGen
  CIRCTCmt2TransformsIncGen

  LINK_COMPONENTS
  Core

  LINK_LIBS PUBLIC
  CIRCTCmt2
  CIRCTComb
  CIRCTHW
  CIRCTFIRRTL
  CIRCTSupport
  MLIRIR
  MLIRPass
  MLIRTransformUtils
  )

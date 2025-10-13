add_circt_dialect_library(CIRCTCmt2Transforms
  # GenerateConflictMatrix.cpp
  # InferRules.cpp
  Test.cpp
  PrintInstanceGraph.cpp

  DEPENDS
  CIRCTCmt2TransformsIncGen

  LINK_LIBS PUBLIC
  CIRCTComb
  CIRCTHW
  CIRCTSupport
  CIRCTCmt2
  MLIRIR
  MLIRPass
  MLIRTransformUtils
)
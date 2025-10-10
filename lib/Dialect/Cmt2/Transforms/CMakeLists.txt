add_circt_dialect_library(CIRCTCmt2Transforms
  # GenerateConflictMatrix.cpp
  # InferRules.cpp

  DEPENDS
  CIRCTCmt2AttrsIncGen
  CIRCTCmt2TransformsIncGen
  CIRCTCmt2OpInterfacesIncGen
  CIRCTCmt2

  LINK_LIBS PUBLIC
  CIRCTComb
  CIRCTHW
  CIRCTSupport
  CIRCTCmt2
  MLIRIR
  MLIRPass
  MLIRTransformUtils
)
add_circt_conversion_library(CIRCTCmt2ToHW
  Cmt2ToHW.cpp

  ADDITIONAL_HEADER_DIRS
  ${MLIR_MAIN_INCLUDE_DIR}/mlir/Conversion/Cmt2ToHW

  DEPENDS
  CIRCTConversionPassIncGen

  LINK_COMPONENTS
  Core

  LINK_LIBS PUBLIC
  CIRCTCmt2
  CIRCTCmt2Transforms
  CIRCTSV
  CIRCTSeq
  CIRCTHW
  CIRCTComb
  MLIRIR
  MLIRPass
  MLIRFuncDialect
  MLIRSupport
  MLIRTransforms
  )

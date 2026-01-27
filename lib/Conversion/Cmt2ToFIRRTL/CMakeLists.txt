add_circt_conversion_library(CIRCTCmt2ToFIRRTL
  Cmt2ToFIRRTL.cpp

  DEPENDS
  CIRCTConversionPassIncGen

  LINK_COMPONENTS
  Core

  LINK_LIBS PUBLIC
  CIRCTCmt2
  CIRCTCmt2Analysis
  CIRCTComb
  CIRCTFIRRTL
  CIRCTSupport
  MLIRIR
  MLIRPass
  MLIRTransformUtils
  )

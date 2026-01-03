add_circt_library(CIRCTCmt2Analysis
  TimingAnalysis.cpp
  TimingCompatibility.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_MAIN_INCLUDE_DIR}/circt/Dialect/Cmt2/Analysis

  DEPENDS
  MLIRCmt2IncGen
  CIRCTCmt2AttrsIncGen

  LINK_COMPONENTS
  Support

  LINK_LIBS PUBLIC
  CIRCTCmt2
  MLIRIR
  )

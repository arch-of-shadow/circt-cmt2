add_circt_dialect_library(CIRCTCmt2
  Cmt2Dialect.cpp
  Cmt2Ops.cpp
  # Cmt2OpInterfaces.cpp
  # InstanceGraph.cpp
  # CallInfo.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_MAIN_INCLUDE_DIR}/circt/Dialect/Cmt2

  DEPENDS
  CIRCTHW
  MLIRCmt2IncGen
  # CIRCTCmt2AttrsIncGen
  # CIRCTCmt2OpInterfacesIncGen

  LINK_COMPONENTS
  Support

  LINK_LIBS PUBLIC
  CIRCTSupport
  CIRCTHW
  MLIRIR
  )
# add_subdirectory(Transforms)
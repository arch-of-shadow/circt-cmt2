add_circt_dialect_library(CIRCTCmt2
  Cmt2Dialect.cpp
  Cmt2Ops.cpp
  Cmt2InstanceGraph.cpp
  Cmt2CallInfo.cpp
  Cmt2ConflictMatrix.cpp
  Cmt2OpInterfaces.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_MAIN_INCLUDE_DIR}/circt/Dialect/Cmt2

  DEPENDS
  MLIRCmt2IncGen
  CIRCTCmt2OpInterfacesIncGen
  # CIRCTCmt2AttrsIncGen

  LINK_COMPONENTS
  Support

  LINK_LIBS PUBLIC
  CIRCTSupport
  MLIRIR
  CIRCTHW
  )
  
add_subdirectory(Transforms)
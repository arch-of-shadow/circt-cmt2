add_circt_dialect_library(CIRCTCmt2
  Cmt2Dialect.cpp
  Cmt2Ops.cpp
  Cmt2OpInterfaces.cpp
  Cmt2Attributes.cpp
  Cmt2Types.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_MAIN_INCLUDE_DIR}/circt/Dialect/Cmt2

  DEPENDS
  MLIRCmt2IncGen
  CIRCTCmt2OpInterfacesIncGen
  CIRCTCmt2AttrsIncGen

  LINK_COMPONENTS
  Support

  LINK_LIBS PUBLIC
  CIRCTSupport
  MLIRIR
  CIRCTHW
  )

add_subdirectory(Analysis)
add_subdirectory(Interpreter)
add_subdirectory(Transforms)
add_subdirectory(ECMT2)
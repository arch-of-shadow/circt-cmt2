# ECMT2 Embedded DSL Library

add_circt_dialect_library(CIRCTECMT2
  Signal.cpp
  SignalHelpers.cpp
  Module.cpp
  FunctionLike.cpp
  Instance.cpp
  Interface.cpp
  Circuit.cpp
  ModuleLibrary.cpp
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
  CIRCTCmt2Transforms
  CIRCTFIRRTL
  CIRCTSeq
  MLIRIR
  MLIRPass
  MLIRSupport
  MLIRParser
  )

# Copy module library to build directory
add_custom_target(cmt2-module-library
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/../ModuleLibrary
          ${CMAKE_BINARY_DIR}/lib/Dialect/Cmt2/ModuleLibrary
  COMMENT "Copying Cmt2 module library"
)

add_dependencies(CIRCTECMT2 cmt2-module-library)

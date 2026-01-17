# CMT2 Module Library - standalone library for ModuleLibrary
# This is separated from ECMT2 to avoid circular dependencies with Transforms

add_circt_dialect_library(CIRCTCmt2ModuleLib
  ModuleLibrary.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_MAIN_INCLUDE_DIR}/circt/Dialect/Cmt2/ECMT2

  DEPENDS
  MLIRCmt2IncGen

  LINK_LIBS PUBLIC
  CIRCTFIRRTL
  MLIRIR
  MLIRParser
  MLIRSupport
)

# Copy module library to build directory
add_custom_target(cmt2-module-library-data
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/../ModuleLibrary
          ${CMAKE_BINARY_DIR}/lib/Dialect/Cmt2/ModuleLibrary
  COMMENT "Copying Cmt2 module library data"
)

add_dependencies(CIRCTCmt2ModuleLib cmt2-module-library-data)

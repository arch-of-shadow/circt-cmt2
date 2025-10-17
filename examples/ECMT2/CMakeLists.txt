# ECMT2 Examples

add_executable(ecmt2-counter-example
  counter_example.cpp
)

target_link_libraries(ecmt2-counter-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-counter-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

add_executable(ecmt2-hello-example
  hello_example.cpp
)

target_link_libraries(ecmt2-hello-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-hello-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

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

add_executable(ecmt2-highlevel-counter-example
  highlevel_counter.cpp
)

target_link_libraries(ecmt2-highlevel-counter-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-highlevel-counter-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

add_executable(ecmt2-simple-highlevel-example
  simple_highlevel.cpp
)

target_link_libraries(ecmt2-simple-highlevel-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-simple-highlevel-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

add_executable(ecmt2-declarative-example
  declarative_example.cpp
)

target_link_libraries(ecmt2-declarative-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-declarative-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

add_executable(ecmt2-improved-counter-example
  improved_counter.cpp
)

target_link_libraries(ecmt2-improved-counter-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-improved-counter-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

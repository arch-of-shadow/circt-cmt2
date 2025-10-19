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

add_executable(ecmt2-counter-highlevel-example
  counter_highlevel.cpp
)

target_link_libraries(ecmt2-counter-highlevel-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-counter-highlevel-example
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

add_executable(ecmt2-hello-highlevel-example
  hello_highlevel.cpp
)

target_link_libraries(ecmt2-hello-highlevel-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-hello-highlevel-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

add_executable(ecmt2-declarative-instance-example
  declarative_instance_example.cpp
)

target_link_libraries(ecmt2-declarative-instance-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-declarative-instance-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)


add_executable(ecmt2-hello-v3-example
  hello_v3.cpp
)

target_link_libraries(ecmt2-hello-v3-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-hello-v3-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

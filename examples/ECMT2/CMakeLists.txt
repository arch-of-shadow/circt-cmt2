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

add_executable(ecmt2-bundle-vector-example
  bundle_vector_example.cpp
)

target_link_libraries(ecmt2-bundle-vector-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-bundle-vector-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

add_executable(ecmt2-bundle-vector-highlevel-example
  bundle_vector_highlevel_example.cpp
)

target_link_libraries(ecmt2-bundle-vector-highlevel-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-bundle-vector-highlevel-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

add_executable(ecmt2-two-counter-if-example
  two_counter_if_example.cpp
)

target_link_libraries(ecmt2-two-counter-if-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-two-counter-if-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

add_executable(ecmt2-two-counter-if-highlevel-example
  two_counter_if_highlevel.cpp
)

target_link_libraries(ecmt2-two-counter-if-highlevel-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-two-counter-if-highlevel-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

add_executable(ecmt2-signal-args-example
  signal_args_example.cpp
)

target_link_libraries(ecmt2-signal-args-example
  PRIVATE
  CIRCTECMT2
  CIRCTCmt2
  CIRCTFIRRTL
  MLIRIR
  MLIRSupport
)

target_include_directories(ecmt2-signal-args-example
  PRIVATE
  ${CIRCT_MAIN_INCLUDE_DIR}
)

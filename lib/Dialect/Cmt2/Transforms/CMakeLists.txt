add_circt_dialect_library(CIRCTCmt2Transforms
  # GenerateConflictMatrix.cpp
  # InferRules.cpp
  Test.cpp
  CompileInvoke.cpp
  TDCC.cpp
  ProcStmtToAction.cpp
  ProcToGAA.cpp
  InstanceGraph.cpp
  CallInfo.cpp
  ConflictMatrix.cpp
  PrintInstanceGraph.cpp
  PrintCallInfo.cpp
  PrintConflictMatrix.cpp
  ModuleInliner.cpp
  PrivateFuncAnalysis.cpp
  InlinePrivateFuncs.cpp
  Scheduler.cpp
  PrintScheduler.cpp
  VerifyPrivateFuncsInlined.cpp
  VerifyCallSequence.cpp
  Cmt2ToFIRRTLPipeline.cpp

  DEPENDS
  CIRCTCmt2TransformsIncGen

  LINK_LIBS PUBLIC
  CIRCTComb
  CIRCTHW
  CIRCTFIRRTL
  CIRCTSupport
  CIRCTCmt2
  CIRCTCmt2ToFIRRTL
  MLIRIR
  MLIRPass
  MLIRTransformUtils
)
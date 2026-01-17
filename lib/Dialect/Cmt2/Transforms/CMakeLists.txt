add_circt_dialect_library(CIRCTCmt2Transforms
  # GenerateConflictMatrix.cpp
  # InferRules.cpp
  Test.cpp
  CompileInvoke.cpp
  CompileStatic.cpp
  ControlCollapsing.cpp
  DataflowLowering.cpp
  TokenLowering.cpp
  TokenRTLGen.cpp
  ModuleGenerator.cpp
  StallControllerGen.cpp
  Diagnostics.cpp
  SimplifyStaticGuards.cpp
  StaticFSMAllocation.cpp
  StaticInference.cpp
  StaticPromotion.cpp
  TimingInference.cpp
  TimingValidation.cpp
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
  CIRCTCmt2ModuleLib
  CIRCTCmt2Analysis
  CIRCTCmt2ToFIRRTL
  MLIRIR
  MLIRPass
  MLIRTransformUtils
)
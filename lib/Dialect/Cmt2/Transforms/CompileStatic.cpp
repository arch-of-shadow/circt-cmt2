//===- CompileStatic.cpp - Compile static control to FSM -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CompileStatic pass for the Cmt2 dialect.
// It compiles static control structures to optimized FSM hardware by using
// the FSM allocation info from StaticFSMAllocation pass.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Attributes.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-compile-static"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_COMPILESTATIC
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// FSM Information Structures
//===----------------------------------------------------------------------===//

/// FSM configuration read from step attributes.
struct StepFSMConfig {
  int64_t numStates;
  int64_t bitwidth;
  bool isOneHot;
  ArrayAttr stateAssignments;
};

//===----------------------------------------------------------------------===//
// CompileStatic Pass Implementation
//===----------------------------------------------------------------------===//

/// Early-reset analysis result.
struct EarlyResetInfo {
  bool canEarlyReset = false;      // Whether early reset is possible
  int64_t earlyResetState = -1;    // State at which early reset is safe
  int64_t savedCycles = 0;         // Number of cycles saved by early reset
};

struct CompileStaticPass
    : public circt::cmt2::impl::CompileStaticBase<CompileStaticPass> {
  using CompileStaticBase::CompileStaticBase;

  void runOnOperation() override;

private:
  /// Process a single module.
  void processModule(cmt2::ModuleOp module);

  /// Process a static step with FSM allocation info.
  void processStaticStep(ProcStaticStepOp step, cmt2::ModuleOp module);

  /// Read FSM config from step attributes.
  std::optional<StepFSMConfig> readFSMConfig(ProcStaticStepOp step);

  /// Generate timing guard expression for a call.
  void annotateCallWithStateGuard(CallOp call, int64_t startState,
                                   int64_t endState, bool isOneHot);

  /// Generate FSM register info for a step.
  void annotateFSMRegisterInfo(ProcStaticStepOp step,
                                const StepFSMConfig &config,
                                const EarlyResetInfo &earlyReset);

  /// Analyze a step for early-reset opportunities.
  EarlyResetInfo analyzeEarlyReset(ProcStaticStepOp step,
                                    const StepFSMConfig &config);

  /// Find a suitable register module in the circuit.
  Operation *findRegisterModule(CircuitOp circuit, unsigned width);

  /// Transform static step into wrapper with internal FSM.
  /// This is the key transformation that merges static path into dynamic.
  void transformStaticStepToWrapper(ProcStaticStepOp step,
                                     cmt2::ModuleOp module,
                                     const StepFSMConfig &config,
                                     const EarlyResetInfo &earlyReset);

  /// Create FSM tick rule for a static step.
  void createFSMTickRule(OpBuilder &builder, Location loc,
                          StringRef stepName, StringRef fsmInstName,
                          unsigned fsmWidth, int64_t numStates, bool isOneHot);

  /// Create done value for a static step.
  void createDoneValue(OpBuilder &builder, Location loc,
                        StringRef stepName, StringRef fsmInstName,
                        unsigned fsmWidth, int64_t doneState, bool isOneHot);

  /// Create start rule that activates FSM when step is enabled.
  void createStartRule(OpBuilder &builder, Location loc,
                        StringRef stepName, StringRef fsmInstName,
                        ProcStaticStepOp step, unsigned fsmWidth, bool isOneHot);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// FSM Configuration Reading
//===----------------------------------------------------------------------===//

std::optional<StepFSMConfig>
CompileStaticPass::readFSMConfig(ProcStaticStepOp step) {
  // Check for FSM allocation attributes
  auto numStatesAttr = step->getAttrOfType<IntegerAttr>("fsm_states");
  if (!numStatesAttr)
    return std::nullopt;

  StepFSMConfig config;
  config.numStates = numStatesAttr.getInt();

  if (auto bitwidthAttr = step->getAttrOfType<IntegerAttr>("fsm_bitwidth"))
    config.bitwidth = bitwidthAttr.getInt();
  else
    config.bitwidth = config.numStates; // Default to one-hot

  if (auto encodingAttr = step->getAttrOfType<StringAttr>("fsm_encoding"))
    config.isOneHot = encodingAttr.getValue() == "one_hot";
  else
    config.isOneHot = true;

  config.stateAssignments =
      step->getAttrOfType<ArrayAttr>("state_assignments");

  return config;
}

//===----------------------------------------------------------------------===//
// Call Annotation with State Guards
//===----------------------------------------------------------------------===//

void CompileStaticPass::annotateCallWithStateGuard(CallOp call,
                                                    int64_t startState,
                                                    int64_t endState,
                                                    bool isOneHot) {
  OpBuilder builder(call->getContext());

  // Add state guard attributes to the call
  // These will be used by subsequent passes to generate actual guard logic
  call->setAttr("fsm_start_state", builder.getI64IntegerAttr(startState));
  call->setAttr("fsm_end_state", builder.getI64IntegerAttr(endState));
  call->setAttr("fsm_is_one_hot", builder.getBoolAttr(isOneHot));

  // Generate guard expression description
  // For binary encoding: fsm >= start && fsm < end
  // For one-hot encoding: |fsm[end-1:start]
  std::string guardExpr;
  if (isOneHot) {
    if (endState - startState == 1) {
      guardExpr = "fsm[" + std::to_string(startState) + "]";
    } else {
      guardExpr = "|fsm[" + std::to_string(endState - 1) + ":" +
                  std::to_string(startState) + "]";
    }
  } else {
    if (endState - startState == 1) {
      guardExpr = "fsm == " + std::to_string(startState);
    } else {
      guardExpr = "fsm >= " + std::to_string(startState) + " && fsm < " +
                  std::to_string(endState);
    }
  }
  call->setAttr("fsm_guard_expr", builder.getStringAttr(guardExpr));

  LLVM_DEBUG(llvm::dbgs() << "    Call guard: " << guardExpr << "\n");
}

//===----------------------------------------------------------------------===//
// Early-Reset Analysis
//===----------------------------------------------------------------------===//

EarlyResetInfo
CompileStaticPass::analyzeEarlyReset(ProcStaticStepOp step,
                                      const StepFSMConfig &config) {
  EarlyResetInfo result;

  // Find the latest end state among all calls
  // This is the earliest state where all operations have completed
  int64_t maxEndState = 0;

  step.getBody().walk([&](CallOp call) {
    if (auto resultTiming = call.getResultTiming()) {
      for (auto attr : *resultTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          maxEndState = std::max(maxEndState, timing.getEnd());
        }
      }
    } else if (auto argTiming = call.getArgTiming()) {
      // If no result timing, use arg timing + 1 as end
      for (auto attr : *argTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          maxEndState = std::max(maxEndState, timing.getEnd());
        }
      }
    }
  });

  // Check if we can reset earlier than the declared latency
  // Early reset is possible if all operations complete before the last state
  if (maxEndState > 0 && maxEndState < config.numStates) {
    result.canEarlyReset = true;
    result.earlyResetState = maxEndState;
    result.savedCycles = config.numStates - maxEndState;

    LLVM_DEBUG(llvm::dbgs() << "    Early reset analysis: can reset at state "
                            << maxEndState << " (saving " << result.savedCycles
                            << " cycles)\n");
  }

  return result;
}

//===----------------------------------------------------------------------===//
// FSM Register Info Annotation
//===----------------------------------------------------------------------===//

void CompileStaticPass::annotateFSMRegisterInfo(ProcStaticStepOp step,
                                                 const StepFSMConfig &config,
                                                 const EarlyResetInfo &earlyReset) {
  OpBuilder builder(step->getContext());

  // Generate FSM register name
  std::string fsmRegName = ("__fsm_" + step.getSymName()).str();
  step->setAttr("fsm_reg_name", builder.getStringAttr(fsmRegName));

  // Generate done signal expression
  std::string doneExpr;
  if (config.isOneHot) {
    // For one-hot, done when MSB is set (last state)
    doneExpr = "fsm[" + std::to_string(config.numStates - 1) + "]";
  } else {
    // For binary, done when fsm == numStates - 1
    doneExpr = "fsm == " + std::to_string(config.numStates - 1);
  }
  step->setAttr("fsm_done_expr", builder.getStringAttr(doneExpr));

  // Generate next-state expression
  std::string nextExpr;
  if (config.isOneHot) {
    // For one-hot, shift left by 1
    nextExpr = "{fsm[" + std::to_string(config.numStates - 2) + ":0], 1'b0}";
  } else {
    // For binary, increment by 1
    nextExpr = "fsm + 1";
  }
  step->setAttr("fsm_next_expr", builder.getStringAttr(nextExpr));

  // Generate initial value
  std::string initExpr;
  if (config.isOneHot) {
    initExpr = std::to_string(config.numStates) + "'b1";
  } else {
    initExpr = std::to_string(config.bitwidth) + "'d0";
  }
  step->setAttr("fsm_init_expr", builder.getStringAttr(initExpr));

  // Add early-reset info if applicable
  if (earlyReset.canEarlyReset) {
    step->setAttr("fsm_early_reset", builder.getUnitAttr());
    step->setAttr("fsm_early_reset_state",
                  builder.getI64IntegerAttr(earlyReset.earlyResetState));
    step->setAttr("fsm_saved_cycles",
                  builder.getI64IntegerAttr(earlyReset.savedCycles));

    // Generate early-reset done expression (can signal done earlier)
    std::string earlyDoneExpr;
    if (config.isOneHot) {
      earlyDoneExpr = "fsm[" + std::to_string(earlyReset.earlyResetState - 1) + "]";
    } else {
      earlyDoneExpr = "fsm == " + std::to_string(earlyReset.earlyResetState - 1);
    }
    step->setAttr("fsm_early_done_expr", builder.getStringAttr(earlyDoneExpr));
  }

  // Mark as compiled
  step->setAttr("static_compiled", builder.getUnitAttr());

  LLVM_DEBUG({
    llvm::dbgs() << "  FSM register: " << fsmRegName << "\n";
    llvm::dbgs() << "    done: " << doneExpr << "\n";
    llvm::dbgs() << "    next: " << nextExpr << "\n";
    llvm::dbgs() << "    init: " << initExpr << "\n";
    if (earlyReset.canEarlyReset) {
      llvm::dbgs() << "    early_reset_state: " << earlyReset.earlyResetState
                   << " (saves " << earlyReset.savedCycles << " cycles)\n";
    }
  });
}

//===----------------------------------------------------------------------===//
// Register Module Finding
//===----------------------------------------------------------------------===//

Operation *CompileStaticPass::findRegisterModule(CircuitOp circuit,
                                                  unsigned width) {
  MLIRContext *ctx = circuit.getContext();
  Location loc = circuit.getLoc();

  // Prefer a dedicated FSM register module, using the same naming convention as
  // ProcStmtToAction and the ModuleLibrary.
  std::string symName = "__FSMReg_" + std::to_string(width);

  // 1) If we already created the desired width, reuse it.
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(op)) {
      if (extMod.getSymName() == symName)
        return extMod;
    }
  }

  // 2) Otherwise, look for any existing extern register module with matching
  // read/write widths.
  for (auto &op : circuit.getBodyRegion().front()) {
    auto extMod = dyn_cast<ExtModuleFirrtlOp>(op);
    if (!extMod || extMod.getBody().empty())
      continue;

    bool hasRead = false, hasWrite = false;
    unsigned readWidth = 0, writeWidth = 0;

    for (auto &bodyOp : extMod.getBody().front()) {
      if (auto bindValue = dyn_cast<BindValueOp>(bodyOp)) {
        if (bindValue.getSymName() == "read") {
          hasRead = true;
          auto returnTypes = bindValue.getResultTypes();
          if (returnTypes.size() == 1) {
            if (auto uintType = dyn_cast<firrtl::UIntType>(returnTypes[0])) {
              if (uintType.getWidth().has_value())
                readWidth = uintType.getWidth().value();
            }
          }
        }
      } else if (auto bindMethod = dyn_cast<BindMethodOp>(bodyOp)) {
        if (bindMethod.getSymName() == "write") {
          hasWrite = true;
          auto argTypes = bindMethod.getArgumentTypes();
          if (argTypes.size() == 1) {
            if (auto uintType = dyn_cast<firrtl::UIntType>(argTypes[0])) {
              if (uintType.getWidth().has_value())
                writeWidth = uintType.getWidth().value();
            }
          }
        }
      }
    }

    if (hasRead && hasWrite && readWidth == width && writeWidth == width)
      return extMod;
  }

  // 3) Create a new FSM register module with the requested width.
  OpBuilder builder(ctx);
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(&circuit.getBodyRegion().front());

  std::string firrtlName = "Reg_width" + std::to_string(width) + "_init0";
  SmallVector<Attribute> argNameAttrs = {
      builder.getStringAttr("clk"),
      builder.getStringAttr("rst")};

  auto extMod = builder.create<ExtModuleFirrtlOp>(
      loc,
      builder.getStringAttr(symName),
      FlatSymbolRefAttr::get(ctx, firrtlName),
      builder.getArrayAttr(argNameAttrs));

  auto clockType = firrtl::ClockType::get(ctx);
  auto resetType = firrtl::UIntType::get(ctx, 1);
  auto dataType = firrtl::UIntType::get(ctx, width);

  Block *body = new Block();
  body->addArguments({clockType, resetType}, {loc, loc});
  extMod.getBody().push_back(body);

  OpBuilder bodyBuilder(body, body->begin());

  bodyBuilder.create<BindBareOp>(
      loc, body->getArgument(0), FlatSymbolRefAttr::get(ctx, "clk"));
  bodyBuilder.create<BindBareOp>(
      loc, body->getArgument(1), FlatSymbolRefAttr::get(ctx, "rst"));

  auto readFuncType = builder.getFunctionType({}, {dataType});
  bodyBuilder.create<BindValueOp>(
      loc,
      builder.getStringAttr("read"),
      TypeAttr::get(readFuncType),
      builder.getStringAttr("read_ready"),
      builder.getArrayAttr({}),
      builder.getArrayAttr({builder.getStringAttr("read_data")}),
      ArrayAttr(),
      ArrayAttr());

  auto writeFuncType = builder.getFunctionType({dataType}, {});
  bodyBuilder.create<BindMethodOp>(
      loc,
      builder.getStringAttr("write"),
      TypeAttr::get(writeFuncType),
      builder.getStringAttr("write_enable"),
      builder.getStringAttr("write_ready"),
      builder.getArrayAttr({builder.getStringAttr("write_data")}),
      builder.getArrayAttr({}),
      ArrayAttr(),
      ArrayAttr());

  // read must sequence before write for this register module.
  SmallVector<Attribute> seqPair = {
      FlatSymbolRefAttr::get(ctx, "read"),
      FlatSymbolRefAttr::get(ctx, "write")};
  extMod->setAttr("sequenceBefore",
                  builder.getArrayAttr({builder.getArrayAttr(seqPair)}));

  return extMod;
}

//===----------------------------------------------------------------------===//
// FSM Tick Rule Creation
//===----------------------------------------------------------------------===//

void CompileStaticPass::createFSMTickRule(OpBuilder &builder, Location loc,
                                           StringRef stepName,
                                           StringRef fsmInstName,
                                           unsigned fsmWidth, int64_t numStates,
                                           bool isOneHot) {
  auto fsmType = firrtl::UIntType::get(builder.getContext(), fsmWidth);

  // Create rule: @{stepName}__tick
  std::string tickRuleName = (stepName + "__tick").str();
  auto funcType = builder.getFunctionType({}, {});
  auto funcTypeAttr = TypeAttr::get(funcType);

  auto tickRule = builder.create<RuleOp>(
      loc, builder.getStringAttr(tickRuleName), funcTypeAttr,
      builder.getArrayAttr({}), builder.getArrayAttr({}),
      ArrayAttr(), ArrayAttr());

  // Build guard region: fsm != 0 && fsm < numStates (running but not done)
  Block *guardBlock = new Block();
  tickRule.getGuard().push_back(guardBlock);
  OpBuilder guardBuilder(guardBlock, guardBlock->begin());

  // Read FSM state
  auto instanceSym = FlatSymbolRefAttr::get(builder.getContext(), fsmInstName);
  auto readSym = FlatSymbolRefAttr::get(builder.getContext(), "read");
  auto fsmReadCall = guardBuilder.create<CallOp>(
      loc, SmallVector<Type>{fsmType}, ValueRange{},
      instanceSym, readSym,
      ArrayAttr(), ArrayAttr());

  // Guard: fsm > 0 && fsm < numStates (in progress, not idle or done)
  auto zeroConst = guardBuilder.create<firrtl::ConstantOp>(
      loc, fsmType, llvm::APInt(fsmWidth, 0));
  auto notIdle = guardBuilder.create<firrtl::GTPrimOp>(
      loc, fsmReadCall.getResult(0), zeroConst.getResult());

  // For simplicity, we check that FSM hasn't reached max state
  // In the final cycle, we don't tick anymore
  auto maxState = guardBuilder.create<firrtl::ConstantOp>(
      loc, fsmType, llvm::APInt(fsmWidth, numStates));
  auto notDone = guardBuilder.create<firrtl::LTPrimOp>(
      loc, fsmReadCall.getResult(0), maxState.getResult());

  auto running = guardBuilder.create<firrtl::AndPrimOp>(
      loc, notIdle.getResult(), notDone.getResult());

  guardBuilder.create<ReturnOp>(loc, ValueRange{running.getResult()});

  // Build body region: fsm <= fsm + 1
  Block *bodyBlock = new Block();
  tickRule.getBody().push_back(bodyBlock);
  OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

  // Read current state
  auto fsmReadCall2 = bodyBuilder.create<CallOp>(
      loc, SmallVector<Type>{fsmType}, ValueRange{},
      instanceSym, readSym,
      ArrayAttr(), ArrayAttr());

  // Compute next state
  Value nextState;
  if (isOneHot) {
    // One-hot: shift left by 1, but keep the FSM register width constant.
    //
    // NOTE: `firrtl.dshl` widens the result (width = a.width + b.maxValue),
    // so we truncate back down to `fsmWidth` to match the register type.
    auto shiftAmt = bodyBuilder.create<firrtl::ConstantOp>(
        loc, firrtl::UIntType::get(builder.getContext(), 1),
        llvm::APInt(1, 1));
    auto shifted = bodyBuilder.create<firrtl::DShlPrimOp>(
        loc, fsmReadCall2.getResult(0), shiftAmt.getResult()).getResult();
    nextState = bodyBuilder
                    .create<firrtl::BitsPrimOp>(loc, shifted, fsmWidth - 1, 0)
                    .getResult();
  } else {
    // Binary: increment by 1
    auto oneConst = bodyBuilder.create<firrtl::ConstantOp>(
        loc, fsmType, llvm::APInt(fsmWidth, 1));
    nextState = bodyBuilder.create<firrtl::AddPrimOp>(
        loc, fsmReadCall2.getResult(0), oneConst.getResult()).getResult();
  }

  // Write next state
  auto writeSym = FlatSymbolRefAttr::get(builder.getContext(), "write");
  bodyBuilder.create<CallOp>(
      loc, SmallVector<Type>{}, ValueRange{nextState},
      instanceSym, writeSym,
      ArrayAttr(), ArrayAttr());

  bodyBuilder.create<ReturnOp>(loc);

  LLVM_DEBUG(llvm::dbgs() << "  Created FSM tick rule @" << tickRuleName << "\n");
}

//===----------------------------------------------------------------------===//
// Done Value Creation
//===----------------------------------------------------------------------===//

void CompileStaticPass::createDoneValue(OpBuilder &builder, Location loc,
                                         StringRef stepName,
                                         StringRef fsmInstName,
                                         unsigned fsmWidth, int64_t doneState,
                                         bool isOneHot) {
  auto fsmType = firrtl::UIntType::get(builder.getContext(), fsmWidth);
  auto boolType = firrtl::UIntType::get(builder.getContext(), 1);

  // Create value: @{stepName}__done
  std::string doneValueName = (stepName + "__done").str();
  auto funcType = builder.getFunctionType({}, {boolType});
  auto funcTypeAttr = TypeAttr::get(funcType);

  auto doneValue = builder.create<ValueOp>(
      loc, builder.getStringAttr(doneValueName), funcTypeAttr,
      builder.getArrayAttr({}), builder.getArrayAttr({}),
      ArrayAttr(), ArrayAttr());

  // Guard: always true
  Block *guardBlock = new Block();
  doneValue.getGuard().push_back(guardBlock);
  OpBuilder guardBuilder(guardBlock, guardBlock->begin());
  auto trueConst = guardBuilder.create<firrtl::ConstantOp>(
      loc, boolType, llvm::APInt(1, 1));
  guardBuilder.create<ReturnOp>(loc, ValueRange{trueConst.getResult()});

  // Body: return fsm >= doneState (step has completed its cycles)
  Block *bodyBlock = new Block();
  doneValue.getBody().push_back(bodyBlock);
  OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

  auto instanceSym = FlatSymbolRefAttr::get(builder.getContext(), fsmInstName);
  auto readSym = FlatSymbolRefAttr::get(builder.getContext(), "read");
  auto fsmReadCall = bodyBuilder.create<CallOp>(
      loc, SmallVector<Type>{fsmType}, ValueRange{},
      instanceSym, readSym,
      ArrayAttr(), ArrayAttr());

  // done = fsm >= doneState
  auto doneStateConst = bodyBuilder.create<firrtl::ConstantOp>(
      loc, fsmType, llvm::APInt(fsmWidth, doneState));
  auto isDone = bodyBuilder.create<firrtl::GEQPrimOp>(
      loc, fsmReadCall.getResult(0), doneStateConst.getResult());

  bodyBuilder.create<ReturnOp>(loc, ValueRange{isDone.getResult()});

  LLVM_DEBUG(llvm::dbgs() << "  Created done value @" << doneValueName
                          << " (done at state " << doneState << ")\n");
}

//===----------------------------------------------------------------------===//
// Start Rule Creation
//===----------------------------------------------------------------------===//

void CompileStaticPass::createStartRule(OpBuilder &builder, Location loc,
                                         StringRef stepName,
                                         StringRef fsmInstName,
                                         ProcStaticStepOp step,
                                         unsigned fsmWidth, bool isOneHot) {
  (void)step; // Used for future expansion
  auto fsmType = firrtl::UIntType::get(builder.getContext(), fsmWidth);

  // Create rule: @{stepName}__start
  std::string startRuleName = (stepName + "__start").str();
  auto funcType = builder.getFunctionType({}, {});
  auto funcTypeAttr = TypeAttr::get(funcType);

  auto startRule = builder.create<RuleOp>(
      loc, builder.getStringAttr(startRuleName), funcTypeAttr,
      builder.getArrayAttr({}), builder.getArrayAttr({}),
      ArrayAttr(), ArrayAttr());

  // Guard: FSM is idle (fsm == 0) AND step is enabled
  // Note: The actual enable signal comes from the enclosing control flow
  // For now, we check that FSM is idle (TDCC will handle the enable)
  Block *guardBlock = new Block();
  startRule.getGuard().push_back(guardBlock);
  OpBuilder guardBuilder(guardBlock, guardBlock->begin());

  auto instanceSym = FlatSymbolRefAttr::get(builder.getContext(), fsmInstName);
  auto readSym = FlatSymbolRefAttr::get(builder.getContext(), "read");
  auto fsmReadCall = guardBuilder.create<CallOp>(
      loc, SmallVector<Type>{fsmType}, ValueRange{},
      instanceSym, readSym,
      ArrayAttr(), ArrayAttr());

  auto zeroConst = guardBuilder.create<firrtl::ConstantOp>(
      loc, fsmType, llvm::APInt(fsmWidth, 0));
  auto isIdle = guardBuilder.create<firrtl::EQPrimOp>(
      loc, fsmReadCall.getResult(0), zeroConst.getResult());

  guardBuilder.create<ReturnOp>(loc, ValueRange{isIdle.getResult()});

  // Body: Set FSM to 1 (start state)
  Block *bodyBlock = new Block();
  startRule.getBody().push_back(bodyBlock);
  OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

  Value initState;
  if (isOneHot) {
    // One-hot: start with bit 1 set
    initState = bodyBuilder.create<firrtl::ConstantOp>(
        loc, fsmType, llvm::APInt(fsmWidth, 1)).getResult();
  } else {
    // Binary: start at state 1
    initState = bodyBuilder.create<firrtl::ConstantOp>(
        loc, fsmType, llvm::APInt(fsmWidth, 1)).getResult();
  }

  auto writeSym = FlatSymbolRefAttr::get(builder.getContext(), "write");
  bodyBuilder.create<CallOp>(
      loc, SmallVector<Type>{}, ValueRange{initState},
      instanceSym, writeSym,
      ArrayAttr(), ArrayAttr());

  bodyBuilder.create<ReturnOp>(loc);

  LLVM_DEBUG(llvm::dbgs() << "  Created start rule @" << startRuleName << "\n");
}

//===----------------------------------------------------------------------===//
// Static Step Transformation
//===----------------------------------------------------------------------===//

void CompileStaticPass::transformStaticStepToWrapper(
    ProcStaticStepOp step, cmt2::ModuleOp module, const StepFSMConfig &config,
    const EarlyResetInfo &earlyReset) {

  Location loc = step.getLoc();
  StringRef stepName = step.getSymName();
  auto circuit = module->getParentOfType<CircuitOp>();

  // Find a suitable register module
  auto regMod = findRegisterModule(circuit, config.bitwidth);
  if (!regMod) {
    LLVM_DEBUG(llvm::dbgs() << "  Warning: No register module found, "
                            << "skipping wrapper transformation\n");
    return;
  }

  // Get clock and reset from module arguments
  Value clock, reset;
  for (auto arg : module.getBody().getArguments()) {
    if (isa<firrtl::ClockType>(arg.getType())) {
      clock = arg;
    } else if (auto uintType = dyn_cast<firrtl::UIntType>(arg.getType())) {
      if (uintType.getWidth() == 1 && !reset) {
        reset = arg;
      }
    }
  }

  if (!clock || !reset) {
    LLVM_DEBUG(llvm::dbgs() << "  Warning: Could not find clock/reset, "
                            << "skipping wrapper transformation\n");
    return;
  }

  OpBuilder builder(step);
  builder.setInsertionPointAfter(step);

  // Create FSM instance
  std::string fsmInstName = ("__fsm_" + stepName).str();
  StringRef regModName;
  if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(regMod)) {
    regModName = extMod.getSymName();
  }

  builder.create<InstanceOp>(
      loc, builder.getStringAttr(fsmInstName),
      ValueRange{clock, reset},
      FlatSymbolRefAttr::get(builder.getContext(), regModName),
      ArrayAttr());

  LLVM_DEBUG(llvm::dbgs() << "  Created FSM instance @" << fsmInstName << "\n");

  // Determine done state (either early reset or full latency)
  int64_t doneState = config.numStates;
  if (earlyReset.canEarlyReset) {
    doneState = earlyReset.earlyResetState;
    LLVM_DEBUG(llvm::dbgs() << "  Using early reset: done at state "
                            << doneState << "\n");
  }

  // Create FSM tick rule (increments FSM each cycle while running)
  createFSMTickRule(builder, loc, stepName, fsmInstName, config.bitwidth,
                    config.numStates, config.isOneHot);

  // Create done value (signals when step is complete)
  createDoneValue(builder, loc, stepName, fsmInstName, config.bitwidth,
                   doneState, config.isOneHot);

  // Create start rule (activates FSM when step is enabled from idle)
  createStartRule(builder, loc, stepName, fsmInstName, step, config.bitwidth,
                   config.isOneHot);

  // Mark step as having wrapper generated
  step->setAttr("wrapper_generated", builder.getUnitAttr());
  step->setAttr("wrapper_fsm_instance", builder.getStringAttr(fsmInstName));
  step->setAttr("wrapper_done_state", builder.getI64IntegerAttr(doneState));

  LLVM_DEBUG(llvm::dbgs() << "  Wrapper transformation complete for @"
                          << stepName << "\n");
}

//===----------------------------------------------------------------------===//
// Static Step Processing
//===----------------------------------------------------------------------===//

void CompileStaticPass::processStaticStep(ProcStaticStepOp step,
                                           cmt2::ModuleOp module) {
  auto config = readFSMConfig(step);
  if (!config) {
    // No FSM allocation from StaticFSMAllocation - use latency directly
    int64_t latency = step.getLatency();
    LLVM_DEBUG(llvm::dbgs() << "  Processing @" << step.getSymName()
                            << " (latency=" << latency << ", no prior FSM alloc)\n");

    // Create config from latency
    StepFSMConfig defaultConfig;
    defaultConfig.numStates = latency;
    // Use binary encoding for > 8 states, one-hot otherwise
    if (latency <= static_cast<int64_t>(oneHotThreshold)) {
      defaultConfig.bitwidth = latency;
      defaultConfig.isOneHot = true;
    } else {
      defaultConfig.bitwidth = llvm::Log2_64_Ceil(latency + 1);
      defaultConfig.isOneHot = false;
    }

    // Analyze early-reset
    EarlyResetInfo earlyReset;

    // Annotate FSM register info
    annotateFSMRegisterInfo(step, defaultConfig, earlyReset);

    // Transform to wrapper with internal FSM
    transformStaticStepToWrapper(step, module, defaultConfig, earlyReset);

    // Mark as compiled
    step->setAttr("static_compiled", OpBuilder(step).getUnitAttr());
    return;
  }

  LLVM_DEBUG(llvm::dbgs() << "  Processing @" << step.getSymName()
                          << " (" << config->numStates << " states, "
                          << (config->isOneHot ? "one-hot" : "binary") << ")\n");

  // Analyze early-reset opportunities
  auto earlyReset = analyzeEarlyReset(step, *config);

  // Annotate FSM register info
  annotateFSMRegisterInfo(step, *config, earlyReset);

  // Walk calls and annotate with state guards
  int64_t callIdx = 0;
  step.getBody().walk([&](CallOp call) {
    // Get timing info from the call
    int64_t startState = 0;
    int64_t endState = 1;

    // Try to get start state from arg_timing
    if (auto argTiming = call.getArgTiming()) {
      int64_t minStart = INT64_MAX;
      for (auto attr : *argTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          minStart = std::min(minStart, timing.getStart());
        }
      }
      if (minStart != INT64_MAX)
        startState = minStart;
    }

    // Try to get end state from result_timing
    if (auto resultTiming = call.getResultTiming()) {
      int64_t maxEnd = 0;
      for (auto attr : *resultTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          maxEnd = std::max(maxEnd, timing.getEnd());
        }
      }
      endState = maxEnd;
    } else {
      endState = startState + 1;
    }

    annotateCallWithStateGuard(call, startState, endState, config->isOneHot);
    // After we've consumed call-site timing to derive FSM guards, drop the
    // timing attributes. Subsequent procedural lowering clones calls out of
    // the `cmt2.proc.static_step` body; keeping arg_timing/result_timing would
    // make those cloned calls illegal (CallOp verifier requires timing attrs
    // to appear only inside ProcStaticStepOp).
    call->removeAttr("arg_timing");
    call->removeAttr("result_timing");
    ++callIdx;
  });

  // Transform to wrapper with internal FSM
  transformStaticStepToWrapper(step, module, *config, earlyReset);
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void CompileStaticPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // Walk all static steps
  module.walk([&](ProcStaticStepOp step) {
    processStaticStep(step, module);
  });
}

//===----------------------------------------------------------------------===//
// Pass Entry Point
//===----------------------------------------------------------------------===//

void CompileStaticPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  LLVM_DEBUG(llvm::dbgs() << "=== CompileStatic Pass ===\n");
  LLVM_DEBUG(llvm::dbgs() << "Options: one-hot-threshold=" << oneHotThreshold
                          << "\n");

  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }
}

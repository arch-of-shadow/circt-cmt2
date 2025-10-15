//===- Cmt2ToHW.cpp - Translate Cmt2 into HW ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the main Cmt2 to HW Conversion Pass Implementation.
//
//===----------------------------------------------------------------------===//

#include "circt/Conversion/Cmt2ToHW.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Transforms/CallInfo.h"
#include "circt/Dialect/Cmt2/Transforms/ConflictMatrix.h"
#include "circt/Dialect/Cmt2/Transforms/Scheduler.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/HW/HWTypes.h"
#include "circt/Dialect/SV/SVDialect.h"
#include "circt/Dialect/SV/SVOps.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "mlir/IR/ImplicitLocOpBuilder.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-to-hw"

namespace circt {
#define GEN_PASS_DEF_CMT2TOHW
#include "circt/Conversion/Passes.h.inc"
} // namespace circt

using namespace mlir;
using namespace circt;
using namespace circt::cmt2;
using namespace circt::comb;
using namespace circt::hw;
using namespace circt::seq;
using namespace circt::sv;

//===----------------------------------------------------------------------===//
// Conversion Infrastructure
//===----------------------------------------------------------------------===//

namespace {

/// Helper class for tracking signal names and values during conversion
class SignalTracker {
public:
  SignalTracker() = default;

  /// Register a ready signal for a function
  void registerReadySignal(StringAttr funcName, Value readySignal) {
    readySignals[funcName] = readySignal;
  }

  /// Register an enable signal for a method
  void registerEnableSignal(StringAttr funcName, Value enableSignal) {
    enableSignals[funcName] = enableSignal;
  }

  /// Register a fire signal for a function
  void registerFireSignal(StringAttr funcName, Value fireSignal) {
    fireSignals[funcName] = fireSignal;
  }

  /// Register body results for a method/value
  void registerBodyResults(StringAttr funcName, ArrayRef<Value> results) {
    bodyResults[funcName] = SmallVector<Value>(results.begin(), results.end());
  }

  /// Get ready signal for a function
  Value getReadySignal(StringAttr funcName) const {
    auto it = readySignals.find(funcName);
    return it != readySignals.end() ? it->second : Value();
  }

  /// Get enable signal for a method
  Value getEnableSignal(StringAttr funcName) const {
    auto it = enableSignals.find(funcName);
    return it != enableSignals.end() ? it->second : Value();
  }

  /// Get fire signal for a function
  Value getFireSignal(StringAttr funcName) const {
    auto it = fireSignals.find(funcName);
    return it != fireSignals.end() ? it->second : Value();
  }

  /// Get body results for a method/value
  ArrayRef<Value> getBodyResults(StringAttr funcName) const {
    auto it = bodyResults.find(funcName);
    return it != bodyResults.end() ? ArrayRef<Value>(it->second) : ArrayRef<Value>();
  }

private:
  DenseMap<StringAttr, Value> readySignals;
  DenseMap<StringAttr, Value> enableSignals;
  DenseMap<StringAttr, Value> fireSignals;
  DenseMap<StringAttr, SmallVector<Value>> bodyResults;
};

/// Context for converting a single Cmt2 module
struct ModuleConversionContext {
  cmt2::ModuleOp cmt2Module;
  hw::HWModuleOp hwModule;
  OpBuilder &builder;
  SignalTracker &signalTracker;
  const cmt2::SchedulerAnalysis &schedulerAnalysis;
  const cmt2::ConflictMatrixAnalysis &conflictAnalysis;
  const cmt2::CallInfoView &callInfo;
  IRMapping &globalMapping; // Maps cmt2 module arguments to hw module arguments

  ModuleConversionContext(cmt2::ModuleOp cmt2Mod, hw::HWModuleOp hwMod,
                          OpBuilder &b, SignalTracker &tracker,
                          const cmt2::SchedulerAnalysis &sched,
                          const cmt2::ConflictMatrixAnalysis &conflict,
                          const cmt2::CallInfoView &callInfoView,
                          IRMapping &mapping)
      : cmt2Module(cmt2Mod), hwModule(hwMod), builder(b),
        signalTracker(tracker), schedulerAnalysis(sched),
        conflictAnalysis(conflict), callInfo(callInfoView), globalMapping(mapping) {}

  /// Get the name for a ready signal
  std::string getReadySignalName(cmt2::Cmt2FunctionLike func) {
    if (auto readyName = func.getReadyName(); !readyName.empty())
      return readyName.str();
    return (func.functionName() + "_ready").str();
  }

  /// Get the name for an enable signal
  std::string getEnableSignalName(cmt2::Cmt2FunctionLike func) {
    if (auto enableName = func.getEnableName(); !enableName.empty())
      return enableName.str();
    return (func.functionName() + "_enable").str();
  }

  /// Get the name for a fire signal
  std::string getFireSignalName(cmt2::Cmt2FunctionLike func) {
    return (func.functionName() + "_fire").str();
  }
};

//===----------------------------------------------------------------------===//
// Function Conversion Logic
//===----------------------------------------------------------------------===//

/// Validate call sequence in a function for conflicts
static LogicalResult
validateCallSequence(cmt2::Cmt2FunctionLike func,
                     ModuleConversionContext &ctx) {
  LLVM_DEBUG(llvm::dbgs() << "  Validating call sequence for @"
                          << func.functionName() << "\n");

  // Collect all calls from guard and body regions by walking operations
  SmallVector<cmt2::CallOp> calls;

  func.walk([&](cmt2::CallOp callOp) {
    calls.push_back(callOp);
  });

  if (calls.empty())
    return success(); // No calls to validate

  // Get the conflict matrix for validation
  auto *moduleMatrix = ctx.conflictAnalysis.getModuleMatrix(ctx.cmt2Module.getSymNameAttr());
  if (!moduleMatrix)
    return success(); // No conflict matrix available

  // Validate that the call sequence respects conflict and sequence relationships
  // Check each pair of calls in order
  for (size_t i = 0; i < calls.size(); ++i) {
    for (size_t j = i + 1; j < calls.size(); ++j) {
      auto call1 = calls[i];
      auto call2 = calls[j];

      // Only validate calls to @this (same module)
      auto callee1 = call1.getCallee().getRootReference();
      auto callee2 = call2.getCallee().getRootReference();

      if (callee1.getValue() != "this" || callee2.getValue() != "this")
        continue; // Skip external calls

      // Get the entity names being called
      auto entity1Name = call1.getMethodOrValue().getRootReference();
      auto entity2Name = call2.getMethodOrValue().getRootReference();

      // Check if there's a conflict or sequence violation
      auto rel = moduleMatrix->getRelationship(entity1Name, entity2Name);

      // If call1 should come after call2 due to sequencing, that's an error
      if (rel == Relationship::SequentialBefore) {
        // entity1 < entity2 means entity1 should execute before entity2
        // This is OK since call1 comes before call2 in the sequence
        continue;
      }

      // Check the reverse relationship
      auto relReverse = moduleMatrix->getRelationship(entity2Name, entity1Name);
      if (relReverse == Relationship::SequentialBefore) {
        // entity2 < entity1 but call1 comes before call2
        return func.emitError("call sequence violation: @")
               << entity1Name << " is called before @" << entity2Name
               << " but @" << entity2Name << " must execute before @"
               << entity1Name;
      }
    }
  }

  return success();
}

/// Helper to create a constant i1 value
static Value createI1Constant(OpBuilder &builder, Location loc, bool value) {
  return builder.create<hw::ConstantOp>(loc, APInt(1, value ? 1 : 0));
}

/// Helper to create AND of multiple values
static Value createAndChain(OpBuilder &builder, Location loc,
                            ArrayRef<Value> values) {
  if (values.empty())
    return createI1Constant(builder, loc, true);

  Value result = values[0];
  for (size_t i = 1; i < values.size(); ++i) {
    result = builder.create<comb::AndOp>(loc, result, values[i], false);
  }
  return result;
}

/// Helper to create OR of multiple values
static Value createOrChain(OpBuilder &builder, Location loc,
                           ArrayRef<Value> values) {
  if (values.empty())
    return createI1Constant(builder, loc, false);

  Value result = values[0];
  for (size_t i = 1; i < values.size(); ++i) {
    result = builder.create<comb::OrOp>(loc, result, values[i], false);
  }
  return result;
}


/// Convert a CallOp to HW signal assignments
static LogicalResult convertCallOp(cmt2::CallOp callOp,
                                   ModuleConversionContext &ctx,
                                   IRMapping &mapping,
                                   SmallVectorImpl<Value> &readySignalsUsed,
                                   Value fireSignal) {
  LLVM_DEBUG(llvm::dbgs() << "    Converting call to @" << callOp.getCallee()
                          << " @" << callOp.getMethodOrValue() << "\n");

  auto calleeName = callOp.getCallee().getRootReference();
  auto methodName = callOp.getMethodOrValue().getRootReference();
  auto loc = callOp.getLoc();

  // Check if this is a call to @this (current module)
  bool isThisCall = calleeName.getValue() == "this";

  // Look up the ready signal for the called method/value
  Value readySignal;
  if (isThisCall) {
    // Calling a function in the same module
    readySignal = ctx.signalTracker.getReadySignal(methodName);
  } else {
    // Calling a function in an instance
    // For external instances, we would need to look up the instance's signals
    // For now, create a placeholder wire representing the ready signal
    std::string readyWireName = (calleeName.getValue() + "_" + methodName.getValue() + "_ready").str();
    auto readyWire = ctx.builder.create<sv::WireOp>(loc, ctx.builder.getI1Type(), readyWireName);
    readySignal = ctx.builder.create<sv::ReadInOutOp>(loc, readyWire);
  }

  if (readySignal)
    readySignalsUsed.push_back(readySignal);

  // Determine if this is a method call
  bool isMethodCall = false;
  if (isThisCall) {
    // Look up the function in the current module
    auto moduleLike = llvm::cast<cmt2::Cmt2ModuleLike>(ctx.cmt2Module.getOperation());
    auto func = moduleLike.lookupFunctionLike(methodName);
    if (func)
      isMethodCall = (func.getFunctionKind() == FunctionKind::Method);
  } else {
    // For external calls, assume methods for now
    isMethodCall = true;
  }

  // If this is a method call, generate enable signal assignment
  // The enable signal is asserted when the calling function fires
  if (isMethodCall && fireSignal) {
    if (isThisCall) {
      // For @this calls, get the enable wire from the signal tracker
      Value enableSignal = ctx.signalTracker.getEnableSignal(methodName);
      if (enableSignal) {
        // Assign the fire signal to the enable wire
        // This connects the calling function's fire to the called method's enable
        std::string enableWireName = ctx.getEnableSignalName(
            llvm::cast<cmt2::Cmt2ModuleLike>(ctx.cmt2Module.getOperation())
                .lookupFunctionLike(methodName));
        auto enableWire = ctx.builder.create<sv::WireOp>(loc, ctx.builder.getI1Type(),
                                                         enableWireName + "_call");
        ctx.builder.create<sv::AssignOp>(loc, enableWire, fireSignal);
      }
    } else {
      // For external calls, create an enable wire and assign fire signal to it
      std::string enableWireName = (calleeName.getValue() + "_" + methodName.getValue() + "_enable").str();
      auto enableWire = ctx.builder.create<sv::WireOp>(loc, ctx.builder.getI1Type(), enableWireName);
      ctx.builder.create<sv::AssignOp>(loc, enableWire, fireSignal);
    }
  }

  // Map operands (arguments) through the mapping
  SmallVector<Value> mappedArgs;
  for (auto arg : callOp.getInputs()) {
    mappedArgs.push_back(mapping.lookupOrDefault(arg));
  }

  // Map the call results
  // For @this calls, we can wire to the actual body results
  // For external calls, we create placeholder wires (actual wiring happens at instance level)
  if (isThisCall) {
    // Get the body results from the signal tracker
    auto bodyResults = ctx.signalTracker.getBodyResults(methodName);
    if (bodyResults.size() == callOp.getResults().size()) {
      // Wire the call results to the function's body results
      for (auto [callResult, bodyResult] : llvm::zip(callOp.getResults(), bodyResults)) {
        mapping.map(callResult, bodyResult);
      }
    } else {
      // Mismatch in result count - create placeholder wires
      for (auto result : callOp.getResults()) {
        std::string resultWireName = (calleeName.getValue() + "_" + methodName.getValue() + "_result").str();
        auto wireOp = ctx.builder.create<sv::WireOp>(loc, result.getType(), resultWireName);
        auto readWire = ctx.builder.create<sv::ReadInOutOp>(loc, wireOp);
        mapping.map(result, readWire);
      }
    }
  } else {
    // For external instance calls, create placeholder wires
    // These would need to be connected to actual instance ports in a full implementation
    for (auto result : callOp.getResults()) {
      std::string resultWireName = (calleeName.getValue() + "_" + methodName.getValue() + "_result").str();
      auto wireOp = ctx.builder.create<sv::WireOp>(loc, result.getType(), resultWireName);
      auto readWire = ctx.builder.create<sv::ReadInOutOp>(loc, wireOp);
      mapping.map(result, readWire);
    }
  }

  return success();
}

/// Generate logic for a single function (rule/method/value)
static LogicalResult generateFunctionLogic(cmt2::Cmt2FunctionLike func,
                                           ModuleConversionContext &ctx,
                                           ArrayRef<cmt2::Cmt2FunctionLike>
                                               precedingConflictingFuncs) {
  LLVM_DEBUG(llvm::dbgs() << "  Generating logic for @" << func.functionName()
                          << "\n");

  // Step 1: Validate call sequence
  if (failed(validateCallSequence(func, ctx)))
    return failure();

  auto loc = func.getLoc();
  auto funcKind = func.getFunctionKind();

  // Step 2: Get guard and body blocks
  // For Cmt2 operations, we need to access the regions differently
  // based on the operation type
  Region *guardRegion = nullptr;
  Region *bodyRegion = nullptr;

  if (auto ruleOp = llvm::dyn_cast<cmt2::RuleOp>(func.getOperation())) {
    guardRegion = &ruleOp.getGuard();
    bodyRegion = &ruleOp.getBody();
  } else if (auto methodOp = llvm::dyn_cast<cmt2::MethodOp>(func.getOperation())) {
    guardRegion = &methodOp.getGuard();
    bodyRegion = &methodOp.getBody();
  } else if (auto valueOp = llvm::dyn_cast<cmt2::ValueOp>(func.getOperation())) {
    guardRegion = &valueOp.getGuard();
    bodyRegion = &valueOp.getBody();
  } else {
    // BindMethodOp or BindValueOp - skip for now
    return success();
  }

  if (!guardRegion || guardRegion->empty())
    return func.emitError("function missing guard region");
  if (!bodyRegion || bodyRegion->empty())
    return func.emitError("function missing body region");

  // Step 3: Clone guard logic
  IRMapping guardMapping;
  // Start with the global mapping (module arguments)
  guardMapping = ctx.globalMapping;
  Value guardResult;
  SmallVector<Value> guardReadySignals;

  // Clone guard region operations
  Block &guardBlock = guardRegion->front();
  // Insert before the terminator (hw.output)
  auto *terminator = ctx.hwModule.getBodyBlock()->getTerminator();
  ctx.builder.setInsertionPoint(terminator);

  for (auto &op : guardBlock.without_terminator()) {
    if (auto callOp = dyn_cast<cmt2::CallOp>(&op)) {
      // Convert call operations to signal references
      // Guard calls don't have fire signal yet (pass null)
      if (failed(convertCallOp(callOp, ctx, guardMapping, guardReadySignals, Value())))
        return failure();
    } else {
      // Clone other operations directly
      ctx.builder.clone(op, guardMapping);
    }
  }

  // Get the guard result (from return operation)
  if (auto returnOp = dyn_cast<cmt2::ReturnOp>(guardBlock.getTerminator())) {
    if (returnOp.getNumOperands() > 0) {
      auto originalResult = returnOp.getOperand(0);
      guardResult = guardMapping.lookupOrDefault(originalResult);
    }
  }

  if (!guardResult) {
    // If no explicit guard result, default to true
    guardResult = createI1Constant(ctx.builder, loc, true);
  }

  // Also collect ready signals from body region calls
  SmallVector<Value> bodyReadySignals;
  Block &bodyBlock = bodyRegion->front();
  for (auto &op : bodyBlock.without_terminator()) {
    if (auto callOp = dyn_cast<cmt2::CallOp>(&op)) {
      // Temporarily collect ready signals (we'll process body later)
      auto calleeName = callOp.getCallee().getRootReference();
      auto methodName = callOp.getMethodOrValue().getRootReference();

      bool isThisCall = calleeName.getValue() == "this";
      if (isThisCall) {
        Value readySignal = ctx.signalTracker.getReadySignal(methodName);
        if (readySignal)
          bodyReadySignals.push_back(readySignal);
      }
    }
  }

  // Step 4: Generate ready signal
  // ready = guard_result AND (ready signals of called functions)
  //         AND NOT (any preceding conflicting function fired)
  std::string readyName = ctx.getReadySignalName(func);

  SmallVector<Value> readyComponents;
  readyComponents.push_back(guardResult);

  // Add ready signals from called functions (both guard and body)
  readyComponents.append(guardReadySignals.begin(), guardReadySignals.end());
  readyComponents.append(bodyReadySignals.begin(), bodyReadySignals.end());

  // Add NOT(preceding conflicting functions fired)
  SmallVector<Value> precedingFireSignals;
  for (auto precedingFunc : precedingConflictingFuncs) {
    if (auto fireSignal = ctx.signalTracker.getFireSignal(precedingFunc.functionNameAttr())) {
      precedingFireSignals.push_back(fireSignal);
    }
  }

  if (!precedingFireSignals.empty()) {
    // NOT (any preceding fired) = NOT (OR of all preceding fires)
    Value anyPrecedingFired = createOrChain(ctx.builder, loc, precedingFireSignals);
    Value notPrecedingFired = ctx.builder.create<comb::XorOp>(
        loc, anyPrecedingFired, createI1Constant(ctx.builder, loc, true), false);
    readyComponents.push_back(notPrecedingFired);
  }

  Value readySignal = createAndChain(ctx.builder, loc, readyComponents);

  // Create a wire for the ready signal
  auto readyWire = ctx.builder.create<sv::WireOp>(loc, ctx.builder.getI1Type(), readyName);
  ctx.builder.create<sv::AssignOp>(loc, readyWire, readySignal);
  readySignal = ctx.builder.create<sv::ReadInOutOp>(loc, readyWire);

  // Step 5: Generate enable signal (for methods only)
  Value enableSignal;
  if (funcKind == FunctionKind::Method) {
    std::string enableName = ctx.getEnableSignalName(func);
    // Create enable input wire
    enableSignal = ctx.builder.create<sv::WireOp>(loc, ctx.builder.getI1Type(), enableName);
    enableSignal = ctx.builder.create<sv::ReadInOutOp>(loc, enableSignal);
  }

  // Step 6: Generate fire signal
  // For value/rule: fire = ready
  // For method: fire = ready AND enable
  std::string fireName = ctx.getFireSignalName(func);
  Value fireSignal;

  if (funcKind == FunctionKind::Method && enableSignal) {
    fireSignal = ctx.builder.create<comb::AndOp>(loc, readySignal, enableSignal, false);
  } else {
    fireSignal = readySignal;
  }

  // Create wire for fire signal
  auto fireWire = ctx.builder.create<sv::WireOp>(loc, ctx.builder.getI1Type(), fireName);
  ctx.builder.create<sv::AssignOp>(loc, fireWire, fireSignal);
  fireSignal = ctx.builder.create<sv::ReadInOutOp>(loc, fireWire);

  // Step 7: Clone body logic guarded by fire signal
  IRMapping bodyMapping;
  // Start with the global mapping (module arguments)
  bodyMapping = ctx.globalMapping;
  SmallVector<Value> bodyCallReadySignals; // Not used here, already collected above
  SmallVector<Value> bodyResults;

  for (auto &op : bodyBlock.without_terminator()) {
    if (auto callOp = dyn_cast<cmt2::CallOp>(&op)) {
      // Convert call operations with fire signal for enable assignment
      // The fire signal guards method calls by driving their enable signals
      if (failed(convertCallOp(callOp, ctx, bodyMapping, bodyCallReadySignals, fireSignal)))
        return failure();
    } else {
      // Clone other operations directly
      // Combinational logic executes unconditionally
      // Stateful operations are guarded at the method call level (enable signals)
      ctx.builder.clone(op, bodyMapping);
    }
  }

  // Get body results from return operation
  if (auto returnOp = dyn_cast<cmt2::ReturnOp>(bodyBlock.getTerminator())) {
    for (auto operand : returnOp.getOutputs()) {
      Value mappedResult = bodyMapping.lookupOrDefault(operand);
      bodyResults.push_back(mappedResult);
    }
  }

  // Register signals and results
  if (readySignal)
    ctx.signalTracker.registerReadySignal(func.functionNameAttr(), readySignal);
  if (enableSignal)
    ctx.signalTracker.registerEnableSignal(func.functionNameAttr(),
                                           enableSignal);
  if (fireSignal)
    ctx.signalTracker.registerFireSignal(func.functionNameAttr(), fireSignal);

  // Store body results for later output port connection
  if (!bodyResults.empty())
    ctx.signalTracker.registerBodyResults(func.functionNameAttr(), bodyResults);

  return success();
}

/// Convert a schedule group to HW logic
static LogicalResult convertScheduleGroup(const cmt2::ScheduleGroup &group,
                                          ModuleConversionContext &ctx) {
  LLVM_DEBUG(llvm::dbgs() << "Converting schedule group with "
                          << group.size() << " functions\n");

  // Get conflict matrix for the module
  auto *moduleMatrix =
      ctx.conflictAnalysis.getModuleMatrix(ctx.cmt2Module.getSymNameAttr());
  if (!moduleMatrix)
    return ctx.cmt2Module.emitError("no conflict matrix found for module");

  const auto &funcNames = group.getFunctions();

  // Cast to Cmt2ModuleLike to access lookupFunctionLike
  auto moduleLike = llvm::cast<cmt2::Cmt2ModuleLike>(ctx.cmt2Module.getOperation());

  // Look up function operations and convert them
  for (size_t i = 0; i < funcNames.size(); ++i) {
    auto funcName = funcNames[i];

    // Look up the function operation in the module
    auto func = moduleLike.lookupFunctionLike(funcName);
    if (!func) {
      return ctx.cmt2Module.emitError("function not found: ") << funcName;
    }

    // Collect preceding functions with conflicts
    SmallVector<cmt2::Cmt2FunctionLike> precedingConflictingFuncs;
    for (size_t j = 0; j < i; ++j) {
      auto prevFuncName = funcNames[j];
      auto rel = moduleMatrix->getRelationship(funcName, prevFuncName);
      if (rel == Relationship::Conflict || rel == Relationship::SequentialBefore) {
        auto prevFunc = moduleLike.lookupFunctionLike(prevFuncName);
        if (prevFunc)
          precedingConflictingFuncs.push_back(prevFunc);
      }
    }

    // Generate logic for this function
    if (failed(generateFunctionLogic(func, ctx, precedingConflictingFuncs)))
      return failure();
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Module Conversion
//===----------------------------------------------------------------------===//

/// Convert a Cmt2 ModuleOp to hw.module
static LogicalResult convertModule(cmt2::ModuleOp cmt2Module, OpBuilder &builder,
                                   const cmt2::SchedulerAnalysis &schedulerAnalysis,
                                   const cmt2::ConflictMatrixAnalysis &conflictAnalysis) {
  LLVM_DEBUG(llvm::dbgs() << "Converting module @" << cmt2Module.getSymName()
                          << "\n");

  // Get schedule for this module
  auto *moduleSchedule = schedulerAnalysis.getModuleSchedule(cmt2Module.getSymNameAttr());
  if (!moduleSchedule) {
    return cmt2Module.emitError("no schedule found for module - run scheduler "
                                "analysis and private function inlining first");
  }

  // Create hw.module with same name and ports
  SmallVector<hw::PortInfo> ports;
  auto argNamesAttr = cmt2Module.getArgNames();
  for (auto [idx, arg] : llvm::enumerate(cmt2Module.getBody().getArguments())) {
    StringRef portName = llvm::cast<StringAttr>(argNamesAttr[idx]).getValue();
    ports.push_back({{builder.getStringAttr(portName), arg.getType(),
                      hw::ModulePort::Direction::Input}});
  }

  // Add output ports for methods/values
  // Collect all methods and values in the module
  SmallVector<cmt2::Cmt2FunctionLike> functions;

  cmt2Module.walk([&](Operation *op) {
    if (auto func = llvm::dyn_cast<cmt2::Cmt2FunctionLike>(op)) {
      auto funcKind = func.getFunctionKind();
      // Only add ports for methods and values (not rules)
      if (funcKind == FunctionKind::Method || funcKind == FunctionKind::Value) {
        functions.push_back(func);
      }
    }
  });

  auto i1Type = builder.getI1Type();
  for (auto func : functions) {
    auto funcKind = func.getFunctionKind();
    std::string baseName = func.functionName().str();

    // Get custom signal names if specified
    std::string readyName = baseName + "_ready";
    if (auto customReady = func.getReadyName(); !customReady.empty())
      readyName = customReady.str();

    std::string enableName = baseName + "_enable";
    if (auto customEnable = func.getEnableName(); !customEnable.empty())
      enableName = customEnable.str();

    if (funcKind == FunctionKind::Method) {
      // Methods have: enable (input), ready (output)
      // Method arguments/results are passed through call sites, not module ports
      ports.push_back({{builder.getStringAttr(enableName), i1Type,
                        hw::ModulePort::Direction::Input}});
      ports.push_back({{builder.getStringAttr(readyName), i1Type,
                        hw::ModulePort::Direction::Output}});
    } else if (funcKind == FunctionKind::Value) {
      // Values have: ready (output)
      // Value arguments/results are passed through call sites, not module ports
      ports.push_back({{builder.getStringAttr(readyName), i1Type,
                        hw::ModulePort::Direction::Output}});
    }
  }

  hw::ModulePortInfo portInfo(ports);
  auto loc = cmt2Module.getLoc();

  auto hwModule = builder.create<hw::HWModuleOp>(
      loc, builder.getStringAttr(cmt2Module.getSymName()), portInfo);

  // Map module arguments from cmt2Module to hwModule
  IRMapping globalMapping;
  for (auto [cmt2Arg, hwArg] : llvm::zip(cmt2Module.getBody().getArguments(),
                                          hwModule.getBodyBlock()->getArguments())) {
    globalMapping.map(cmt2Arg, hwArg);
  }

  // Set up conversion context
  SignalTracker signalTracker;
  ModuleConversionContext ctx(cmt2Module, hwModule, builder, signalTracker,
                              schedulerAnalysis, conflictAnalysis,
                              cmt2::CallInfoView(cmt2Module->getParentOfType<cmt2::CircuitOp>()),
                              globalMapping);

  // Move insertion point to body (before the auto-generated hw.output)
  ctx.builder.setInsertionPoint(hwModule.getBodyBlock()->getTerminator());

  // Convert each schedule group
  for (const auto &group : moduleSchedule->getGroups()) {
    if (failed(convertScheduleGroup(group, ctx)))
      return failure();
  }

  // Connect output ports with proper values
  // Collect all output values in the order they were added to ports
  SmallVector<Value> outputValues;

  for (auto func : functions) {
    auto funcKind = func.getFunctionKind();
    auto funcNameAttr = func.functionNameAttr();

    if (funcKind == FunctionKind::Method) {
      // Methods: enable (already an input), ready (output)
      // Method results are not exposed as module outputs - they're passed through call sites
      if (auto ready = signalTracker.getReadySignal(funcNameAttr))
        outputValues.push_back(ready);
      else {
        // Create a default false ready signal if not found
        outputValues.push_back(createI1Constant(builder, loc, false));
      }
    } else if (funcKind == FunctionKind::Value) {
      // Values: ready (output)
      // Value results are not exposed as module outputs - they're passed through call sites
      if (auto ready = signalTracker.getReadySignal(funcNameAttr))
        outputValues.push_back(ready);
      else {
        outputValues.push_back(createI1Constant(builder, loc, false));
      }
    }
  }

  // Update the hw.output terminator with the collected output values
  auto *terminator = hwModule.getBodyBlock()->getTerminator();
  terminator->setOperands(outputValues);

  return success();
}

//===----------------------------------------------------------------------===//
// Pass Implementation
//===----------------------------------------------------------------------===//

struct Cmt2ToHWPass : public circt::impl::Cmt2ToHWBase<Cmt2ToHWPass> {
  void runOnOperation() override;
};

void Cmt2ToHWPass::runOnOperation() {
  mlir::ModuleOp mlirModule = getOperation();

  // Find the Cmt2 circuit
  cmt2::CircuitOp circuitOp;
  mlirModule.walk([&](cmt2::CircuitOp op) {
    if (!circuitOp)
      circuitOp = op;
  });

  if (!circuitOp) {
    LLVM_DEBUG(llvm::dbgs() << "No cmt2.circuit found, skipping conversion\n");
    return;
  }

  // Run analyses
  LLVM_DEBUG(llvm::dbgs() << "Running analyses...\n");
  cmt2::SchedulerAnalysis schedulerAnalysis(circuitOp);

  // We need to create ConflictMatrixAnalysis separately since it's private in Scheduler
  // In a full implementation, SchedulerAnalysis could expose a getConflictAnalysis() method
  cmt2::ConflictMatrixAnalysis conflictAnalysis(circuitOp);

  // We need CallInfo for looking up function details
  // Note: This is a simplified approach - in full implementation we'd use
  // the CallInfo from inside SchedulerAnalysis

  // Convert each module
  OpBuilder builder(&getContext());
  builder.setInsertionPointAfter(circuitOp);

  SmallVector<cmt2::ModuleOp> modulesToConvert;
  circuitOp.walk([&](cmt2::ModuleOp module) {
    modulesToConvert.push_back(module);
  });

  for (auto module : modulesToConvert) {
    if (failed(convertModule(module, builder, schedulerAnalysis,
                            conflictAnalysis))) {
      signalPassFailure();
      return;
    }
  }

  // Note: We don't erase the original cmt2.circuit to avoid use-after-free issues
  // The converted hw.module operations are added alongside the cmt2 IR
  // A separate cleanup pass could remove the cmt2 IR if needed

  LLVM_DEBUG(llvm::dbgs() << "Cmt2ToHW conversion completed\n");
}

} // namespace

//===----------------------------------------------------------------------===//
// Pass Creation
//===----------------------------------------------------------------------===//

std::unique_ptr<mlir::Pass> circt::createCmt2ToHWPass() {
  return std::make_unique<Cmt2ToHWPass>();
}

//===- Cmt2ToHW.cpp - Translate Cmt2 into HW ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Cmt2 to HW conversion pass.
//
// ALGORITHM OVERVIEW:
// ===================
// The conversion transforms Cmt2 modules (rule-based concurrent system) into
// hw.module (structural hardware). The key challenge is handling instances that
// depend on each other's results, which requires deferred instance creation.
//
// THREE-PHASE CONVERSION:
// 1. Register Instances: Collect all cmt2.instance ops without creating hw.instance
// 2. Generate Function Logic: Process scheduled functions, track method calls
// 3. Create Instances: Build hw.instance in topological order with muxed ports
//
// KEY DATA STRUCTURES:
// - InstanceTracker: Manages deferred instances and dependency tracking
// - PortConnectionTracker: Records multiple calls to same method for muxing
// - SignalTracker: Maps function names to their ready/fire/result signals
//
// SSA VALUE HANDLING:
// - Problem: MLIR Values become invalid after replaceAllUsesWith
// - Solution: Store (Operation*, resultIndex) pairs instead of raw Values
// - MethodCall.getMappedArgs() dynamically looks up current values
//
// TOPOLOGICAL SORTING:
// - Uses ready queue algorithm: instances with no dependencies created first
// - Updates queue as dependencies are satisfied
// - Detects cyclic dependencies
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

namespace {

//===----------------------------------------------------------------------===//
// Data Structures for Tracking Calls and Dependencies
//===----------------------------------------------------------------------===//

/// Represents a single method/value call with fire condition and arguments.
///
/// IMPORTANT: Stores (Operation*, resultIndex) pairs instead of Values because
/// Values become stale after replaceAllUsesWith operations during instance creation.
struct MethodCall {
  Value fireCondition;              ///< When this call is active
  SmallVector<Value> originalArgs;  ///< Original arguments (for debugging)
  SmallVector<std::pair<Operation*, unsigned>> argOps; ///< (op, resultIdx) pairs
  Location loc;

  /// Dynamically retrieve current argument values by looking up operations.
  /// This survives SSA value invalidation after IR updates.
  SmallVector<Value> getMappedArgs() const {
    SmallVector<Value> result;
    for (auto [op, resultIdx] : argOps) {
      if (op && resultIdx < op->getNumResults())
        result.push_back(op->getResult(resultIdx));
    }
    return result;
  }

  /// Find all instances this call depends on (arguments from instance results).
  void getDependentInstances(DenseSet<StringAttr> &deps,
                             const DenseMap<Value, StringAttr> &valueToInstance) const {
    for (auto [op, resultIdx] : argOps) {
      if (op && resultIdx < op->getNumResults()) {
        Value arg = op->getResult(resultIdx);
        if (auto it = valueToInstance.find(arg); it != valueToInstance.end())
          deps.insert(it->second);
      }
    }
  }
};

/// Tracks multiple calls to the same instance.method for mux generation.
///
/// When multiple functions call the same method (e.g., both @swap and @start
/// call @x @write), we need to generate mux logic to select the active call.
class PortConnectionTracker {
public:
  /// Register a call to instance.method with its fire condition and arguments.
  void registerCall(StringRef instanceName, StringRef methodName,
                    Value fireCondition, ArrayRef<Value> originalArgs,
                    ArrayRef<Value> mappedArgs, Location loc) {
    auto key = (instanceName.str() + "." + methodName.str());

    // Store (Operation*, resultIndex) pairs for each argument
    SmallVector<std::pair<Operation*, unsigned>> argOps;
    for (auto mappedArg : mappedArgs) {
      if (auto defOp = mappedArg.getDefiningOp()) {
        unsigned resultIdx = 0;
        for (auto result : defOp->getResults()) {
          if (result == mappedArg) {
            argOps.push_back({defOp, resultIdx});
            break;
          }
          resultIdx++;
        }
      } else {
        argOps.push_back({nullptr, 0}); // Block argument
      }
    }

    calls[key].push_back({fireCondition, SmallVector<Value>(originalArgs),
                          argOps, loc});
  }

  /// Get all calls to a specific instance.method.
  ArrayRef<MethodCall> getCalls(StringRef instanceName, StringRef methodName) const {
    auto key = (instanceName.str() + "." + methodName.str());
    auto it = calls.find(key);
    return it != calls.end() ? ArrayRef<MethodCall>(it->second) : ArrayRef<MethodCall>();
  }

  /// Check if any calls exist for instance.method.
  bool hasCalls(StringRef instanceName, StringRef methodName) const {
    auto key = (instanceName.str() + "." + methodName.str());
    return calls.find(key) != calls.end();
  }

  /// Get all instances that the given instance depends on.
  void getInstanceDependencies(StringAttr instanceName,
                               DenseSet<StringAttr> &deps,
                               const DenseMap<Value, StringAttr> &valueToInstance) const {
    for (const auto &entry : calls) {
      StringRef key = entry.first();
      if (key.starts_with((instanceName.getValue() + ".").str())) {
        for (const auto &call : entry.second)
          call.getDependentInstances(deps, valueToInstance);
      }
    }
  }

private:
  llvm::StringMap<SmallVector<MethodCall>> calls;
};

/// Tracks signal names and values generated during conversion.
class SignalTracker {
public:
  void registerReadySignal(StringAttr funcName, Value readySignal) {
    readySignals[funcName] = readySignal;
  }
  void registerFireSignal(StringAttr funcName, Value fireSignal) {
    fireSignals[funcName] = fireSignal;
  }
  void registerBodyResults(StringAttr funcName, ArrayRef<Value> results) {
    bodyResults[funcName] = SmallVector<Value>(results.begin(), results.end());
  }

  Value getReadySignal(StringAttr funcName) const {
    auto it = readySignals.find(funcName);
    return it != readySignals.end() ? it->second : Value();
  }
  Value getFireSignal(StringAttr funcName) const {
    auto it = fireSignals.find(funcName);
    return it != fireSignals.end() ? it->second : Value();
  }
  ArrayRef<Value> getBodyResults(StringAttr funcName) const {
    auto it = bodyResults.find(funcName);
    return it != bodyResults.end() ? ArrayRef<Value>(it->second) : ArrayRef<Value>();
  }

  /// Register placeholder constant that will be replaced with actual instance result.
  void registerPlaceholder(Value placeholder, StringAttr instanceName,
                          StringAttr methodName, size_t resultIdx) {
    placeholders[placeholder] = {instanceName, methodName, resultIdx};
  }

  /// Replace placeholders with actual hw.instance results after creation.
  void replacePlaceholders(StringAttr instanceName, hw::InstanceOp hwInstance,
                          cmt2::ExtModuleHwOp extModuleOp, hw::HWModuleOp hwModuleOp) {
    SmallVector<std::pair<Value, Value>> replacements;

    for (const auto &entry : placeholders) {
      if (entry.second.instanceName != instanceName)
        continue;

      Value placeholder = entry.first;
      auto methodName = entry.second.methodName;
      size_t resultIdx = entry.second.resultIdx;

      // Look up bind operation to find corresponding hw port
      auto bindOp = extModuleOp.lookupSymbol(methodName);
      if (!bindOp) continue;

      Value actualResult;
      if (auto bindValue = dyn_cast<cmt2::BindValueOp>(bindOp)) {
        auto dataAttrs = bindValue.getData();
        if (resultIdx < dataAttrs.size()) {
          auto portName = llvm::cast<FlatSymbolRefAttr>(dataAttrs[resultIdx]).getValue();
          actualResult = findInstanceOutput(hwInstance, hwModuleOp, portName);
        }
      } else if (auto bindMethod = dyn_cast<cmt2::BindMethodOp>(bindOp)) {
        auto outputAttrs = bindMethod.getOutputs();
        if (resultIdx < outputAttrs.size()) {
          auto portName = llvm::cast<FlatSymbolRefAttr>(outputAttrs[resultIdx]).getValue();
          actualResult = findInstanceOutput(hwInstance, hwModuleOp, portName);
        }
      }

      if (actualResult)
        replacements.push_back({placeholder, actualResult});
    }

    // Perform all replacements
    for (auto [placeholder, actualResult] : replacements) {
      placeholder.replaceAllUsesWith(actualResult);
      placeholders.erase(placeholder);
    }
  }

private:
  struct PlaceholderInfo {
    StringAttr instanceName;
    StringAttr methodName;
    size_t resultIdx;
  };

  /// Helper to find hw.instance output by port name.
  Value findInstanceOutput(hw::InstanceOp hwInstance, hw::HWModuleOp hwModuleOp,
                          StringRef portName) {
    auto ports = hwModuleOp.getPortList();
    size_t outputIdx = 0;
    for (const auto &port : ports) {
      if (port.dir == hw::ModulePort::Direction::Output) {
        if (port.name.getValue() == portName)
          return hwInstance.getResult(outputIdx);
        outputIdx++;
      }
    }
    return Value();
  }

  DenseMap<StringAttr, Value> readySignals;
  DenseMap<StringAttr, Value> fireSignals;
  DenseMap<StringAttr, SmallVector<Value>> bodyResults;
  DenseMap<Value, PlaceholderInfo> placeholders;
};

/// Information for deferred instance creation.
struct DeferredInstanceInfo {
  cmt2::InstanceOp cmt2Instance;
  cmt2::ExtModuleHwOp extModuleOp;
  hw::HWModuleOp hwModuleOp;
  SmallVector<Value> baseArgs; ///< Arguments from cmt2.instance (mapped)
};

/// Tracks instances and manages deferred creation.
///
/// DEFERRED CREATION STRATEGY:
/// 1. Register all instances first (without creating hw.instance)
/// 2. Track which values come from which instances
/// 3. Create instances in topological order when dependencies are ready
class InstanceTracker {
public:
  void registerDeferredInstance(StringAttr instanceName, DeferredInstanceInfo info) {
    deferredInstances[instanceName] = std::move(info);
  }

  const DeferredInstanceInfo *getDeferredInstance(StringAttr instanceName) const {
    auto it = deferredInstances.find(instanceName);
    return it != deferredInstances.end() ? &it->second : nullptr;
  }

  SmallVector<StringAttr> getDeferredInstanceNames() const {
    SmallVector<StringAttr> names;
    for (const auto &entry : deferredInstances)
      names.push_back(entry.first);
    return names;
  }

  /// Mark instance as created and register its outputs in globalMapping.
  void markInstanceCreated(StringAttr instanceName, hw::InstanceOp hwInstance,
                          IRMapping &globalMapping) {
    instances[instanceName] = hwInstance;

    // Register all outputs for dependency tracking
    for (auto result : hwInstance.getResults()) {
      valueToInstance[result] = instanceName;
      globalMapping.map(result, result);
    }

    deferredInstances.erase(instanceName);
  }

  bool isInstanceCreated(StringAttr instanceName) const {
    return instances.count(instanceName) > 0;
  }

  hw::InstanceOp getInstance(StringAttr instanceName) const {
    auto it = instances.find(instanceName);
    return it != instances.end() ? it->second : hw::InstanceOp();
  }

  void registerInstanceResult(Value result, StringAttr instanceName) {
    valueToInstance[result] = instanceName;
  }

  StringAttr getInstanceForValue(Value val) const {
    auto it = valueToInstance.find(val);
    return it != valueToInstance.end() ? it->second : StringAttr();
  }

  const DenseMap<Value, StringAttr> &getValueToInstanceMap() const {
    return valueToInstance;
  }

private:
  DenseMap<StringAttr, DeferredInstanceInfo> deferredInstances;
  DenseMap<StringAttr, hw::InstanceOp> instances;
  DenseMap<Value, StringAttr> valueToInstance; ///< Maps values to producing instance
};

/// Context for converting a single Cmt2 module.
struct ModuleConversionContext {
  cmt2::ModuleOp cmt2Module;
  hw::HWModuleOp hwModule;
  OpBuilder &builder;
  SignalTracker &signalTracker;
  InstanceTracker &instanceTracker;
  PortConnectionTracker &portTracker;
  const cmt2::SchedulerAnalysis &schedulerAnalysis;
  const cmt2::ConflictMatrixAnalysis &conflictAnalysis;
  const cmt2::CallInfoView &callInfo;
  IRMapping &globalMapping; ///< Maps cmt2 module args to hw module args

  ModuleConversionContext(cmt2::ModuleOp cmt2Mod, hw::HWModuleOp hwMod,
                          OpBuilder &b, SignalTracker &tracker,
                          InstanceTracker &instTracker,
                          PortConnectionTracker &portTrack,
                          const cmt2::SchedulerAnalysis &sched,
                          const cmt2::ConflictMatrixAnalysis &conflict,
                          const cmt2::CallInfoView &callInfoView,
                          IRMapping &mapping)
      : cmt2Module(cmt2Mod), hwModule(hwMod), builder(b),
        signalTracker(tracker), instanceTracker(instTracker),
        portTracker(portTrack), schedulerAnalysis(sched),
        conflictAnalysis(conflict), callInfo(callInfoView), globalMapping(mapping) {}

  /// Get signal names with custom attributes or defaults.
  std::string getReadySignalName(cmt2::Cmt2FunctionLike func) {
    if (auto readyName = func.getReadyName(); !readyName.empty())
      return readyName.str();
    return (func.functionName() + "_ready").str();
  }

  std::string getEnableSignalName(cmt2::Cmt2FunctionLike func) {
    if (auto enableName = func.getEnableName(); !enableName.empty())
      return enableName.str();
    return (func.functionName() + "_enable").str();
  }

  std::string getFireSignalName(cmt2::Cmt2FunctionLike func) {
    return (func.functionName() + "_fire").str();
  }
};

//===----------------------------------------------------------------------===//
// Helper Functions for Combinational Logic
//===----------------------------------------------------------------------===//

static Value createI1Constant(OpBuilder &builder, Location loc, bool value) {
  return builder.create<hw::ConstantOp>(loc, APInt(1, value ? 1 : 0));
}

static Value createAndChain(OpBuilder &builder, Location loc, ArrayRef<Value> values) {
  if (values.empty())
    return createI1Constant(builder, loc, true);
  Value result = values[0];
  for (size_t i = 1; i < values.size(); ++i)
    result = builder.create<comb::AndOp>(loc, result, values[i], false);
  return result;
}

static Value createOrChain(OpBuilder &builder, Location loc, ArrayRef<Value> values) {
  if (values.empty())
    return createI1Constant(builder, loc, false);
  Value result = values[0];
  for (size_t i = 1; i < values.size(); ++i)
    result = builder.create<comb::OrOp>(loc, result, values[i], false);
  return result;
}

/// Create mux tree: cond[n] ? val[n] : (cond[n-1] ? val[n-1] : ... : default)
static Value createMux(OpBuilder &builder, Location loc,
                       ArrayRef<Value> conditions, ArrayRef<Value> values,
                       Value defaultValue) {
  assert(conditions.size() == values.size() && "Mismatch in condition/value count");
  if (conditions.empty())
    return defaultValue;

  Value result = defaultValue;
  for (int i = conditions.size() - 1; i >= 0; --i)
    result = builder.create<comb::MuxOp>(loc, conditions[i], values[i], result, false);
  return result;
}

//===----------------------------------------------------------------------===//
// Phase 1: Register Deferred Instances
//===----------------------------------------------------------------------===//

/// Register all cmt2.instance ops for deferred creation.
///
/// We don't create hw.instance yet because input values may not be ready.
/// Instead, we collect information needed for later creation.
static LogicalResult registerDeferredInstances(ModuleConversionContext &ctx) {
  LLVM_DEBUG(llvm::dbgs() << "Registering deferred instances...\n");

  WalkResult walkResult = ctx.cmt2Module.walk([&](cmt2::InstanceOp instOp) {
    LLVM_DEBUG(llvm::dbgs() << "  Registering instance @" << instOp.getSymName() << "\n");

    // Look up referenced module
    auto moduleName = instOp.getModuleName();
    auto circuitOp = ctx.cmt2Module->getParentOfType<cmt2::CircuitOp>();
    auto referencedOp = circuitOp.lookupSymbol(moduleName);

    if (!referencedOp) {
      instOp.emitError("referenced module not found: ") << moduleName;
      return WalkResult::interrupt();
    }

    // Only handle external HW modules for now
    auto extModuleOp = dyn_cast<cmt2::ExtModuleHwOp>(referencedOp);
    if (!extModuleOp) {
      LLVM_DEBUG(llvm::dbgs() << "    Skipping non-external module\n");
      return WalkResult::advance();
    }

    // Find corresponding hw.module
    auto hwModuleName = extModuleOp.getExtModuleName();
    auto mlirModule = circuitOp->getParentOfType<mlir::ModuleOp>();
    auto hwModuleOp = mlirModule.lookupSymbol<hw::HWModuleOp>(hwModuleName);

    if (!hwModuleOp) {
      instOp.emitError("hw module not found: ") << hwModuleName;
      return WalkResult::interrupt();
    }

    // Map base instance arguments
    SmallVector<Value> baseArgs;
    for (auto arg : instOp.getArgs())
      baseArgs.push_back(ctx.globalMapping.lookupOrDefault(arg));

    // Register deferred instance
    DeferredInstanceInfo info{instOp, extModuleOp, hwModuleOp, baseArgs};
    ctx.instanceTracker.registerDeferredInstance(instOp.getSymNameAttr(), std::move(info));

    return WalkResult::advance();
  });

  return walkResult.wasInterrupted() ? failure() : success();
}

//===----------------------------------------------------------------------===//
// Phase 2: Function Logic Generation and Call Tracking
//===----------------------------------------------------------------------===//

/// Validate that call sequence respects conflict relationships.
/// Also checks that no @this calls exist (they should be inlined first).
static LogicalResult validateCallSequence(cmt2::Cmt2FunctionLike func,
                                         ModuleConversionContext &ctx) {
  SmallVector<cmt2::CallOp> calls;
  func.walk([&](cmt2::CallOp callOp) { calls.push_back(callOp); });

  if (calls.empty())
    return success();

  // Check for @this calls - these should be inlined first
  for (auto callOp : calls) {
    auto calleeName = callOp.getCallee().getRootReference();
    if (calleeName.getValue() == "this") {
      return callOp.emitError("found call to @this - run -cmt2-inline-private-funcs first");
    }
  }

  auto *moduleMatrix = ctx.conflictAnalysis.getModuleMatrix(ctx.cmt2Module.getSymNameAttr());
  if (!moduleMatrix)
    return success();

  // Check each pair for sequence violations
  for (size_t i = 0; i < calls.size(); ++i) {
    for (size_t j = i + 1; j < calls.size(); ++j) {
      auto call1 = calls[i];
      auto call2 = calls[j];

      auto callee1 = call1.getCallee().getRootReference();
      auto callee2 = call2.getCallee().getRootReference();

      auto entity1Name = call1.getMethodOrValue().getRootReference();
      auto entity2Name = call2.getMethodOrValue().getRootReference();

      // Check if entity2 must come before entity1 (violation)
      auto relReverse = moduleMatrix->getRelationship(entity2Name, entity1Name);
      if (relReverse == Relationship::SequentialBefore) {
        return func.emitError("call sequence violation: @")
               << entity1Name << " is called before @" << entity2Name
               << " but @" << entity2Name << " must execute before @" << entity1Name;
      }
    }
  }

  return success();
}

/// Convert a cmt2.call operation during region cloning.
///
/// For external calls with fireSignal, registers the call for muxing.
/// Maps results to actual values (from signal tracker or instance outputs).
static LogicalResult convertCallOp(cmt2::CallOp callOp,
                                   ModuleConversionContext &ctx,
                                   IRMapping &mapping,
                                   SmallVectorImpl<Value> &readySignalsUsed,
                                   Value fireSignal) {
  auto calleeName = callOp.getCallee().getRootReference();
  auto methodName = callOp.getMethodOrValue().getRootReference();
  auto loc = callOp.getLoc();
  bool isThisCall = calleeName.getValue() == "this";

  // Get ready signal
  Value readySignal = isThisCall
      ? ctx.signalTracker.getReadySignal(methodName)
      : createI1Constant(ctx.builder, loc, true);

  if (readySignal)
    readySignalsUsed.push_back(readySignal);

  // Register call for muxing (external calls only)
  if (!isThisCall && fireSignal) {
    SmallVector<Value> originalArgs(callOp.getInputs().begin(), callOp.getInputs().end());
    SmallVector<Value> mappedArgs;
    for (auto arg : callOp.getInputs()) {
      Value mappedArg = mapping.lookupOrDefault(arg);
      if (!mappedArg)
        return callOp.emitError("failed to map argument - value is null");
      mappedArgs.push_back(mappedArg);
    }
    ctx.portTracker.registerCall(calleeName.getValue(), methodName.getValue(),
                                  fireSignal, originalArgs, mappedArgs, loc);
  }

  // Map call results
  if (isThisCall) {
    // Wire to body results from signal tracker
    auto bodyResults = ctx.signalTracker.getBodyResults(methodName);
    if (bodyResults.size() == callOp.getResults().size()) {
      for (auto [callResult, bodyResult] : llvm::zip(callOp.getResults(), bodyResults))
        mapping.map(callResult, bodyResult);
    } else {
      // Mismatch - create placeholders
      for (auto result : callOp.getResults()) {
        auto constZero = ctx.builder.create<hw::ConstantOp>(loc, result.getType(), 0);
        mapping.map(result, constZero);
      }
    }
  } else {
    // Wire to instance outputs (if created) or create placeholders
    auto hwInstance = ctx.instanceTracker.getInstance(calleeName);
    if (hwInstance) {
      // Instance exists - wire to its outputs
      auto circuitOp = ctx.cmt2Module->getParentOfType<cmt2::CircuitOp>();
      auto cmt2Instance = ctx.cmt2Module.lookupSymbol<cmt2::InstanceOp>(calleeName);
      if (cmt2Instance) {
        auto extModuleOp = circuitOp.lookupSymbol<cmt2::ExtModuleHwOp>(
            cmt2Instance.getModuleName());
        if (extModuleOp) {
          auto bindOp = extModuleOp.lookupSymbol(methodName);

          // Helper to map outputs from bind operation
          auto mapOutputs = [&](auto bindOp, auto outputAttrs) {
            size_t resultIdx = 0;
            for (auto outputAttr : outputAttrs) {
              auto portName = llvm::cast<FlatSymbolRefAttr>(outputAttr).getValue();
              auto mlirModule = circuitOp->getParentOfType<mlir::ModuleOp>();
              auto hwModuleOp = mlirModule.lookupSymbol<hw::HWModuleOp>(hwInstance.getModuleName());
              if (hwModuleOp) {
                auto ports = hwModuleOp.getPortList();
                size_t outputIdx = 0;
                for (const auto &port : ports) {
                  if (port.dir == hw::ModulePort::Direction::Output) {
                    if (port.name.getValue() == portName && resultIdx < callOp.getResults().size()) {
                      Value instanceOutput = hwInstance.getResult(outputIdx);
                      mapping.map(callOp.getResults()[resultIdx], instanceOutput);
                      ctx.instanceTracker.registerInstanceResult(instanceOutput, calleeName);
                      resultIdx++;
                      break;
                    }
                    outputIdx++;
                  }
                }
              }
            }
          };

          if (auto bindValue = dyn_cast_or_null<cmt2::BindValueOp>(bindOp)) {
            mapOutputs(bindValue, bindValue.getData());
          } else if (auto bindMethod = dyn_cast_or_null<cmt2::BindMethodOp>(bindOp)) {
            mapOutputs(bindMethod, bindMethod.getOutputs());
          }
        }
      }
    }

    // Create placeholders for unmapped results
    for (auto [idx, result] : llvm::enumerate(callOp.getResults())) {
      if (!mapping.contains(result)) {
        auto constZero = ctx.builder.create<hw::ConstantOp>(loc, result.getType(), 0);
        mapping.map(result, constZero);
        ctx.signalTracker.registerPlaceholder(constZero, calleeName, methodName, idx);
      }
    }
  }

  return success();
}

/// Generate hardware logic for a single function (rule/method/value).
///
/// SIGNAL GENERATION:
/// - ready = guard AND called_ready AND NOT(preceding_conflicts_fired)
/// - enable = input port (methods only)
/// - fire = ready (rules/values) or ready AND enable (methods)
static LogicalResult generateFunctionLogic(cmt2::Cmt2FunctionLike func,
                                           ModuleConversionContext &ctx,
                                           ArrayRef<cmt2::Cmt2FunctionLike>
                                               precedingConflictingFuncs) {
  LLVM_DEBUG(llvm::dbgs() << "  Generating logic for @" << func.functionName() << "\n");

  if (failed(validateCallSequence(func, ctx)))
    return failure();

  auto loc = func.getLoc();
  auto funcKind = func.getFunctionKind();

  // Get guard and body regions
  Region *guardRegion = nullptr, *bodyRegion = nullptr;
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
    return success(); // Skip BindMethodOp/BindValueOp
  }

  if (!guardRegion || guardRegion->empty() || !bodyRegion || bodyRegion->empty())
    return func.emitError("function missing guard or body region");

  // Clone guard logic
  IRMapping guardMapping = ctx.globalMapping;
  SmallVector<Value> guardReadySignals;
  ctx.builder.setInsertionPoint(ctx.hwModule.getBodyBlock()->getTerminator());

  Block &guardBlock = guardRegion->front();
  for (auto &op : guardBlock.without_terminator()) {
    if (auto callOp = dyn_cast<cmt2::CallOp>(&op)) {
      if (failed(convertCallOp(callOp, ctx, guardMapping, guardReadySignals, Value())))
        return failure();
    } else {
      ctx.builder.clone(op, guardMapping);
    }
  }

  // Get guard result
  Value guardResult;
  if (auto returnOp = dyn_cast<cmt2::ReturnOp>(guardBlock.getTerminator())) {
    if (returnOp.getNumOperands() > 0)
      guardResult = guardMapping.lookupOrDefault(returnOp.getOperand(0));
  }
  if (!guardResult)
    guardResult = createI1Constant(ctx.builder, loc, true);

  // Collect ready signals from body calls
  SmallVector<Value> bodyReadySignals;
  Block &bodyBlock = bodyRegion->front();
  for (auto &op : bodyBlock.without_terminator()) {
    if (auto callOp = dyn_cast<cmt2::CallOp>(&op)) {
      auto calleeName = callOp.getCallee().getRootReference();
      auto methodName = callOp.getMethodOrValue().getRootReference();
      if (calleeName.getValue() == "this") {
        if (Value readySignal = ctx.signalTracker.getReadySignal(methodName))
          bodyReadySignals.push_back(readySignal);
      }
    }
  }

  // Generate ready signal
  SmallVector<Value> readyComponents;
  readyComponents.push_back(guardResult);
  readyComponents.append(guardReadySignals.begin(), guardReadySignals.end());
  readyComponents.append(bodyReadySignals.begin(), bodyReadySignals.end());

  // Add NOT(preceding conflicts fired)
  SmallVector<Value> precedingFireSignals;
  for (auto precedingFunc : precedingConflictingFuncs) {
    if (auto fireSignal = ctx.signalTracker.getFireSignal(precedingFunc.functionNameAttr()))
      precedingFireSignals.push_back(fireSignal);
  }

  if (!precedingFireSignals.empty()) {
    Value anyPrecedingFired = createOrChain(ctx.builder, loc, precedingFireSignals);
    Value notPrecedingFired = ctx.builder.create<comb::XorOp>(
        loc, anyPrecedingFired, createI1Constant(ctx.builder, loc, true), false);
    readyComponents.push_back(notPrecedingFired);
  }

  Value readySignal = createAndChain(ctx.builder, loc, readyComponents);

  // Generate enable signal (methods only)
  Value enableSignal;
  if (funcKind == FunctionKind::Method) {
    std::string enableName = ctx.getEnableSignalName(func);
    auto ports = ctx.hwModule.getPortList();
    size_t inputIdx = 0;
    for (const auto &port : ports) {
      if (port.dir == hw::ModulePort::Direction::Input) {
        if (port.name.getValue().str() == enableName) {
          enableSignal = ctx.hwModule.getBodyBlock()->getArgument(inputIdx);
          break;
        }
        inputIdx++;
      }
    }
  }

  // Generate fire signal
  Value fireSignal = (funcKind == FunctionKind::Method && enableSignal)
      ? ctx.builder.create<comb::AndOp>(loc, readySignal, enableSignal, false)
      : readySignal;

  // Clone body logic
  IRMapping bodyMapping = ctx.globalMapping;
  SmallVector<Value> bodyCallReadySignals;
  SmallVector<Value> bodyResults;

  for (auto &op : bodyBlock.without_terminator()) {
    if (auto callOp = dyn_cast<cmt2::CallOp>(&op)) {
      if (failed(convertCallOp(callOp, ctx, bodyMapping, bodyCallReadySignals, fireSignal)))
        return failure();
    } else {
      ctx.builder.clone(op, bodyMapping);
    }
  }

  // Get body results
  if (auto returnOp = dyn_cast<cmt2::ReturnOp>(bodyBlock.getTerminator())) {
    for (auto operand : returnOp.getOutputs())
      bodyResults.push_back(bodyMapping.lookupOrDefault(operand));
  }

  // Register signals and results
  ctx.signalTracker.registerReadySignal(func.functionNameAttr(), readySignal);
  ctx.signalTracker.registerFireSignal(func.functionNameAttr(), fireSignal);
  if (!bodyResults.empty())
    ctx.signalTracker.registerBodyResults(func.functionNameAttr(), bodyResults);

  return success();
}

/// Convert a schedule group to hardware logic.
static LogicalResult convertScheduleGroup(const cmt2::ScheduleGroup &group,
                                          ModuleConversionContext &ctx) {
  auto *moduleMatrix = ctx.conflictAnalysis.getModuleMatrix(ctx.cmt2Module.getSymNameAttr());
  if (!moduleMatrix)
    return ctx.cmt2Module.emitError("no conflict matrix found for module");

  const auto &funcNames = group.getFunctions();
  auto moduleLike = llvm::cast<cmt2::Cmt2ModuleLike>(ctx.cmt2Module.getOperation());

  // Generate logic for each function in order
  for (size_t i = 0; i < funcNames.size(); ++i) {
    auto funcName = funcNames[i];
    auto func = moduleLike.lookupFunctionLike(funcName);
    if (!func)
      return ctx.cmt2Module.emitError("function not found: ") << funcName;

    // Collect preceding conflicting functions
    SmallVector<cmt2::Cmt2FunctionLike> precedingConflictingFuncs;
    for (size_t j = 0; j < i; ++j) {
      auto prevFuncName = funcNames[j];
      auto rel = moduleMatrix->getRelationship(funcName, prevFuncName);
      if (rel == Relationship::Conflict || rel == Relationship::SequentialBefore) {
        if (auto prevFunc = moduleLike.lookupFunctionLike(prevFuncName))
          precedingConflictingFuncs.push_back(prevFunc);
      }
    }

    if (failed(generateFunctionLogic(func, ctx, precedingConflictingFuncs)))
      return failure();
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Phase 3: Instance Creation with Topological Sorting
//===----------------------------------------------------------------------===//

/// Create a single hw.instance with muxed input ports.
///
/// PORT BINDING STRATEGY:
/// 1. Bare bindings (clock, reset): Direct connection from cmt2.instance args
/// 2. Method bindings: Mux all calls with fire conditions
/// 3. Unbound ports: Default to zero
static LogicalResult createSingleInstance(StringAttr instanceName,
                                          ModuleConversionContext &ctx) {
  LLVM_DEBUG(llvm::dbgs() << "Creating instance @" << instanceName.getValue() << "\n");

  auto *deferredInfo = ctx.instanceTracker.getDeferredInstance(instanceName);
  if (!deferredInfo)
    return ctx.cmt2Module.emitError("deferred instance not found: ") << instanceName;

  auto instOp = deferredInfo->cmt2Instance;
  auto extModuleOp = deferredInfo->extModuleOp;
  auto hwModuleOp = deferredInfo->hwModuleOp;
  const auto &baseArgs = deferredInfo->baseArgs;

  auto loc = instOp.getLoc();
  // Note: insertion point is managed by caller (createInstancesInTopologicalOrder)

  // Create mapping from ExtModuleHwOp arguments to actual values
  IRMapping instanceArgMapping;
  auto extModuleArgs = extModuleOp.getBody().getArguments();
  for (auto [extArg, actualArg] : llvm::zip(extModuleArgs, baseArgs))
    instanceArgMapping.map(extArg, actualArg);

  // Build input values with muxing
  SmallVector<Value> inputValues;
  auto ports = hwModuleOp.getPortList();

  for (const auto &port : ports) {
    if (port.dir != hw::ModulePort::Direction::Input)
      continue;

    auto portName = port.name.getValue();
    bool foundBinding = false;

    // Check bare bindings (clock, reset, etc.)
    extModuleOp.walk([&](cmt2::BindBareOp bindOp) {
      if (bindOp.getPort() == portName) {
        auto signal = bindOp.getSignal();
        inputValues.push_back(instanceArgMapping.lookupOrDefault(signal));
        foundBinding = true;
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });

    if (foundBinding)
      continue;

    // Check method/value bindings
    extModuleOp.walk([&](Operation *op) {
      if (auto bindMethod = dyn_cast<cmt2::BindMethodOp>(op)) {
        auto methodName = bindMethod.getSymNameAttr();
        auto calls = ctx.portTracker.getCalls(instanceName.getValue(), methodName.getValue());

        // Check enable port
        if (auto enableAttr = bindMethod.getEnable()) {
          if (*enableAttr == portName) {
            if (!calls.empty()) {
              SmallVector<Value> conditions, enables;
              for (const auto &call : calls) {
                conditions.push_back(call.fireCondition);
                enables.push_back(call.fireCondition);
              }
              Value defaultEnable = createI1Constant(ctx.builder, loc, false);
              inputValues.push_back(createMux(ctx.builder, loc, conditions, enables, defaultEnable));
            } else {
              inputValues.push_back(createI1Constant(ctx.builder, loc, false));
            }
            foundBinding = true;
            return WalkResult::interrupt();
          }
        }

        // Check input argument ports
        auto inputAttrs = bindMethod.getInputs();
        for (size_t argIdx = 0; argIdx < inputAttrs.size(); ++argIdx) {
          if (llvm::cast<FlatSymbolRefAttr>(inputAttrs[argIdx]).getValue() == portName) {
            if (!calls.empty()) {
              SmallVector<Value> conditions, args;
              for (const auto &call : calls) {
                conditions.push_back(call.fireCondition);
                auto mappedArgs = call.getMappedArgs(); // Dynamic lookup!
                if (argIdx < mappedArgs.size())
                  args.push_back(mappedArgs[argIdx]);
                else
                  args.push_back(ctx.builder.create<hw::ConstantOp>(loc, port.type, 0));
              }
              Value defaultArg = ctx.builder.create<hw::ConstantOp>(loc, port.type, 0);
              inputValues.push_back(createMux(ctx.builder, loc, conditions, args, defaultArg));
            } else {
              inputValues.push_back(ctx.builder.create<hw::ConstantOp>(loc, port.type, 0));
            }
            foundBinding = true;
            return WalkResult::interrupt();
          }
        }
      }
      return WalkResult::advance();
    });

    if (!foundBinding) {
      return instOp.emitError("no binding found for port '")
             << portName << "' of module " << hwModuleOp.getSymName()
             << " in instance @" << instanceName.getValue();
    }
  }

  // Create hw.instance
  auto hwInstance = ctx.builder.create<hw::InstanceOp>(
      loc, hwModuleOp, instOp.getSymName(), inputValues,
      ctx.builder.getArrayAttr({}), nullptr);

  // Mark as created and replace placeholders
  ctx.instanceTracker.markInstanceCreated(instanceName, hwInstance, ctx.globalMapping);
  ctx.signalTracker.replacePlaceholders(instanceName, hwInstance, extModuleOp, hwModuleOp);

  return success();
}

/// Create instances in topological order using ready queue algorithm.
///
/// TOPOLOGICAL SORTING WITH READY QUEUE:
/// 1. Build dependency map: instanceName -> {instances it depends on}
/// 2. Initialize ready queue with instances having no dependencies
/// 3. Process ready queue:
///    - Create instance
///    - Remove it from other instances' dependency sets
///    - Add newly-ready instances to queue
/// 4. Check for cyclic dependencies if not all instances created
static LogicalResult createInstancesInTopologicalOrder(ModuleConversionContext &ctx) {
  LLVM_DEBUG(llvm::dbgs() << "Creating instances in topological order...\n");

  auto deferredNames = ctx.instanceTracker.getDeferredInstanceNames();
  if (deferredNames.empty())
    return success();

  // Build dependency map
  DenseMap<StringAttr, DenseSet<StringAttr>> dependencies;
  const auto &valueToInstance = ctx.instanceTracker.getValueToInstanceMap();

  for (auto instanceName : deferredNames) {
    DenseSet<StringAttr> deps;
    ctx.portTracker.getInstanceDependencies(instanceName, deps, valueToInstance);

    // Filter to only deferred instances
    DenseSet<StringAttr> filteredDeps;
    for (auto dep : deps) {
      if (!ctx.instanceTracker.isInstanceCreated(dep))
        filteredDeps.insert(dep);
    }
    dependencies[instanceName] = std::move(filteredDeps);
  }

  // Initialize ready queue
  SmallVector<StringAttr> readyQueue;
  for (auto instanceName : deferredNames) {
    if (dependencies[instanceName].empty())
      readyQueue.push_back(instanceName);
  }

  // Process in topological order
  DenseSet<StringAttr> created;
  while (!readyQueue.empty()) {
    auto instanceName = readyQueue.pop_back_val();

    LLVM_DEBUG(llvm::dbgs() << "  Processing instance @" << instanceName.getValue() << "\n");

    // Create instance (getMappedArgs() gets current values dynamically)
    if (failed(createSingleInstance(instanceName, ctx)))
      return failure();

    created.insert(instanceName);

    // Update ready queue
    for (auto otherName : deferredNames) {
      if (created.count(otherName) > 0)
        continue;

      auto &deps = dependencies[otherName];
      deps.erase(instanceName);

      if (deps.empty()) {
        // Check if already in queue to avoid duplicates
        bool alreadyInQueue = llvm::is_contained(readyQueue, otherName);
        if (!alreadyInQueue)
          readyQueue.push_back(otherName);
      }
    }
  }

  // Check for cyclic dependencies
  if (created.size() != deferredNames.size()) {
    SmallVector<StringRef> uncreatedNames;
    for (auto name : deferredNames) {
      if (created.count(name) == 0)
        uncreatedNames.push_back(name.getValue());
    }
    return ctx.cmt2Module.emitError("cyclic dependency among instances: ")
           << llvm::join(uncreatedNames, ", ");
  }

  LLVM_DEBUG(llvm::dbgs() << "  Created " << created.size() << " instances\n");
  return success();
}

//===----------------------------------------------------------------------===//
// Module Conversion Entry Point
//===----------------------------------------------------------------------===//

/// Convert a Cmt2 ModuleOp to hw.module.
///
/// CONVERSION STEPS:
/// 1. Create hw.module with ports (module args + method/value enable/ready)
/// 2. Register deferred instances
/// 3. Convert schedule groups (generates function logic, tracks calls)
/// 4. Create instances in topological order with muxed inputs
/// 5. Connect output ports
static LogicalResult convertModule(cmt2::ModuleOp cmt2Module, OpBuilder &builder,
                                   const cmt2::SchedulerAnalysis &schedulerAnalysis,
                                   const cmt2::ConflictMatrixAnalysis &conflictAnalysis) {
  LLVM_DEBUG(llvm::dbgs() << "Converting module @" << cmt2Module.getSymName() << "\n");

  // Get schedule
  auto *moduleSchedule = schedulerAnalysis.getModuleSchedule(cmt2Module.getSymNameAttr());
  if (!moduleSchedule) {
    return cmt2Module.emitError("no schedule found - run scheduler analysis and "
                                "private function inlining first");
  }

  // Build hw.module ports
  SmallVector<hw::PortInfo> ports;
  auto argNamesAttr = cmt2Module.getArgNames();
  for (auto [idx, arg] : llvm::enumerate(cmt2Module.getBody().getArguments())) {
    StringRef portName = llvm::cast<StringAttr>(argNamesAttr[idx]).getValue();
    ports.push_back({{builder.getStringAttr(portName), arg.getType(),
                      hw::ModulePort::Direction::Input}});
  }

  // Add method/value ports
  SmallVector<cmt2::Cmt2FunctionLike> functions;
  cmt2Module.walk([&](Operation *op) {
    if (auto func = llvm::dyn_cast<cmt2::Cmt2FunctionLike>(op)) {
      auto funcKind = func.getFunctionKind();
      if (funcKind == FunctionKind::Method || funcKind == FunctionKind::Value)
        functions.push_back(func);
    }
  });

  auto i1Type = builder.getI1Type();
  for (auto func : functions) {
    auto funcKind = func.getFunctionKind();
    std::string baseName = func.functionName().str();

    std::string readyName = baseName + "_ready";
    if (auto customReady = func.getReadyName(); !customReady.empty())
      readyName = customReady.str();

    std::string enableName = baseName + "_enable";
    if (auto customEnable = func.getEnableName(); !customEnable.empty())
      enableName = customEnable.str();

    if (funcKind == FunctionKind::Method) {
      ports.push_back({{builder.getStringAttr(enableName), i1Type,
                        hw::ModulePort::Direction::Input}});
      ports.push_back({{builder.getStringAttr(readyName), i1Type,
                        hw::ModulePort::Direction::Output}});
    } else if (funcKind == FunctionKind::Value) {
      ports.push_back({{builder.getStringAttr(readyName), i1Type,
                        hw::ModulePort::Direction::Output}});
    }
  }

  // Create hw.module
  hw::ModulePortInfo portInfo(ports);
  auto loc = cmt2Module.getLoc();
  auto hwModule = builder.create<hw::HWModuleOp>(
      loc, builder.getStringAttr(cmt2Module.getSymName()), portInfo);

  // Map module arguments
  IRMapping globalMapping;
  for (auto [cmt2Arg, hwArg] : llvm::zip(cmt2Module.getBody().getArguments(),
                                          hwModule.getBodyBlock()->getArguments())) {
    globalMapping.map(cmt2Arg, hwArg);
  }

  // Set up conversion context
  SignalTracker signalTracker;
  InstanceTracker instanceTracker;
  PortConnectionTracker portTracker;
  ModuleConversionContext ctx(cmt2Module, hwModule, builder, signalTracker,
                              instanceTracker, portTracker,
                              schedulerAnalysis, conflictAnalysis,
                              cmt2::CallInfoView(cmt2Module->getParentOfType<cmt2::CircuitOp>()),
                              globalMapping);

  ctx.builder.setInsertionPoint(hwModule.getBodyBlock()->getTerminator());

  // THREE-PHASE CONVERSION
  // Phase 1: Register deferred instances
  if (failed(registerDeferredInstances(ctx)))
    return failure();

  // Phase 2: Generate function logic and track calls
  for (const auto &group : moduleSchedule->getGroups()) {
    if (failed(convertScheduleGroup(group, ctx)))
      return failure();
  }

  // Phase 3: Create instances in topological order
  // Instances use fire signals from Phase 2, so must come after that logic
  ctx.builder.setInsertionPoint(hwModule.getBodyBlock()->getTerminator());
  if (failed(createInstancesInTopologicalOrder(ctx)))
    return failure();

  // Connect output ports
  SmallVector<Value> outputValues;
  for (auto func : functions) {
    auto funcKind = func.getFunctionKind();
    auto funcNameAttr = func.functionNameAttr();

    if (funcKind == FunctionKind::Method || funcKind == FunctionKind::Value) {
      if (auto ready = signalTracker.getReadySignal(funcNameAttr))
        outputValues.push_back(ready);
      else
        outputValues.push_back(createI1Constant(builder, loc, false));
    }
  }

  hwModule.getBodyBlock()->getTerminator()->setOperands(outputValues);
  return success();
}

//===----------------------------------------------------------------------===//
// Pass Implementation
//===----------------------------------------------------------------------===//

struct Cmt2ToHWPass : public circt::impl::Cmt2ToHWBase<Cmt2ToHWPass> {
  void runOnOperation() override {
    mlir::ModuleOp mlirModule = getOperation();

    // Find Cmt2 circuit
    cmt2::CircuitOp circuitOp;
    mlirModule.walk([&](cmt2::CircuitOp op) {
      if (!circuitOp)
        circuitOp = op;
    });

    if (!circuitOp) {
      LLVM_DEBUG(llvm::dbgs() << "No cmt2.circuit found\n");
      return;
    }

    // Run analyses
    cmt2::SchedulerAnalysis schedulerAnalysis(circuitOp);
    cmt2::ConflictMatrixAnalysis conflictAnalysis(circuitOp);

    // Convert each module
    OpBuilder builder(&getContext());
    builder.setInsertionPointAfter(circuitOp);

    SmallVector<cmt2::ModuleOp> modulesToConvert;
    circuitOp.walk([&](cmt2::ModuleOp module) {
      modulesToConvert.push_back(module);
    });

    for (auto module : modulesToConvert) {
      if (failed(convertModule(module, builder, schedulerAnalysis, conflictAnalysis))) {
        signalPassFailure();
        return;
      }
    }

    LLVM_DEBUG(llvm::dbgs() << "Cmt2ToHW conversion completed\n");
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// Pass Creation
//===----------------------------------------------------------------------===//

std::unique_ptr<mlir::Pass> circt::createCmt2ToHWPass() {
  return std::make_unique<Cmt2ToHWPass>();
}

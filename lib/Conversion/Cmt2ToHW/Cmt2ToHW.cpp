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

//===----------------------------------------------------------------------===//
// Conversion Infrastructure
//===----------------------------------------------------------------------===//

namespace {

/// Represents a single call to a method/value with its fire condition and arguments
struct MethodCall {
  Value fireCondition;              // When this call is active
  SmallVector<Value> originalArgs;  // Original arguments (before mapping)
  // Store operations and result indices instead of Values (Values become stale after replacement)
  SmallVector<std::pair<Operation*, unsigned>> argOps;
  Location loc;                     // Source location

  /// Get current mapped arguments by looking up operations
  SmallVector<Value> getMappedArgs() const {
    SmallVector<Value> result;
    for (auto [op, resultIdx] : argOps) {
      if (op && resultIdx < op->getNumResults()) {
        result.push_back(op->getResult(resultIdx));
      }
    }
    return result;
  }

  /// Get all instances that this call depends on (instances whose results are used as args)
  void getDependentInstances(DenseSet<StringAttr> &deps,
                             const DenseMap<Value, StringAttr> &valueToInstance) const {
    // Check dependencies using current mapped args
    for (auto [op, resultIdx] : argOps) {
      if (op && resultIdx < op->getNumResults()) {
        Value arg = op->getResult(resultIdx);
        if (auto it = valueToInstance.find(arg); it != valueToInstance.end()) {
          deps.insert(it->second);
        }
      }
    }
  }
};

/// Tracks multiple calls to the same instance.method for muxing
class PortConnectionTracker {
public:
  /// Register a call to an instance's method/value
  void registerCall(StringRef instanceName, StringRef methodName,
                    Value fireCondition, ArrayRef<Value> originalArgs,
                    ArrayRef<Value> mappedArgs, Location loc) {
    auto key = (instanceName.str() + "." + methodName.str());

    // Store operation/result pairs for each mapped arg
    SmallVector<std::pair<Operation*, unsigned>> argOps;
    for (auto mappedArg : mappedArgs) {
      if (auto defOp = mappedArg.getDefiningOp()) {
        // Find which result index this is
        unsigned resultIdx = 0;
        for (auto result : defOp->getResults()) {
          if (result == mappedArg) {
            argOps.push_back({defOp, resultIdx});
            break;
          }
          resultIdx++;
        }
      } else {
        // This is a block argument, not an operation result
        argOps.push_back({nullptr, 0});
      }
    }

    calls[key].push_back({fireCondition,
                          SmallVector<Value>(originalArgs),
                          argOps,
                          loc});
  }

  /// Get all calls for a given instance.method
  ArrayRef<MethodCall> getCalls(StringRef instanceName, StringRef methodName) const {
    auto key = (instanceName.str() + "." + methodName.str());
    auto it = calls.find(key);
    return it != calls.end() ? ArrayRef<MethodCall>(it->second) : ArrayRef<MethodCall>();
  }

  /// Check if there are any calls for instance.method
  bool hasCalls(StringRef instanceName, StringRef methodName) const {
    auto key = (instanceName.str() + "." + methodName.str());
    return calls.find(key) != calls.end();
  }

  /// Get all instances that a given instance depends on (uses results from)
  void getInstanceDependencies(StringAttr instanceName,
                               DenseSet<StringAttr> &deps,
                               const DenseMap<Value, StringAttr> &valueToInstance) const {
    // Iterate all calls to this instance's methods/values
    for (const auto &entry : calls) {
      StringRef key = entry.first();
      // Check if this call is to instanceName
      if (key.starts_with((instanceName.getValue() + ".").str())) {
        // Check dependencies in the call arguments
        for (const auto &call : entry.second) {
          call.getDependentInstances(deps, valueToInstance);
        }
      }
    }
  }

private:
  llvm::StringMap<SmallVector<MethodCall>> calls;
};

/// Helper class for tracking signal names and values during conversion
class SignalTracker {
public:
  SignalTracker() = default;

  /// Register a ready signal for a function
  void registerReadySignal(StringAttr funcName, Value readySignal) {
    readySignals[funcName] = readySignal;
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

  /// Register a placeholder constant that should be replaced with instance result
  void registerPlaceholder(Value placeholder, StringAttr instanceName,
                          StringAttr methodName, size_t resultIdx) {
    placeholders[placeholder] = {instanceName, methodName, resultIdx};
  }

  /// Replace placeholders with actual instance results
  void replacePlaceholders(StringAttr instanceName, hw::InstanceOp hwInstance,
                          cmt2::ExtModuleHwOp extModuleOp, hw::HWModuleOp hwModuleOp) {
    SmallVector<std::pair<Value, Value>> replacements;

    for (const auto &entry : placeholders) {
      if (entry.second.instanceName != instanceName)
        continue;

      Value placeholder = entry.first;
      auto methodName = entry.second.methodName;
      size_t resultIdx = entry.second.resultIdx;

      // Look up the bind operation
      auto bindOp = extModuleOp.lookupSymbol(methodName);
      if (!bindOp)
        continue;

      // Find the corresponding hw.instance output
      Value actualResult;
      if (auto bindValue = dyn_cast<cmt2::BindValueOp>(bindOp)) {
        auto dataAttrs = bindValue.getData();
        if (resultIdx < dataAttrs.size()) {
          auto portName = llvm::cast<FlatSymbolRefAttr>(dataAttrs[resultIdx]).getValue();
          auto ports = hwModuleOp.getPortList();
          size_t outputIdx = 0;
          for (const auto &port : ports) {
            if (port.dir == hw::ModulePort::Direction::Output) {
              if (port.name.getValue() == portName) {
                actualResult = hwInstance.getResult(outputIdx);
                break;
              }
              outputIdx++;
            }
          }
        }
      } else if (auto bindMethod = dyn_cast<cmt2::BindMethodOp>(bindOp)) {
        auto outputAttrs = bindMethod.getOutputs();
        if (resultIdx < outputAttrs.size()) {
          auto portName = llvm::cast<FlatSymbolRefAttr>(outputAttrs[resultIdx]).getValue();
          auto ports = hwModuleOp.getPortList();
          size_t outputIdx = 0;
          for (const auto &port : ports) {
            if (port.dir == hw::ModulePort::Direction::Output) {
              if (port.name.getValue() == portName) {
                actualResult = hwInstance.getResult(outputIdx);
                break;
              }
              outputIdx++;
            }
          }
        }
      }

      if (actualResult) {
        replacements.push_back({placeholder, actualResult});
      }
    }

    // Perform replacements
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

  DenseMap<StringAttr, Value> readySignals;
  DenseMap<StringAttr, Value> fireSignals;
  DenseMap<StringAttr, SmallVector<Value>> bodyResults;
  DenseMap<Value, PlaceholderInfo> placeholders;
};

/// Information for deferred instance creation
struct DeferredInstanceInfo {
  cmt2::InstanceOp cmt2Instance;
  cmt2::ExtModuleHwOp extModuleOp;
  hw::HWModuleOp hwModuleOp;
  SmallVector<Value> baseArgs; // Arguments from cmt2.instance (mapped through globalMapping)
};

/// Tracks instances and their hw.instance operations
class InstanceTracker {
public:
  /// Register a deferred instance (before creation)
  void registerDeferredInstance(StringAttr instanceName, DeferredInstanceInfo info) {
    deferredInstances[instanceName] = std::move(info);
  }

  /// Get deferred instance info
  const DeferredInstanceInfo *getDeferredInstance(StringAttr instanceName) const {
    auto it = deferredInstances.find(instanceName);
    return it != deferredInstances.end() ? &it->second : nullptr;
  }

  /// Get all deferred instance names
  SmallVector<StringAttr> getDeferredInstanceNames() const {
    SmallVector<StringAttr> names;
    for (const auto &entry : deferredInstances) {
      names.push_back(entry.first);
    }
    return names;
  }

  /// Mark instance as created and register result mappings
  void markInstanceCreated(StringAttr instanceName, hw::InstanceOp hwInstance,
                          IRMapping &globalMapping) {
    instances[instanceName] = hwInstance;

    // Register all output values from this instance into globalMapping
    for (auto result : hwInstance.getResults()) {
      valueToInstance[result] = instanceName;
      // Also add to globalMapping for future remapping
      globalMapping.map(result, result);
    }

    deferredInstances.erase(instanceName);
  }

  /// Check if instance has been created
  bool isInstanceCreated(StringAttr instanceName) const {
    return instances.count(instanceName) > 0;
  }

  /// Get hw.instance for a cmt2 instance (if created)
  hw::InstanceOp getInstance(StringAttr instanceName) const {
    auto it = instances.find(instanceName);
    return it != instances.end() ? it->second : hw::InstanceOp();
  }

  /// Register value -> instance mapping (for dependency tracking)
  void registerInstanceResult(Value result, StringAttr instanceName) {
    valueToInstance[result] = instanceName;
  }

  /// Get the instance that produces a value
  StringAttr getInstanceForValue(Value val) const {
    auto it = valueToInstance.find(val);
    return it != valueToInstance.end() ? it->second : StringAttr();
  }

  /// Get value->instance map for dependency analysis
  const DenseMap<Value, StringAttr> &getValueToInstanceMap() const {
    return valueToInstance;
  }

private:
  DenseMap<StringAttr, DeferredInstanceInfo> deferredInstances;
  DenseMap<StringAttr, hw::InstanceOp> instances;
  DenseMap<Value, StringAttr> valueToInstance;
};

/// Context for converting a single Cmt2 module
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
  IRMapping &globalMapping; // Maps cmt2 module arguments to hw module arguments

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
// Helper Functions
//===----------------------------------------------------------------------===//

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

/// Create a mux tree for selecting among multiple values based on conditions
static Value createMux(OpBuilder &builder, Location loc,
                       ArrayRef<Value> conditions, ArrayRef<Value> values,
                       Value defaultValue) {
  assert(conditions.size() == values.size() && "Mismatch in condition/value count");

  if (conditions.empty())
    return defaultValue;

  Value result = defaultValue;
  // Build mux chain from back to front: cond[n] ? val[n] : (cond[n-1] ? val[n-1] : ...)
  for (int i = conditions.size() - 1; i >= 0; --i) {
    result = builder.create<comb::MuxOp>(loc, conditions[i], values[i], result, false);
  }
  return result;
}

//===----------------------------------------------------------------------===//
// Instance Conversion
//===----------------------------------------------------------------------===//

/// Register instances for deferred creation (does not create hw.instance yet)
static LogicalResult registerDeferredInstances(ModuleConversionContext &ctx) {
  LLVM_DEBUG(llvm::dbgs() << "Registering deferred instances...\n");

  // Walk all instances in the cmt2 module
  WalkResult walkResult = ctx.cmt2Module.walk([&](cmt2::InstanceOp instOp) {
    LLVM_DEBUG(llvm::dbgs() << "  Registering instance @" << instOp.getSymName() << "\n");

    // Look up the referenced module
    auto moduleName = instOp.getModuleName();
    auto circuitOp = ctx.cmt2Module->getParentOfType<cmt2::CircuitOp>();
    auto referencedOp = circuitOp.lookupSymbol(moduleName);

    if (!referencedOp) {
      instOp.emitError("referenced module not found: ") << moduleName;
      return WalkResult::interrupt();
    }

    // Check if this is an external hw module
    auto extModuleOp = dyn_cast<cmt2::ExtModuleHwOp>(referencedOp);
    if (!extModuleOp) {
      // For regular cmt2 modules, we would need to handle them after conversion
      // For now, skip non-external modules
      LLVM_DEBUG(llvm::dbgs() << "    Skipping non-external module instance\n");
      return WalkResult::advance();
    }

    // Get the hw module name from ExtModuleHwOp
    auto hwModuleName = extModuleOp.getExtModuleName();

    // Find the hw.module to get port information
    auto mlirModule = circuitOp->getParentOfType<mlir::ModuleOp>();
    auto hwModuleOp = mlirModule.lookupSymbol<hw::HWModuleOp>(hwModuleName);

    if (!hwModuleOp) {
      instOp.emitError("hw module not found: ") << hwModuleName;
      return WalkResult::interrupt();
    }

    // Map base instance arguments (from cmt2.instance)
    SmallVector<Value> baseArgs;
    for (auto arg : instOp.getArgs()) {
      baseArgs.push_back(ctx.globalMapping.lookupOrDefault(arg));
    }

    // Register deferred instance info
    DeferredInstanceInfo info{instOp, extModuleOp, hwModuleOp, baseArgs};
    ctx.instanceTracker.registerDeferredInstance(instOp.getSymNameAttr(), std::move(info));

    return WalkResult::advance();
  });

  return walkResult.wasInterrupted() ? failure() : success();
}

//===----------------------------------------------------------------------===//
// Call Conversion
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

/// Convert a CallOp during region cloning
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
    // Calling a function in an instance - ready signal will be wired from instance
    // For now, assume ready (will be connected properly later)
    readySignal = createI1Constant(ctx.builder, loc, true);
  }

  if (readySignal)
    readySignalsUsed.push_back(readySignal);

  // Register call for muxing (if it's to an external instance and has a fire signal)
  if (!isThisCall && fireSignal) {
    // Store both original arguments (for remapping) and mapped arguments (for immediate use)
    SmallVector<Value> originalArgs(callOp.getInputs().begin(), callOp.getInputs().end());
    SmallVector<Value> mappedArgs;
    for (auto arg : callOp.getInputs()) {
      Value mappedArg = mapping.lookupOrDefault(arg);
      // Ensure the mapped value is valid
      if (!mappedArg) {
        return callOp.emitError("failed to map argument for call - value is null");
      }
      mappedArgs.push_back(mappedArg);
    }

    ctx.portTracker.registerCall(calleeName.getValue(), methodName.getValue(),
                                  fireSignal, originalArgs, mappedArgs, loc);
  }

  // Map the call results
  // For @this calls, we can wire to the actual body results
  // For external calls, we need to look up results from hw.instance (if created)
  if (isThisCall) {
    // Get the body results from the signal tracker
    auto bodyResults = ctx.signalTracker.getBodyResults(methodName);
    if (bodyResults.size() == callOp.getResults().size()) {
      // Wire the call results to the function's body results
      for (auto [callResult, bodyResult] : llvm::zip(callOp.getResults(), bodyResults)) {
        mapping.map(callResult, bodyResult);
      }
    } else {
      // Mismatch in result count - create placeholder constants
      for (auto result : callOp.getResults()) {
        auto constZero = ctx.builder.create<hw::ConstantOp>(loc, result.getType(), 0);
        mapping.map(result, constZero);
      }
    }
  } else {
    // For external instance calls, check if instance has been created
    auto hwInstance = ctx.instanceTracker.getInstance(calleeName);
    if (hwInstance) {
      // Instance exists - wire results from hw.instance output ports
      auto circuitOp = ctx.cmt2Module->getParentOfType<cmt2::CircuitOp>();
      auto cmt2Instance = ctx.cmt2Module.lookupSymbol<cmt2::InstanceOp>(calleeName);
      if (cmt2Instance) {
        auto extModuleOp = circuitOp.lookupSymbol<cmt2::ExtModuleHwOp>(
            cmt2Instance.getModuleName());
        if (extModuleOp) {
          // Look up the bind operation for this method
          auto bindOp = extModuleOp.lookupSymbol(methodName);

          if (auto bindValue = dyn_cast_or_null<cmt2::BindValueOp>(bindOp)) {
            // Map value results from hw.instance outputs
            auto dataAttrs = bindValue.getData();
            size_t resultIdx = 0;
            for (auto dataAttr : dataAttrs) {
              auto portName = llvm::cast<FlatSymbolRefAttr>(dataAttr).getValue();
              // Find the output port index in hw.instance
              auto mlirModule = circuitOp->getParentOfType<mlir::ModuleOp>();
              auto hwModuleOp = mlirModule.lookupSymbol<hw::HWModuleOp>(hwInstance.getModuleName());
              if (hwModuleOp) {
                auto ports = hwModuleOp.getPortList();
                size_t outputIdx = 0;
                for (const auto &port : ports) {
                  if (port.dir == hw::ModulePort::Direction::Output) {
                    if (port.name.getValue() == portName && resultIdx < callOp.getResults().size()) {
                      // Map the call result to the instance output
                      Value instanceOutput = hwInstance.getResult(outputIdx);
                      mapping.map(callOp.getResults()[resultIdx], instanceOutput);
                      // Track that this value comes from this instance
                      ctx.instanceTracker.registerInstanceResult(instanceOutput, calleeName);
                      resultIdx++;
                      break;
                    }
                    outputIdx++;
                  }
                }
              }
            }
          } else if (auto bindMethod = dyn_cast_or_null<cmt2::BindMethodOp>(bindOp)) {
            // Map method results from hw.instance outputs
            auto outputAttrs = bindMethod.getOutputs();
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
          }
        }
      }
    }

    // For any unmapped results, create placeholder constants
    // (This happens when instance not yet created - will be fixed in later pass)
    for (auto [idx, result] : llvm::enumerate(callOp.getResults())) {
      if (!mapping.contains(result)) {
        auto constZero = ctx.builder.create<hw::ConstantOp>(loc, result.getType(), 0);
        mapping.map(result, constZero);
        // Register placeholder for later replacement
        ctx.signalTracker.registerPlaceholder(constZero, calleeName, methodName, idx);
      }
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Function Logic Generation
//===----------------------------------------------------------------------===//

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

  // Step 5: Generate enable signal (for methods only)
  // Enable comes from module input port
  Value enableSignal;
  if (funcKind == FunctionKind::Method) {
    std::string enableName = ctx.getEnableSignalName(func);
    // Look up enable signal from module inputs
    auto hwModuleInputs = ctx.hwModule.getBodyBlock()->getArguments();
    auto ports = ctx.hwModule.getPortList();
    size_t inputIdx = 0;
    for (const auto &port : ports) {
      if (port.dir == hw::ModulePort::Direction::Input) {
        if (port.name.getValue().str() == enableName && inputIdx < hwModuleInputs.size()) {
          enableSignal = hwModuleInputs[inputIdx];
          break;
        }
        inputIdx++;
      }
    }
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

  // Step 7: Clone body logic
  IRMapping bodyMapping;
  // Start with the global mapping (module arguments)
  bodyMapping = ctx.globalMapping;
  SmallVector<Value> bodyCallReadySignals; // Not used here, already collected above
  SmallVector<Value> bodyResults;

  for (auto &op : bodyBlock.without_terminator()) {
    if (auto callOp = dyn_cast<cmt2::CallOp>(&op)) {
      // Convert call operations with fire signal for registering calls
      if (failed(convertCallOp(callOp, ctx, bodyMapping, bodyCallReadySignals, fireSignal)))
        return failure();
    } else {
      // Clone other operations directly
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
// Instance Creation with Port Wiring
//===----------------------------------------------------------------------===//

/// Create a single hw.instance with muxed input ports
static LogicalResult createSingleInstance(StringAttr instanceName,
                                          ModuleConversionContext &ctx) {
  LLVM_DEBUG(llvm::dbgs() << "Creating instance @" << instanceName.getValue() << "\n");

  auto *deferredInfo = ctx.instanceTracker.getDeferredInstance(instanceName);
  if (!deferredInfo) {
    return ctx.cmt2Module.emitError("deferred instance not found: ") << instanceName;
  }

  auto instOp = deferredInfo->cmt2Instance;
  auto extModuleOp = deferredInfo->extModuleOp;
  auto hwModuleOp = deferredInfo->hwModuleOp;
  const auto &baseArgs = deferredInfo->baseArgs;

  auto loc = instOp.getLoc();
  ctx.builder.setInsertionPoint(ctx.hwModule.getBodyBlock()->getTerminator());

  // Create mapping from ExtModuleHwOp arguments to actual values
  IRMapping instanceArgMapping;
  auto extModuleArgs = extModuleOp.getBody().getArguments();
  for (auto [extArg, actualArg] : llvm::zip(extModuleArgs, baseArgs)) {
    instanceArgMapping.map(extArg, actualArg);
  }

  // Build input values for hw.instance with muxing
  SmallVector<Value> inputValues;
  auto ports = hwModuleOp.getPortList();

  for (const auto &port : ports) {
    if (port.dir != hw::ModulePort::Direction::Input)
      continue;

    auto portName = port.name.getValue();
    bool foundBinding = false;

    // Check for bare bindings (clock, reset, etc.)
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

    // Check for method/value bindings
    extModuleOp.walk([&](Operation *op) {
      if (auto bindMethod = dyn_cast<cmt2::BindMethodOp>(op)) {
        // Check enable port
        if (auto enableAttr = bindMethod.getEnable()) {
          if (*enableAttr == portName) {
            auto methodName = bindMethod.getSymNameAttr();
            auto calls = ctx.portTracker.getCalls(instanceName.getValue(), methodName.getValue());

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

        // Check input ports
        auto inputAttrs = bindMethod.getInputs();
        for (size_t argIdx = 0; argIdx < inputAttrs.size(); ++argIdx) {
          if (llvm::cast<FlatSymbolRefAttr>(inputAttrs[argIdx]).getValue() == portName) {
            auto methodName = bindMethod.getSymNameAttr();
            auto calls = ctx.portTracker.getCalls(instanceName.getValue(), methodName.getValue());

            if (!calls.empty()) {
              SmallVector<Value> conditions, args;
              for (const auto &call : calls) {
                conditions.push_back(call.fireCondition);
                auto mappedArgs = call.getMappedArgs();
                if (argIdx < mappedArgs.size()) {
                  args.push_back(mappedArgs[argIdx]);
                } else {
                  args.push_back(ctx.builder.create<hw::ConstantOp>(loc, port.type, 0));
                }
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
      // No binding - use default zero
      inputValues.push_back(ctx.builder.create<hw::ConstantOp>(loc, port.type, 0));
    }
  }

  // Create the hw.instance
  auto hwInstance = ctx.builder.create<hw::InstanceOp>(
      loc, hwModuleOp, instOp.getSymName(), inputValues,
      ctx.builder.getArrayAttr({}), nullptr);

  // Mark instance as created and register result tracking (updates globalMapping)
  ctx.instanceTracker.markInstanceCreated(instanceName, hwInstance, ctx.globalMapping);

  // Replace placeholder constants with actual instance results
  ctx.signalTracker.replacePlaceholders(instanceName, hwInstance, extModuleOp, hwModuleOp);

  return success();
}

/// Create instances in topological order using ready queue
static LogicalResult createInstancesInTopologicalOrder(ModuleConversionContext &ctx) {
  LLVM_DEBUG(llvm::dbgs() << "Creating instances in topological order...\n");

  auto deferredNames = ctx.instanceTracker.getDeferredInstanceNames();
  if (deferredNames.empty()) {
    LLVM_DEBUG(llvm::dbgs() << "  No instances to create\n");
    return success();
  }

  // Build dependency map: instanceName -> set of instances it depends on
  DenseMap<StringAttr, DenseSet<StringAttr>> dependencies;
  const auto &valueToInstance = ctx.instanceTracker.getValueToInstanceMap();

  for (auto instanceName : deferredNames) {
    DenseSet<StringAttr> deps;
    ctx.portTracker.getInstanceDependencies(instanceName, deps, valueToInstance);
    // Only keep dependencies that are also deferred instances (not already created)
    DenseSet<StringAttr> filteredDeps;
    for (auto dep : deps) {
      if (!ctx.instanceTracker.isInstanceCreated(dep)) {
        filteredDeps.insert(dep);
      }
    }
    dependencies[instanceName] = std::move(filteredDeps);
  }

  // Ready queue: instances with no unmet dependencies
  SmallVector<StringAttr> readyQueue;
  for (auto instanceName : deferredNames) {
    if (dependencies[instanceName].empty()) {
      readyQueue.push_back(instanceName);
    }
  }

  // Process instances in topological order
  DenseSet<StringAttr> created;
  while (!readyQueue.empty()) {
    // Pop from ready queue
    auto instanceName = readyQueue.pop_back_val();

    LLVM_DEBUG(llvm::dbgs() << "  Processing instance @" << instanceName.getValue() << "\n");

    // Create this instance (getMappedArgs() will get current values from operations)
    if (failed(createSingleInstance(instanceName, ctx))) {
      return failure();
    }

    created.insert(instanceName);

    // Check if any other instances are now ready
    for (auto otherName : deferredNames) {
      if (created.count(otherName) > 0)
        continue; // Already created

      auto &deps = dependencies[otherName];
      deps.erase(instanceName); // Remove this dependency

      if (deps.empty()) {
        // All dependencies met - add to ready queue (if not already in queue)
        bool alreadyInQueue = false;
        for (auto queuedName : readyQueue) {
          if (queuedName == otherName) {
            alreadyInQueue = true;
            break;
          }
        }
        if (!alreadyInQueue) {
          readyQueue.push_back(otherName);
        }
      }
    }
  }

  // Check if all instances were created
  if (created.size() != deferredNames.size()) {
    // Cyclic dependency detected
    SmallVector<StringRef> uncreatedNames;
    for (auto name : deferredNames) {
      if (created.count(name) == 0) {
        uncreatedNames.push_back(name.getValue());
      }
    }
    return ctx.cmt2Module.emitError("cyclic dependency among instances: ")
           << llvm::join(uncreatedNames, ", ");
  }

  LLVM_DEBUG(llvm::dbgs() << "  Created " << created.size() << " instances\n");
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

  // Add input/output ports for methods/values
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
      ports.push_back({{builder.getStringAttr(enableName), i1Type,
                        hw::ModulePort::Direction::Input}});
      ports.push_back({{builder.getStringAttr(readyName), i1Type,
                        hw::ModulePort::Direction::Output}});
    } else if (funcKind == FunctionKind::Value) {
      // Values have: ready (output)
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
  InstanceTracker instanceTracker;
  PortConnectionTracker portTracker;
  ModuleConversionContext ctx(cmt2Module, hwModule, builder, signalTracker,
                              instanceTracker, portTracker,
                              schedulerAnalysis, conflictAnalysis,
                              cmt2::CallInfoView(cmt2Module->getParentOfType<cmt2::CircuitOp>()),
                              globalMapping);

  // Move insertion point to body (before the auto-generated hw.output)
  ctx.builder.setInsertionPoint(hwModule.getBodyBlock()->getTerminator());

  // Step 1: Register deferred instances (no hw.instance created yet)
  if (failed(registerDeferredInstances(ctx)))
    return failure();

  // Step 2: Convert each schedule group (generates function logic and call tracking)
  for (const auto &group : moduleSchedule->getGroups()) {
    if (failed(convertScheduleGroup(group, ctx)))
      return failure();
  }

  // Step 3: Create instances in topological order with muxed inputs
  if (failed(createInstancesInTopologicalOrder(ctx)))
    return failure();

  // Step 4: Connect output ports with proper values
  // Collect all output values in the order they were added to ports
  SmallVector<Value> outputValues;

  for (auto func : functions) {
    auto funcKind = func.getFunctionKind();
    auto funcNameAttr = func.functionNameAttr();

    if (funcKind == FunctionKind::Method) {
      // Methods: enable (already an input), ready (output)
      if (auto ready = signalTracker.getReadySignal(funcNameAttr))
        outputValues.push_back(ready);
      else {
        // Create a default false ready signal if not found
        outputValues.push_back(createI1Constant(builder, loc, false));
      }
    } else if (funcKind == FunctionKind::Value) {
      // Values: ready (output)
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
  cmt2::ConflictMatrixAnalysis conflictAnalysis(circuitOp);

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

  LLVM_DEBUG(llvm::dbgs() << "Cmt2ToHW conversion completed\n");
}

} // namespace

//===----------------------------------------------------------------------===//
// Pass Creation
//===----------------------------------------------------------------------===//

std::unique_ptr<mlir::Pass> circt::createCmt2ToHWPass() {
  return std::make_unique<Cmt2ToHWPass>();
}

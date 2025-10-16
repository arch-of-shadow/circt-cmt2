//===- Cmt2ToFIRRTL.cpp - Cmt2 to FIRRTL conversion --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Cmt2 to FIRRTL conversion pass.
//
//===----------------------------------------------------------------------===//

#include "circt/Conversion/Cmt2ToFIRRTL.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Transforms/CallInfo.h"
#include "circt/Dialect/Cmt2/Transforms/ConflictMatrix.h"
#include "circt/Dialect/Cmt2/Transforms/Scheduler.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/ImplicitLocOpBuilder.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-to-firrtl"

using namespace mlir;
using namespace circt;
using namespace circt::cmt2;
using namespace circt::firrtl;

namespace circt {
#define GEN_PASS_DEF_LOWERCMT2TOFIRRTL
#include "circt/Conversion/Passes.h.inc"
} // namespace circt

namespace {

//===----------------------------------------------------------------------===//
// Helper Classes
//===----------------------------------------------------------------------===//

/// SignalTracker tracks generated signals (ready/enable/fire) and body results
/// for each function during conversion.
class SignalTracker {
public:
  SignalTracker() = default;

  /// Set the ready signal for a function
  void setReady(StringAttr funcName, Value ready) { readySignals[funcName] = ready; }

  /// Get the ready signal for a function
  Value getReady(StringAttr funcName) const {
    auto it = readySignals.find(funcName);
    return it != readySignals.end() ? it->second : Value();
  }

  /// Set the enable signal for a method
  void setEnable(StringAttr funcName, Value enable) { enableSignals[funcName] = enable; }

  /// Get the enable signal for a method
  Value getEnable(StringAttr funcName) const {
    auto it = enableSignals.find(funcName);
    return it != enableSignals.end() ? it->second : Value();
  }

  /// Set the fire signal for a function
  void setFire(StringAttr funcName, Value fire) { fireSignals[funcName] = fire; }

  /// Get the fire signal for a function
  Value getFire(StringAttr funcName) const {
    auto it = fireSignals.find(funcName);
    return it != fireSignals.end() ? it->second : Value();
  }

  /// Set body results for a function
  void setBodyResults(StringAttr funcName, ValueRange results) {
    bodyResults[funcName] = SmallVector<Value>(results.begin(), results.end());
  }

  /// Get body results for a function
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

/// ModuleConversionContext holds per-module state during conversion
class ModuleConversionContext {
public:
  ModuleConversionContext(cmt2::ModuleOp cmt2Module, FModuleOp firrtlModule,
                           const ModuleScheduleResult *schedule,
                           const ModuleConflictMatrix *conflictMatrix,
                           const ModuleCallInfo *callInfo)
      : cmt2Module(cmt2Module), firrtlModule(firrtlModule), schedule(schedule),
        conflictMatrix(conflictMatrix), callInfo(callInfo) {}

  cmt2::ModuleOp getCmt2Module() const { return cmt2Module; }
  FModuleOp getFIRRTLModule() const { return firrtlModule; }
  const ModuleScheduleResult *getSchedule() const { return schedule; }
  const ModuleConflictMatrix *getConflictMatrix() const { return conflictMatrix; }
  const ModuleCallInfo *getCallInfo() const { return callInfo; }

  SignalTracker &getSignalTracker() { return signalTracker; }
  IRMapping &getIRMapping() { return irMapping; }

  /// Track FIRRTL instance operations by their symbol name
  void registerInstance(StringAttr name, firrtl::InstanceOp firrtlInst) {
    instances[name] = firrtlInst;
  }

  /// Get a FIRRTL instance by symbol name
  firrtl::InstanceOp getInstance(StringAttr name) const {
    auto it = instances.find(name);
    return it != instances.end() ? it->second : firrtl::InstanceOp();
  }

private:
  cmt2::ModuleOp cmt2Module;
  FModuleOp firrtlModule;
  const ModuleScheduleResult *schedule;
  const ModuleConflictMatrix *conflictMatrix;
  const ModuleCallInfo *callInfo;
  SignalTracker signalTracker;
  IRMapping irMapping;
  DenseMap<StringAttr, firrtl::InstanceOp> instances;
};

//===----------------------------------------------------------------------===//
// Conversion Pass
//===----------------------------------------------------------------------===//

class LowerCmt2ToFIRRTLPass
    : public circt::impl::LowerCmt2ToFIRRTLBase<LowerCmt2ToFIRRTLPass> {
public:
  void runOnOperation() override;

private:
  LogicalResult convertCircuit(cmt2::CircuitOp circuit);
  LogicalResult convertModule(cmt2::ModuleOp module, firrtl::CircuitOp firrtlCircuit,
                                 const SchedulerAnalysis &scheduler,
                                 const ConflictMatrixAnalysis &conflictAnalysis,
                                 const CallInfoView &callInfo);

  LogicalResult createInstances(cmt2::ModuleOp module, ModuleConversionContext &ctx,
                                  ImplicitLocOpBuilder &builder);

  LogicalResult processFunction(Cmt2FunctionLike func, ModuleConversionContext &ctx,
                                  ImplicitLocOpBuilder &builder);

  LogicalResult cloneRegionOps(Region &sourceRegion,
                                ModuleConversionContext &ctx,
                                ImplicitLocOpBuilder &builder,
                                SmallVectorImpl<Value> &results);

  Value generateReadySignal(Cmt2FunctionLike func, Value guardResult,
                              const SmallVector<CallInfo> &calls,
                              const ScheduleGroup &group,
                              ModuleConversionContext &ctx,
                              ImplicitLocOpBuilder &builder);

  Value generateFireSignal(Cmt2FunctionLike func, Value readySignal,
                            ModuleConversionContext &ctx,
                            ImplicitLocOpBuilder &builder);

  LogicalResult convertCallOp(CallOp callOp, ModuleConversionContext &ctx,
                                 ImplicitLocOpBuilder &builder);

  LogicalResult validateCallSequence(const SmallVector<CallInfo> &calls,
                                       const ModuleConflictMatrix *conflictMatrix,
                                       Cmt2FunctionLike func);

  /// Find the top module (not instantiated by any other module)
  cmt2::ModuleOp findTopModule(cmt2::CircuitOp circuit);
};

void LowerCmt2ToFIRRTLPass::runOnOperation() {
  mlir::ModuleOp topModule = getOperation();

  // Find the cmt2.circuit operation
  cmt2::CircuitOp cmt2Circuit;
  topModule.walk([&](cmt2::CircuitOp circuit) {
    if (!cmt2Circuit)
      cmt2Circuit = circuit;
  });

  if (!cmt2Circuit) {
    LLVM_DEBUG(llvm::dbgs() << "No cmt2.circuit found, skipping\n");
    return;
  }

  if (failed(convertCircuit(cmt2Circuit))) {
    signalPassFailure();
  }
}

LogicalResult LowerCmt2ToFIRRTLPass::convertCircuit(cmt2::CircuitOp circuit) {
  OpBuilder builder(circuit);

  // Run analyses
  SchedulerAnalysis scheduler(circuit);
  ConflictMatrixAnalysis conflictAnalysis(circuit);
  CallInfoView callInfo(circuit);

  // Find the top module (not instantiated by any other module)
  cmt2::ModuleOp cmt2TopModule = findTopModule(circuit);
  if (!cmt2TopModule) {
    return circuit.emitError("No top module found (all modules are instantiated)");
  }

  StringAttr topModuleName = cmt2TopModule.getSymNameAttr();

  // Find existing FIRRTL circuit or create a new one
  auto topModule = circuit->getParentOfType<mlir::ModuleOp>();
  if (!topModule) {
    return circuit.emitError("Circuit not inside a top-level module");
  }

  firrtl::CircuitOp firrtlCircuit;
  topModule.walk([&](firrtl::CircuitOp fCircuit) {
    if (!firrtlCircuit) {
      firrtlCircuit = fCircuit;
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });

  // If there's an existing FIRRTL circuit, add our modules to it
  // Otherwise, create a new circuit with the top module name
  if (!firrtlCircuit) {
    firrtlCircuit = builder.create<firrtl::CircuitOp>(circuit.getLoc(), topModuleName);
  } else {
    // Update the circuit name to match the top module if needed
    // Note: This modifies the existing circuit to ensure the top-level name is correct
    if (firrtlCircuit.getName() != topModuleName) {
      // Set the circuit name to the Cmt2 top module
      firrtlCircuit.setNameAttr(topModuleName);
    }
  }

  // Convert each module
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      if (failed(convertModule(module, firrtlCircuit, scheduler, conflictAnalysis, callInfo))) {
        return failure();
      }
    }
  }

  // Erase the original cmt2 circuit
  circuit.erase();

  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::convertModule(
    cmt2::ModuleOp module, firrtl::CircuitOp firrtlCircuit,
    const SchedulerAnalysis &scheduler,
    const ConflictMatrixAnalysis &conflictAnalysis,
    const CallInfoView &callInfo) {

  OpBuilder builder(firrtlCircuit.getBodyBlock(), firrtlCircuit.getBodyBlock()->end());
  ImplicitLocOpBuilder implicitBuilder(module.getLoc(), builder);

  // Get schedule for this module
  StringAttr moduleName = module.getSymNameAttr();
  const ModuleScheduleResult *schedule = scheduler.getModuleSchedule(moduleName);
  const ModuleConflictMatrix *conflictMatrix = conflictAnalysis.getModuleMatrix(moduleName);
  const ModuleCallInfo *moduleCallInfo = callInfo.getModuleCallInfo(moduleName.getValue());

  if (!schedule) {
    return module.emitError("No schedule found for module");
  }

  // Create FIRRTL module with same ports
  SmallVector<PortInfo> ports;
  for (auto arg : module.getBodyRegion().front().getArguments()) {
    auto type = cast<FIRRTLBaseType>(arg.getType());
    StringAttr portName = builder.getStringAttr("arg" + std::to_string(arg.getArgNumber()));
    ports.push_back(PortInfo(portName, type, Direction::In, {}, module.getLoc()));
  }

  // Add output ports for methods and values
  for (auto &op : module.getBodyRegion().front()) {
    if (auto func = dyn_cast<Cmt2FunctionLike>(op)) {
      if (func.getFunctionKind() == FunctionKind::Method) {
        // Add enable input port
        StringAttr enableName = builder.getStringAttr(func.functionName().str() + "_enable");
        ports.push_back(PortInfo(enableName, UIntType::get(builder.getContext(), 1),
                                  Direction::In, {}, func.getLoc()));

        // Add ready output port
        StringAttr readyName = builder.getStringAttr(func.functionName().str() + "_ready");
        ports.push_back(PortInfo(readyName, UIntType::get(builder.getContext(), 1),
                                  Direction::Out, {}, func.getLoc()));

        // Add argument input ports
        auto funcType = cast<FunctionType>(func.getFunctionType());
        for (auto [idx, argType] : llvm::enumerate(funcType.getInputs())) {
          StringAttr argName = builder.getStringAttr(
              func.functionName().str() + "_arg" + std::to_string(idx));
          ports.push_back(PortInfo(argName, cast<FIRRTLBaseType>(argType),
                                    Direction::In, {}, func.getLoc()));
        }

        // Add result output ports
        for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
          StringAttr resName = builder.getStringAttr(
              func.functionName().str() + "_result" + std::to_string(idx));
          ports.push_back(PortInfo(resName, cast<FIRRTLBaseType>(resType),
                                    Direction::Out, {}, func.getLoc()));
        }
      } else if (func.getFunctionKind() == FunctionKind::Value) {
        // Add ready output port
        StringAttr readyName = builder.getStringAttr(func.functionName().str() + "_ready");
        ports.push_back(PortInfo(readyName, UIntType::get(builder.getContext(), 1),
                                  Direction::Out, {}, func.getLoc()));

        // Add result output ports
        auto funcType = cast<FunctionType>(func.getFunctionType());
        for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
          StringAttr resName = builder.getStringAttr(
              func.functionName().str() + "_result" + std::to_string(idx));
          ports.push_back(PortInfo(resName, cast<FIRRTLBaseType>(resType),
                                    Direction::Out, {}, func.getLoc()));
        }
      }
      // Rules have no ports
    }
  }

  auto firrtlModule = builder.create<FModuleOp>(
      module.getLoc(), moduleName, ConventionAttr::get(builder.getContext(), Convention::Internal), ports);

  // Set insertion point to inside the FIRRTL module
  implicitBuilder.setInsertionPointToStart(firrtlModule.getBodyBlock());

  // Create conversion context
  ModuleConversionContext ctx(module, firrtlModule, schedule, conflictMatrix, moduleCallInfo);

  // Map module arguments
  for (auto [cmt2Arg, firrtlArg] : llvm::zip(
           module.getBodyRegion().front().getArguments(),
           firrtlModule.getBodyBlock()->getArguments().take_front(
               module.getBodyRegion().front().getNumArguments()))) {
    ctx.getIRMapping().map(cmt2Arg, firrtlArg);
  }

  // Create FIRRTL instances for all cmt2.instance operations
  if (failed(createInstances(module, ctx, implicitBuilder))) {
    return failure();
  }

  // Process each schedule group
  for (const auto &group : schedule->getGroups()) {
    for (auto funcName : group.getFunctions()) {
      // Find the function operation
      Cmt2FunctionLike func;
      for (auto &op : module.getBodyRegion().front()) {
        if (auto f = dyn_cast<Cmt2FunctionLike>(op)) {
          if (f.functionNameAttr() == funcName) {
            func = f;
            break;
          }
        }
      }

      if (!func) {
        return module.emitError("Function not found: ") << funcName;
      }

      if (failed(processFunction(func, ctx, implicitBuilder))) {
        return failure();
      }
    }
  }

  // Connect output ports for methods and values
  size_t portIndex = module.getBodyRegion().front().getNumArguments();
  for (auto &op : module.getBodyRegion().front()) {
    if (auto func = dyn_cast<Cmt2FunctionLike>(op)) {
      if (func.getFunctionKind() == FunctionKind::Method) {
        // Get and set enable input port
        Value enablePort = firrtlModule.getBodyBlock()->getArgument(portIndex++);
        ctx.getSignalTracker().setEnable(func.functionNameAttr(), enablePort);

        // Connect ready output
        Value readyPort = firrtlModule.getBodyBlock()->getArgument(portIndex++);
        Value readySignal = ctx.getSignalTracker().getReady(func.functionNameAttr());
        if (readySignal) {
          implicitBuilder.create<ConnectOp>(func.getLoc(), readyPort, readySignal);
        }

        // Skip arg input ports (they're already mapped in processFunction)
        auto funcType = cast<FunctionType>(func.getFunctionType());
        portIndex += funcType.getInputs().size();

        // Connect result output ports
        ArrayRef<Value> bodyResults = ctx.getSignalTracker().getBodyResults(func.functionNameAttr());
        for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
          Value resultPort = firrtlModule.getBodyBlock()->getArgument(portIndex++);
          if (idx < bodyResults.size() && bodyResults[idx]) {
            implicitBuilder.create<ConnectOp>(func.getLoc(), resultPort, bodyResults[idx]);
          }
        }

      } else if (func.getFunctionKind() == FunctionKind::Value) {
        // Connect ready output
        Value readyPort = firrtlModule.getBodyBlock()->getArgument(portIndex++);
        Value readySignal = ctx.getSignalTracker().getReady(func.functionNameAttr());
        if (readySignal) {
          implicitBuilder.create<ConnectOp>(func.getLoc(), readyPort, readySignal);
        }

        // Connect result output ports
        auto funcType = cast<FunctionType>(func.getFunctionType());
        ArrayRef<Value> bodyResults = ctx.getSignalTracker().getBodyResults(func.functionNameAttr());
        for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
          Value resultPort = firrtlModule.getBodyBlock()->getArgument(portIndex++);
          if (idx < bodyResults.size() && bodyResults[idx]) {
            implicitBuilder.create<ConnectOp>(func.getLoc(), resultPort, bodyResults[idx]);
          }
        }
      }
      // Rules have no ports
    }
  }

  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::createInstances(cmt2::ModuleOp module,
                                                   ModuleConversionContext &ctx,
                                                   ImplicitLocOpBuilder &builder) {
  // Iterate through all operations in the module to find instances
  for (auto &op : module.getBodyRegion().front()) {
    auto instOp = dyn_cast<cmt2::InstanceOp>(op);
    if (!instOp)
      continue;

    // Get the referenced module
    auto referencedModule = instOp.getReferencedModule();
    if (!referencedModule) {
      return instOp.emitError("Referenced module not found: ")
             << instOp.getModuleName();
    }

    // Build port list for the FIRRTL instance
    SmallVector<PortInfo> ports;

    // Check if it's an external FIRRTL module
    if (auto extModOp = dyn_cast<ExtModuleFirrtlOp>(referencedModule.getOperation())) {
      // Get the actual FIRRTL module to determine ports
      auto firrtlModuleName = extModOp.getExtModuleName();

      // Find the FIRRTL module in the circuit
      auto parentCircuit = module->getParentOfType<cmt2::CircuitOp>();
      if (!parentCircuit) {
        return instOp.emitError("Module not inside a circuit");
      }

      // Look for the FIRRTL circuit and module
      auto topModule = parentCircuit->getParentOfType<mlir::ModuleOp>();
      if (!topModule) {
        return instOp.emitError("Circuit not inside a top-level module");
      }

      firrtl::CircuitOp firrtlCircuit;
      StringRef targetModuleName = firrtlModuleName;
      topModule.walk([&](firrtl::CircuitOp circuit) {
        if (circuit.getName() == targetModuleName) {
          firrtlCircuit = circuit;
          return WalkResult::interrupt();
        }
        return WalkResult::advance();
      });

      FModuleOp firrtlMod;
      if (firrtlCircuit) {
        firrtlCircuit.walk([&](FModuleOp mod) {
          if (mod.getModuleName() == targetModuleName) {
            firrtlMod = mod;
            return WalkResult::interrupt();
          }
          return WalkResult::advance();
        });
      }

      if (!firrtlMod) {
        // Try to find it directly in the top module
        topModule.walk([&](FModuleOp mod) {
          if (mod.getModuleName() == targetModuleName) {
            firrtlMod = mod;
            return WalkResult::interrupt();
          }
          return WalkResult::advance();
        });
      }

      if (!firrtlMod) {
        return instOp.emitError("FIRRTL module not found: ") << firrtlModuleName;
      }

      // Copy ports from the FIRRTL module
      ports = firrtlMod.getPorts();
    } else {
      // For regular cmt2 modules, we need to generate ports based on the module's interface
      // For now, this is a placeholder - in a full implementation, you'd generate ports
      // based on the module's methods and values
      return instOp.emitError("Instance of non-external modules not yet supported");
    }

    // Create the FIRRTL instance
    auto extMod = dyn_cast<ExtModuleFirrtlOp>(referencedModule.getOperation());
    auto firrtlInst = builder.create<firrtl::InstanceOp>(
        instOp.getLoc(), ports, extMod.getExtModuleName(), instOp.getSymName(),
        NameKindEnum::DroppableName);

    // Register the instance in the context
    ctx.registerInstance(instOp.getSymNameAttr(), firrtlInst);

    // Track which ports have been connected
    llvm::SmallDenseSet<size_t> connectedPorts;

    // Connect bare arguments (e.g., clock, reset) from instance operands to instance ports
    // The bare arguments are the operands of the instance and correspond to bind.bare operations
    auto instanceArgs = instOp.getArgs();
    if (!instanceArgs.empty()) {
      // Find bind.bare operations in the external module to determine port names
      size_t barePortIdx = 0;
      if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(referencedModule.getOperation())) {
        for (auto &bodyOp : extMod.getBodyRegion().front()) {
          if (auto bindBare = dyn_cast<BindBareOp>(bodyOp)) {
            if (barePortIdx >= instanceArgs.size()) {
              break;
            }

            // Map the instance argument through IRMapping
            Value firrtlArg = ctx.getIRMapping().lookupOrDefault(instanceArgs[barePortIdx]);

            // Find the corresponding instance port by name
            FlatSymbolRefAttr portAttr = bindBare.getPortAttr();
            StringAttr portName = portAttr.getAttr();
            for (size_t i = 0; i < firrtlInst.getNumResults(); ++i) {
              if (firrtlInst.getPortName(i) == portName) {
                Value instPort = firrtlInst.getResult(i);
                builder.create<ConnectOp>(instOp.getLoc(), instPort, firrtlArg);
                connectedPorts.insert(i);
                break;
              }
            }
            barePortIdx++;
          }
        }
      }
    }

    // Initialize all remaining input ports with default values
    // This is required by FIRRTL semantics - all sinks must be fully initialized
    for (size_t i = 0; i < ports.size(); ++i) {
      if (connectedPorts.contains(i))
        continue;

      const PortInfo &portInfo = ports[i];
      if (portInfo.direction == Direction::In) {
        Value instPort = firrtlInst.getResult(i);
        Type portType = instPort.getType();

        // Create a default value based on the port type
        if (auto uintType = dyn_cast<UIntType>(portType)) {
          // For UInt types, use 0 as default
          Value zero = builder.create<ConstantOp>(
              instOp.getLoc(), uintType, APInt(uintType.getWidth().value_or(1), 0));
          builder.create<ConnectOp>(instOp.getLoc(), instPort, zero);
        } else if (auto sintType = dyn_cast<SIntType>(portType)) {
          // For SInt types, use 0 as default
          Value zero = builder.create<ConstantOp>(
              instOp.getLoc(), sintType, APInt(sintType.getWidth().value_or(1), 0));
          builder.create<ConnectOp>(instOp.getLoc(), instPort, zero);
        }
        // Clock and Reset types are typically connected via bare args, so we don't need defaults for them
        // If they're not connected, it's likely an error in the input
      }
    }
  }

  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::processFunction(Cmt2FunctionLike func,
                                                   ModuleConversionContext &ctx,
                                                   ImplicitLocOpBuilder &builder) {
  // Check for @this calls (private functions should be inlined first)
  bool hasThisCall = false;
  func.walk([&](CallOp call) {
    if (call.getCallee().getRootReference().getValue() == "this") {
      hasThisCall = true;
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });

  if (hasThisCall) {
    return func.emitError(
        "Function contains @this calls. Run -cmt2-inline-private-funcs before conversion.");
  }

  // Get calls for this function
  const ModuleCallInfo *callInfo = ctx.getCallInfo();
  SmallVector<CallInfo> calls;
  if (callInfo) {
    auto it = callInfo->find(SymbolRefAttr::get(func.functionNameAttr()));
    if (it != callInfo->end()) {
      calls.assign(it->second.begin(), it->second.end());
    }
  }

  // Validate call sequence
  if (failed(validateCallSequence(calls, ctx.getConflictMatrix(), func))) {
    return failure();
  }

  // Map function parameters to FIRRTL module argument ports
  // This must be done before cloning any regions, as they reference these parameters
  if (!func.isExternal()) {
    FModuleOp firrtlModule = ctx.getFIRRTLModule();

    // Map block arguments from all regions (guard and body) to FIRRTL ports
    for (Region &region : func->getRegions()) {
      if (!region.empty() && !region.front().getArguments().empty()) {
        for (auto [idx, blockArg] : llvm::enumerate(region.front().getArguments())) {
          StringAttr argPortName = builder.getStringAttr(
              func.functionName().str() + "_arg" + std::to_string(idx));

          // Find the port in the FIRRTL module
          for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts(); ++portIdx) {
            if (firrtlModule.getPortName(portIdx) == argPortName) {
              Value argPort = firrtlModule.getArgument(portIdx);
              ctx.getIRMapping().map(blockArg, argPort);
              break;
            }
          }
        }
      }
    }
  }

  // Clone guard region to get the guard result
  SmallVector<Value> guardResults;
  if (!func.isExternal()) {
    // Get the guard region (first region for two-region ops)
    Region &guardRegion = func->getRegion(0);
    if (failed(cloneRegionOps(guardRegion, ctx, builder, guardResults))) {
      return failure();
    }
  }

  // Guard result defaults to true if no guard
  Value guardResult;
  if (!guardResults.empty()) {
    guardResult = guardResults[0];
  } else {
    guardResult = builder.create<ConstantOp>(
        func.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 1));
  }

  // Find the schedule group containing this function
  const ScheduleGroup *containingGroup = nullptr;
  for (const auto &group : ctx.getSchedule()->getGroups()) {
    if (llvm::is_contained(group.getFunctions(), func.functionNameAttr())) {
      containingGroup = &group;
      break;
    }
  }

  if (!containingGroup) {
    return func.emitError("Function not found in any schedule group");
  }

  // Generate ready signal
  Value readySignal = generateReadySignal(func, guardResult, calls,
                                           *containingGroup, ctx, builder);
  ctx.getSignalTracker().setReady(func.functionNameAttr(), readySignal);

  // Generate fire signal
  Value fireSignal = generateFireSignal(func, readySignal, ctx, builder);
  ctx.getSignalTracker().setFire(func.functionNameAttr(), fireSignal);

  // Clone body region under fire guard
  if (!func.isExternal() && func->getNumRegions() > 1) {
    Region &bodyRegion = func->getRegion(1);

    // Create firrtl.when block for the body
    auto whenOp = builder.create<WhenOp>(func.getLoc(), fireSignal, /*withElseRegion=*/false);

    // Set builder to insert into when block
    OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(&whenOp.getThenBlock());

    SmallVector<Value> bodyResults;
    if (failed(cloneRegionOps(bodyRegion, ctx, builder, bodyResults))) {
      return failure();
    }

    // Store body results for later use
    ctx.getSignalTracker().setBodyResults(func.functionNameAttr(), bodyResults);
  }

  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::validateCallSequence(
    const SmallVector<CallInfo> &calls,
    const ModuleConflictMatrix *conflictMatrix,
    Cmt2FunctionLike func) {
  if (!conflictMatrix || calls.size() < 2)
    return success();

  // Check for sequential violations
  for (size_t i = 0; i < calls.size(); ++i) {
    for (size_t j = i + 1; j < calls.size(); ++j) {
      auto rel = conflictMatrix->getRelationship(
          calls[j].calleeEntity.getLeafReference(),
          calls[i].calleeEntity.getLeafReference());
      if (rel == Relationship::SequentialBefore) {
        // calls[j] < calls[i], but calls[i] appears before calls[j]
        return func.emitError("Sequential before violation: ")
               << calls[j].calleeEntity
               << " should be called before " << calls[i].calleeEntity;
      }
    }
  }

  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::cloneRegionOps(
    Region &sourceRegion,
    ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder,
    SmallVectorImpl<Value> &results) {

  if (sourceRegion.empty())
    return success();

  Block &sourceBlock = sourceRegion.front();

  // Clone each operation in the region
  for (Operation &op : sourceBlock) {
    // Handle cmt2.return specially - collect results
    if (auto returnOp = dyn_cast<ReturnOp>(op)) {
      for (Value result : returnOp.getOperands()) {
        Value mappedResult = ctx.getIRMapping().lookupOrDefault(result);
        results.push_back(mappedResult);
      }
      continue;
    }

    // Handle cmt2.call specially - convert to signal accesses
    if (auto callOp = dyn_cast<CallOp>(op)) {
      if (failed(convertCallOp(callOp, ctx, builder))) {
        return failure();
      }
      continue;
    }

    // Clone other operations normally
    Operation *cloned = builder.clone(op, ctx.getIRMapping());

    // Update mapping for results
    for (auto [oldResult, newResult] : llvm::zip(op.getResults(), cloned->getResults())) {
      ctx.getIRMapping().map(oldResult, newResult);
    }
  }

  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::convertCallOp(
    CallOp callOp,
    ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder) {

  // Get callee information
  StringAttr instanceName = callOp.getCallee().getRootReference();
  StringAttr methodName = callOp.getMethodOrValue().getLeafReference();

  // Look up the FIRRTL instance
  firrtl::InstanceOp firrtlInst = ctx.getInstance(instanceName);
  if (!firrtlInst) {
    return callOp.emitError("Instance not found: ") << instanceName;
  }

  // Find the cmt2 instance to get the referenced module
  cmt2::InstanceOp cmt2Inst;
  for (auto &op : ctx.getCmt2Module().getBodyRegion().front()) {
    if (auto inst = dyn_cast<cmt2::InstanceOp>(op)) {
      if (inst.getSymNameAttr() == instanceName) {
        cmt2Inst = inst;
        break;
      }
    }
  }

  if (!cmt2Inst) {
    return callOp.emitError("Cmt2 instance not found: ") << instanceName;
  }

  // Get the referenced module
  auto referencedModule = cmt2Inst.getReferencedModule();
  if (!referencedModule) {
    return callOp.emitError("Referenced module not found for instance: ") << instanceName;
  }

  // Only handle external FIRRTL modules for now
  auto extModOp = dyn_cast<ExtModuleFirrtlOp>(referencedModule.getOperation());
  if (!extModOp) {
    return callOp.emitError("Only external FIRRTL modules are supported for calls");
  }

  // Find the bind operation for this method/value
  Cmt2FunctionLike bindFunc;
  for (auto &bodyOp : extModOp.getBodyRegion().front()) {
    if (auto bindMethod = dyn_cast<BindMethodOp>(bodyOp)) {
      if (bindMethod.getSymNameAttr() == methodName) {
        bindFunc = bindMethod;
        break;
      }
    } else if (auto bindValue = dyn_cast<BindValueOp>(bodyOp)) {
      if (bindValue.getSymNameAttr() == methodName) {
        bindFunc = bindValue;
        break;
      }
    }
  }

  if (!bindFunc) {
    return callOp.emitError("Bind operation not found for: ") << methodName;
  }

  // Get port indices for the FIRRTL instance
  auto getPortIndex = [&](StringAttr portName) -> std::optional<size_t> {
    for (size_t i = 0; i < firrtlInst.getNumResults(); ++i) {
      if (firrtlInst.getPortName(i) == portName) {
        return i;
      }
    }
    return std::nullopt;
  };

  SmallVector<Value> mappedResults;

  if (auto bindMethod = dyn_cast<BindMethodOp>(bindFunc.getOperation())) {
    // Handle method call
    // Drive the enable signal to 1 to indicate we're calling this method
    auto enableAttr = bindMethod.getEnable();
    if (enableAttr.has_value()) {
      StringAttr enablePortName = builder.getStringAttr(enableAttr.value());
      auto enablePortIdx = getPortIndex(enablePortName);
      if (!enablePortIdx) {
        return callOp.emitError("Enable port not found: ") << enablePortName;
      }
      Value enablePort = firrtlInst.getResult(*enablePortIdx);

      // Create constant 1 to enable the method
      Value one = builder.create<ConstantOp>(
          callOp.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 1));
      builder.create<ConnectOp>(callOp.getLoc(), enablePort, one);
    }

    // Connect input arguments to input ports
    auto inputsAttr = bindMethod.getInputs();
    if (inputsAttr.size() != callOp.getNumOperands()) {
      return callOp.emitError("Operand count mismatch: expected ")
             << inputsAttr.size() << " but got " << callOp.getNumOperands();
    }

    for (auto [operand, inputAttr] : llvm::zip(callOp.getOperands(), inputsAttr)) {
      auto portAttr = cast<FlatSymbolRefAttr>(inputAttr);
      auto portIdx = getPortIndex(portAttr.getAttr());
      if (!portIdx) {
        return callOp.emitError("Input port not found: ") << portAttr;
      }
      Value inputPort = firrtlInst.getResult(*portIdx);

      // Map the operand through IRMapping to get the actual value in FIRRTL context
      Value mappedOperand = ctx.getIRMapping().lookupOrDefault(operand);

      // Ensure the operand was properly mapped
      if (!mappedOperand) {
        return callOp.emitError("Call operand was not properly mapped to FIRRTL context");
      }

      // Create connect operation to drive the input port
      builder.create<ConnectOp>(callOp.getLoc(), inputPort, mappedOperand);
    }

    // Read output results
    auto outputsAttr = bindMethod.getOutputs();
    if (outputsAttr.size() != callOp.getNumResults()) {
      return callOp.emitError("Result count mismatch: expected ")
             << outputsAttr.size() << " but got " << callOp.getNumResults();
    }

    for (auto outputAttr : outputsAttr) {
      auto portAttr = cast<FlatSymbolRefAttr>(outputAttr);
      auto portIdx = getPortIndex(portAttr.getAttr());
      if (!portIdx) {
        return callOp.emitError("Output port not found: ") << portAttr;
      }
      Value outputPort = firrtlInst.getResult(*portIdx);
      mappedResults.push_back(outputPort);
    }

  } else if (auto bindValue = dyn_cast<BindValueOp>(bindFunc.getOperation())) {
    // Handle value call
    // Read data results
    auto dataAttr = bindValue.getData();
    if (dataAttr.size() != callOp.getNumResults()) {
      return callOp.emitError("Result count mismatch: expected ")
             << dataAttr.size() << " but got " << callOp.getNumResults();
    }

    for (auto dataPortAttr : dataAttr) {
      auto portAttr = cast<FlatSymbolRefAttr>(dataPortAttr);
      auto portIdx = getPortIndex(portAttr.getAttr());
      if (!portIdx) {
        return callOp.emitError("Data port not found: ") << portAttr;
      }
      Value dataPort = firrtlInst.getResult(*portIdx);
      mappedResults.push_back(dataPort);
    }
  }

  // Map call results to the instance ports
  for (auto [callResult, portValue] : llvm::zip(callOp.getResults(), mappedResults)) {
    ctx.getIRMapping().map(callResult, portValue);
  }

  return success();
}

Value LowerCmt2ToFIRRTLPass::generateReadySignal(
    Cmt2FunctionLike func, Value guardResult,
    const SmallVector<CallInfo> &calls,
    const ScheduleGroup &group,
    ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder) {

  Value ready = guardResult;

  // AND with called functions' ready signals
  for (const auto &call : calls) {
    Value calleeReady = ctx.getSignalTracker().getReady(
        call.calleeEntity.getLeafReference());
    if (calleeReady) {
      ready = builder.create<AndPrimOp>(func.getLoc(), ready, calleeReady);
    }
  }

  // AND with NOT(preceding conflicting functions fired)
  const auto &funcs = group.getFunctions();
  auto funcIt = llvm::find(funcs, func.functionNameAttr());
  if (funcIt != funcs.end()) {
    for (auto it = funcs.begin(); it != funcIt; ++it) {
      StringAttr precedingFunc = *it;
      auto rel = ctx.getConflictMatrix()->getRelationship(
          precedingFunc, func.functionNameAttr());
      if (rel == Relationship::Conflict || rel == Relationship::SequentialBefore) {
        Value precedingFire = ctx.getSignalTracker().getFire(precedingFunc);
        if (precedingFire) {
          Value notFired = builder.create<XorPrimOp>(
              func.getLoc(), precedingFire,
              builder.create<ConstantOp>(func.getLoc(),
                                          UIntType::get(builder.getContext(), 1),
                                          APInt(1, 1)));
          ready = builder.create<AndPrimOp>(func.getLoc(), ready, notFired);
        }
      }
    }
  }

  return ready;
}

Value LowerCmt2ToFIRRTLPass::generateFireSignal(
    Cmt2FunctionLike func, Value readySignal,
    ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder) {

  if (func.getFunctionKind() == FunctionKind::Method) {
    // fire = ready AND enable
    Value enableSignal = ctx.getSignalTracker().getEnable(func.functionNameAttr());
    if (!enableSignal) {
      // Find enable port from FIRRTL module
      FModuleOp firrtlModule = ctx.getFIRRTLModule();
      StringAttr enablePortName = builder.getStringAttr(func.functionName().str() + "_enable");

      // Search for the enable port
      for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts(); ++portIdx) {
        if (firrtlModule.getPortName(portIdx) == enablePortName) {
          enableSignal = firrtlModule.getArgument(portIdx);
          // Cache it for future use
          ctx.getSignalTracker().setEnable(func.functionNameAttr(), enableSignal);
          break;
        }
      }

      // If still not found, default to constant 0 (method never fires)
      if (!enableSignal) {
        enableSignal = builder.create<ConstantOp>(
            func.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 0));
      }
    }
    return builder.create<AndPrimOp>(func.getLoc(), readySignal, enableSignal);
  } else {
    // For rules and values, fire = ready
    return readySignal;
  }
}

cmt2::ModuleOp LowerCmt2ToFIRRTLPass::findTopModule(cmt2::CircuitOp circuit) {
  // Collect all module names that are instantiated
  DenseSet<StringAttr> instantiatedModules;

  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      // Walk through the module to find all instance operations
      module.walk([&](cmt2::InstanceOp inst) {
        auto refModule = inst.getReferencedModule();
        if (refModule) {
          instantiatedModules.insert(refModule.moduleNameAttr());
        }
      });
    }
  }

  // Find modules that are not instantiated by any other module
  SmallVector<cmt2::ModuleOp> topModules;
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      if (!instantiatedModules.contains(module.getSymNameAttr())) {
        topModules.push_back(module);
      }
    }
  }

  // Return the first top module (there should be exactly one in well-formed circuits)
  return topModules.empty() ? cmt2::ModuleOp() : topModules[0];
}

} // namespace

std::unique_ptr<Pass> circt::createLowerCmt2ToFIRRTLPass() {
  return std::make_unique<LowerCmt2ToFIRRTLPass>();
}

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
// CONVERSION STRATEGY:
// ====================
// The conversion transforms a Cmt2 circuit into a FIRRTL circuit by converting
// each Cmt2 module into a FIRRTL module with proper scheduling and control logic.
//
// High-level steps:
// 1. Run analyses (Scheduler, ConflictMatrix, CallInfo) to understand module behavior
// 2. For each Cmt2 module:
//    a. Create FIRRTL module with ports for methods/values (enable, ready, args, results)
//    b. Instantiate external FIRRTL modules for cmt2.instance operations
//    c. Process each function (rule/method/value) in scheduled order:
//       - Clone guard region to compute guard result
//       - Generate ready signal = guard ∧ called_readies ∧ ¬(conflicting_fires)
//       - Generate fire signal = ready (for rules/values) or ready ∧ enable (for methods)
//       - Clone body region inside firrtl.when(fire) block
//       - Convert cmt2.call operations to FIRRTL signal connections
//
// SIGNAL GENERATION:
// ==================
// - ready: Indicates a function can execute (guard is true, dependencies ready, no conflicts)
// - enable: Input port for methods, driven by callers to request execution
// - fire: Indicates a function is actually executing (ready for rules/values, ready∧enable for methods)
//
// CALL CONVERSION:
// ================
// cmt2.call operations are converted to FIRRTL port connections:
// - For method calls: Drive enable=1, connect arguments to input ports, read output ports
// - For value calls: Read data output ports directly
//
//===----------------------------------------------------------------------===//

#include "circt/Conversion/Cmt2ToFIRRTL.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Transforms/CallInfo.h"
#include "circt/Dialect/Cmt2/Transforms/ConflictMatrix.h"
#include "circt/Dialect/Cmt2/Transforms/InstanceGraph.h"
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
#include "llvm/ADT/PostOrderIterator.h"
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

/// Tracks generated signals (ready/enable/fire) and body results for each
/// function during conversion. This allows later stages to reference signals
/// generated in earlier stages.
class SignalTracker {
public:
  void setReady(StringAttr funcName, Value ready) { readySignals[funcName] = ready; }
  Value getReady(StringAttr funcName) const {
    auto it = readySignals.find(funcName);
    return it != readySignals.end() ? it->second : Value();
  }

  void setEnable(StringAttr funcName, Value enable) { enableSignals[funcName] = enable; }
  Value getEnable(StringAttr funcName) const {
    auto it = enableSignals.find(funcName);
    return it != enableSignals.end() ? it->second : Value();
  }

  void setFire(StringAttr funcName, Value fire) { fireSignals[funcName] = fire; }
  Value getFire(StringAttr funcName) const {
    auto it = fireSignals.find(funcName);
    return it != fireSignals.end() ? it->second : Value();
  }

  void setBodyResults(StringAttr funcName, ValueRange results) {
    bodyResults[funcName] = SmallVector<Value>(results.begin(), results.end());
  }
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

/// Holds per-module conversion state including analysis results, signal tracker,
/// and mappings between Cmt2 and FIRRTL constructs.
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

  void registerInstance(StringAttr name, firrtl::InstanceOp firrtlInst) {
    instances[name] = firrtlInst;
  }
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
  // Main conversion entry points
  LogicalResult convertCircuit(cmt2::CircuitOp circuit);
  LogicalResult convertModule(cmt2::ModuleOp module, firrtl::CircuitOp firrtlCircuit,
                                 const SchedulerAnalysis &scheduler,
                                 const ConflictMatrixAnalysis &conflictAnalysis,
                                 const CallInfoView &callInfo,
                                 const DenseMap<StringAttr, FModuleOp> &convertedModules,
                                 FModuleOp &outFirrtlModule);

  // Module-level operations
  void createFunctionPorts(cmt2::ModuleOp module, OpBuilder &builder,
                            SmallVectorImpl<PortInfo> &ports);
  LogicalResult createInstances(cmt2::ModuleOp module, ModuleConversionContext &ctx,
                                  ImplicitLocOpBuilder &builder,
                                  const DenseMap<StringAttr, FModuleOp> &convertedModules);
  LogicalResult connectOutputPorts(cmt2::ModuleOp module, ModuleConversionContext &ctx,
                                     ImplicitLocOpBuilder &builder);

  // Function-level operations
  LogicalResult processFunction(Cmt2FunctionLike func, ModuleConversionContext &ctx,
                                  ImplicitLocOpBuilder &builder);
  void mapFunctionArgumentsToports(Cmt2FunctionLike func, ModuleConversionContext &ctx,
                                    OpBuilder &builder);

  // Signal generation
  Value generateReadySignal(Cmt2FunctionLike func, Value guardResult,
                              // const SmallVector<CallInfo> &calls,
                              const ScheduleGroup &group,
                              ModuleConversionContext &ctx,
                              ImplicitLocOpBuilder &builder);
  Value generateFireSignal(Cmt2FunctionLike func, Value readySignal,
                            ModuleConversionContext &ctx,
                            ImplicitLocOpBuilder &builder);
  
  // Region cloning and call conversion
  LogicalResult cloneRegionOps(Region &sourceRegion, ModuleConversionContext &ctx,
                                ImplicitLocOpBuilder &builder,
                                SmallVectorImpl<Value> &results);
  LogicalResult convertCallOp(CallOp callOp, ModuleConversionContext &ctx,
                                ImplicitLocOpBuilder &builder);
  LogicalResult connectMethodCall(CallOp callOp, BindMethodOp bindMethod,
                                    firrtl::InstanceOp firrtlInst,
                                    ModuleConversionContext &ctx,
                                    ImplicitLocOpBuilder &builder);
  LogicalResult connectValueCall(CallOp callOp, BindValueOp bindValue,
                                   firrtl::InstanceOp firrtlInst,
                                   ModuleConversionContext &ctx,
                                   ImplicitLocOpBuilder &builder);

  // Helper utilities
  FModuleOp findFIRRTLModule(StringRef moduleName, Operation *searchRoot);
  std::optional<size_t> getPortIndex(firrtl::InstanceOp inst, StringAttr portName);
  cmt2::ModuleOp findTopModule(cmt2::CircuitOp circuit);

  // Port name helpers
  std::string buildPortName(StringRef base, StringRef suffix);
  std::string buildInterfacePortName(StringRef declName, StringRef methodName, StringRef suffix);

  // Instance creation helpers
  void connectInstanceModuleArguments(cmt2::InstanceOp instOp, firrtl::InstanceOp firrtlInst,
                                       Operation *referencedModule, ModuleConversionContext &ctx,
                                       ImplicitLocOpBuilder &builder,
                                       llvm::SmallDenseSet<size_t> &connectedPorts);
  void initializeUnconnectedInputPorts(firrtl::InstanceOp firrtlInst,
                                        const SmallVector<PortInfo> &ports,
                                        const llvm::SmallDenseSet<size_t> &connectedPorts,
                                        ImplicitLocOpBuilder &builder);
  LogicalResult processInterfaceBinding(cmt2::InstanceOp instOp, firrtl::InstanceOp firrtlInst,
                                         ArrayAttr interfaceBindAttr, cmt2::ModuleOp module,
                                         ModuleConversionContext &ctx,
                                         ImplicitLocOpBuilder &builder,
                                         llvm::SmallDenseSet<size_t> &connectedPorts);

  // Interface binding helper
  LogicalResult connectInterfaceBinding(firrtl::InstanceOp childInst,
                                         firrtl::InstanceOp parentInst,
                                         StringRef childDeclName, StringRef childMethodName,
                                         StringRef parentMethodName, Cmt2FunctionLike ifaceFunc,
                                         Operation *parentBindOp, bool isExternalFirrtl,
                                         ImplicitLocOpBuilder &builder,
                                         llvm::SmallDenseSet<size_t> &connectedPorts);

  // Interface helpers
  bool isInterfaceCall(CallOp callOp, cmt2::ModuleOp module);
  InterfaceOp getInterfaceForDecl(InterfaceDeclOp decl);
  void createInterfacePorts(cmt2::ModuleOp module, OpBuilder &builder,
                            SmallVectorImpl<PortInfo> &ports);
  LogicalResult connectInterfaceCall(CallOp callOp, InterfaceDeclOp interfaceDecl,
                                      ModuleConversionContext &ctx,
                                      ImplicitLocOpBuilder &builder);

  // Naming helpers
  ArrayAttr getArgNames(Cmt2FunctionLike func);
  ArrayAttr getBodyResNames(Cmt2FunctionLike func);
};

//===----------------------------------------------------------------------===//
// Main Entry Points
//===----------------------------------------------------------------------===//

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

  // Run analyses on the circuit to gather scheduling and dependency information
  SchedulerAnalysis scheduler(circuit);
  ConflictMatrixAnalysis conflictAnalysis(circuit);
  CallInfoView callInfo(circuit);

  // Build instance graph to determine conversion order (bottom-up)
  InstanceGraph instanceGraph(circuit);

  // Find the top module (not instantiated by any other module)
  cmt2::ModuleOp cmt2TopModule = findTopModule(circuit);
  if (!cmt2TopModule) {
    return circuit.emitError("No top module found (all modules are instantiated)");
  }

  // Find or create FIRRTL circuit with the top module name
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

  StringAttr topModuleName = cmt2TopModule.getSymNameAttr();
  if (!firrtlCircuit) {
    firrtlCircuit = builder.create<firrtl::CircuitOp>(circuit.getLoc(), topModuleName);
  } else if (firrtlCircuit.getName() != topModuleName) {
    firrtlCircuit.setNameAttr(topModuleName);
  }

  // Map to track converted Cmt2 modules to their FIRRTL equivalents
  DenseMap<StringAttr, FModuleOp> convertedModules;

  // Collect modules in post-order (bottom-up) for proper instantiation
  SmallVector<cmt2::ModuleOp> modulesToConvert;
  auto *topNode = instanceGraph.lookup(cmt2TopModule);

  // Use post-order traversal to visit leaves first, then parents
  for (auto *node : llvm::post_order(topNode)) {
    auto module = dyn_cast<cmt2::ModuleOp>(node->getModule().getOperation());
    if (module) {
      modulesToConvert.push_back(module);
    }
  }

  // Convert each Cmt2 module to a FIRRTL module in bottom-up order
  for (auto module : modulesToConvert) {
    FModuleOp firrtlMod;
    if (failed(convertModule(module, firrtlCircuit, scheduler, conflictAnalysis,
                             callInfo, convertedModules, firrtlMod))) {
      return failure();
    }
    // Track the converted module
    convertedModules[module.getSymNameAttr()] = firrtlMod;
  }

  // Erase the original cmt2 circuit
  circuit.erase();
  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::convertModule(
    cmt2::ModuleOp module, firrtl::CircuitOp firrtlCircuit,
    const SchedulerAnalysis &scheduler,
    const ConflictMatrixAnalysis &conflictAnalysis,
    const CallInfoView &callInfo,
    const DenseMap<StringAttr, FModuleOp> &convertedModules,
    FModuleOp &outFirrtlModule) {

  OpBuilder builder(firrtlCircuit.getBodyBlock(), firrtlCircuit.getBodyBlock()->end());
  ImplicitLocOpBuilder implicitBuilder(module.getLoc(), builder);

  // Retrieve analysis results for this module
  StringAttr moduleName = module.getSymNameAttr();
  const ModuleScheduleResult *schedule = scheduler.getModuleSchedule(moduleName);
  const ModuleConflictMatrix *conflictMatrix = conflictAnalysis.getModuleMatrix(moduleName);
  const ModuleCallInfo *moduleCallInfo = callInfo.getModuleCallInfo(moduleName.getValue());

  if (!schedule) {
    return module.emitError("No schedule found for module");
  }

  // Create FIRRTL module with ports for module arguments and function interfaces
  SmallVector<PortInfo> ports;

  // Add input ports for module arguments
  auto moduleArgNames = module.getArgNames();
  for (auto arg : module.getBodyRegion().front().getArguments()) {
    auto type = cast<FIRRTLBaseType>(arg.getType());
    StringRef argName = arg.getArgNumber() < moduleArgNames.size()
                            ? cast<StringAttr>(moduleArgNames[arg.getArgNumber()]).getValue()
                            : ("arg" + std::to_string(arg.getArgNumber()));
    StringAttr portName = builder.getStringAttr(argName);
    ports.push_back(PortInfo(portName, type, Direction::In, {}, module.getLoc()));
  }

  // Add ports for methods and values (enable, ready, args, results)
  createFunctionPorts(module, builder, ports);

  // Add ports for interface declarations
  createInterfacePorts(module, builder, ports);

  auto firrtlModule = builder.create<FModuleOp>(
      module.getLoc(), moduleName, ConventionAttr::get(builder.getContext(), Convention::Internal), ports);

  implicitBuilder.setInsertionPointToStart(firrtlModule.getBodyBlock());

  // Create conversion context to track state during conversion
  ModuleConversionContext ctx(module, firrtlModule, schedule, conflictMatrix, moduleCallInfo);

  // Map module arguments to FIRRTL ports
  for (auto [cmt2Arg, firrtlArg] : llvm::zip(
           module.getBodyRegion().front().getArguments(),
           firrtlModule.getBodyBlock()->getArguments().take_front(
               module.getBodyRegion().front().getNumArguments()))) {
    ctx.getIRMapping().map(cmt2Arg, firrtlArg);
  }

  // Initialize interface output ports with default values
  // Interface ports start after module arguments and function ports
  for (auto &op : module.getBodyRegion().front()) {
    auto decl = dyn_cast<InterfaceDeclOp>(op);
    if (!decl)
      continue;

    InterfaceOp iface = getInterfaceForDecl(decl);
    if (!iface)
      continue;

    StringRef declName = decl.getSymName();

    // Initialize enable and data output ports for each method/value in the interface
    for (auto &ifaceOp : iface.getBodyRegion().front()) {
      if (auto method = dyn_cast<MethodOp>(ifaceOp)) {
        auto funcType = cast<FunctionType>(method.getFunctionType());
        StringRef methodName = method.getSymName();
        auto argNames = method.getArgNames();

        // Initialize enable port (out) with 0
        StringAttr enablePortName = builder.getStringAttr(
            declName.str() + "_" + methodName.str() + "_enable");
        for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts(); ++portIdx) {
          if (firrtlModule.getPortName(portIdx) == enablePortName) {
            Value enablePort = firrtlModule.getArgument(portIdx);
            Value zero = implicitBuilder.create<ConstantOp>(
                module.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 0));
            implicitBuilder.create<ConnectOp>(module.getLoc(), enablePort, zero);
            break;
          }
        }

        // Initialize argument ports (out) with invalid
        for (auto [idx, argType] : llvm::enumerate(funcType.getInputs())) {
          StringRef argName = idx < argNames.size()
                                  ? cast<StringAttr>(argNames[idx]).getValue()
                                  : ("arg" + std::to_string(idx));
          StringAttr argPortName = builder.getStringAttr(
              declName.str() + "_" + methodName.str() + "_" + argName.str());
          for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts(); ++portIdx) {
            if (firrtlModule.getPortName(portIdx) == argPortName) {
              Value argPort = firrtlModule.getArgument(portIdx);
              auto firrtlType = cast<FIRRTLBaseType>(argType);
              Value invalid = implicitBuilder.create<InvalidValueOp>(
                  module.getLoc(), firrtlType);
              implicitBuilder.create<ConnectOp>(module.getLoc(), argPort, invalid);
              break;
            }
          }
        }
      }
    }
  }

  // Create FIRRTL instances for all cmt2.instance operations
  if (failed(createInstances(module, ctx, implicitBuilder, convertedModules))) {
    return failure();
  }

  // Process each function in scheduled order
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

  // Connect function outputs to FIRRTL module ports
  if (failed(connectOutputPorts(module, ctx, implicitBuilder))) {
    return failure();
  }

  // Return the created FIRRTL module
  outFirrtlModule = firrtlModule;
  return success();
}

//===----------------------------------------------------------------------===//
// Module-Level Operations
//===----------------------------------------------------------------------===//

void LowerCmt2ToFIRRTLPass::createFunctionPorts(cmt2::ModuleOp module,
                                                  OpBuilder &builder,
                                                  SmallVectorImpl<PortInfo> &ports) {
  // For each function (method/value/rule), create appropriate ports
  for (auto &op : module.getBodyRegion().front()) {
    auto func = dyn_cast<Cmt2FunctionLike>(op);
    if (!func)
      continue;

    auto funcType = cast<FunctionType>(func.getFunctionType());
    StringRef funcName = func.functionName();
    auto argNames = getArgNames(func);
    auto bodyResNames = getBodyResNames(func);

    if (func.getFunctionKind() == FunctionKind::Method) {
      // Methods need: enable (in), ready (out), args (in), results (out)
      ports.push_back(PortInfo(builder.getStringAttr(funcName.str() + "_enable"),
                                UIntType::get(builder.getContext(), 1),
                                Direction::In, {}, func.getLoc()));
      ports.push_back(PortInfo(builder.getStringAttr(funcName.str() + "_ready"),
                                UIntType::get(builder.getContext(), 1),
                                Direction::Out, {}, func.getLoc()));

      // Add argument ports with meaningful names
      for (auto [idx, argType] : llvm::enumerate(funcType.getInputs())) {
        StringRef argName = idx < argNames.size()
                                ? cast<StringAttr>(argNames[idx]).getValue()
                                : ("arg" + std::to_string(idx));
        ports.push_back(PortInfo(
            builder.getStringAttr(funcName.str() + "_" + argName.str()),
            cast<FIRRTLBaseType>(argType), Direction::In, {}, func.getLoc()));
      }

      // Add result ports with meaningful names
      for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
        StringRef resName = bodyResNames && idx < bodyResNames.size()
                                ? cast<StringAttr>(bodyResNames[idx]).getValue()
                                : ("result" + std::to_string(idx));
        ports.push_back(PortInfo(
            builder.getStringAttr(funcName.str() + "_" + resName.str()),
            cast<FIRRTLBaseType>(resType), Direction::Out, {}, func.getLoc()));
      }

    } else if (func.getFunctionKind() == FunctionKind::Value) {
      // Values need: ready (out), results (out)
      ports.push_back(PortInfo(builder.getStringAttr(funcName.str() + "_ready"),
                                UIntType::get(builder.getContext(), 1),
                                Direction::Out, {}, func.getLoc()));

      // Add result ports with meaningful names
      for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
        StringRef resName = bodyResNames && idx < bodyResNames.size()
                                ? cast<StringAttr>(bodyResNames[idx]).getValue()
                                : ("result" + std::to_string(idx));
        ports.push_back(PortInfo(
            builder.getStringAttr(funcName.str() + "_" + resName.str()),
            cast<FIRRTLBaseType>(resType), Direction::Out, {}, func.getLoc()));
      }
    }
    // Rules have no external ports (they're internal to the module)
  }
}

LogicalResult LowerCmt2ToFIRRTLPass::createInstances(cmt2::ModuleOp module,
                                                   ModuleConversionContext &ctx,
                                                   ImplicitLocOpBuilder &builder,
                                                   const DenseMap<StringAttr, FModuleOp> &convertedModules) {
  // For each cmt2.instance, create a corresponding firrtl.instance
  for (auto &op : module.getBodyRegion().front()) {
    auto instOp = dyn_cast<cmt2::InstanceOp>(op);
    if (!instOp)
      continue;

    auto referencedModule = instOp.getReferencedModule();
    if (!referencedModule)
      return instOp.emitError("Referenced module not found: ") << instOp.getModuleName();

    // Determine target FIRRTL module (external or converted cmt2 module)
    FModuleOp firrtlMod;
    StringRef targetModuleName;
    if (auto extModOp = dyn_cast<ExtModuleFirrtlOp>(referencedModule.getOperation())) {
      targetModuleName = extModOp.getExtModuleName();
      firrtlMod = findFIRRTLModule(targetModuleName, module->getParentOfType<mlir::ModuleOp>());
      if (!firrtlMod)
        return instOp.emitError("FIRRTL module not found: ") << targetModuleName;
    } else if (auto cmt2Mod = dyn_cast<cmt2::ModuleOp>(referencedModule.getOperation())) {
      targetModuleName = cmt2Mod.getSymName();
      auto it = convertedModules.find(cmt2Mod.getSymNameAttr());
      if (it == convertedModules.end())
        return instOp.emitError("Cmt2 module not yet converted: ") << targetModuleName;
      firrtlMod = it->second;
    } else {
      return instOp.emitError("Unsupported module type for instantiation");
    }

    // Create the FIRRTL instance
    SmallVector<PortInfo> ports = firrtlMod.getPorts();
    auto firrtlInst = builder.create<firrtl::InstanceOp>(
        instOp.getLoc(), ports, targetModuleName, instOp.getSymName(), NameKindEnum::DroppableName);
    ctx.registerInstance(instOp.getSymNameAttr(), firrtlInst);

    // Track connected ports
    llvm::SmallDenseSet<size_t> connectedPorts;

    // Connect module arguments to FIRRTL ports
    connectInstanceModuleArguments(instOp, firrtlInst, referencedModule.getOperation(),
                                    ctx, builder, connectedPorts);

    // Initialize unconnected input ports with default values
    initializeUnconnectedInputPorts(firrtlInst, ports, connectedPorts, builder);

    // Process interface bindings
    if (auto interfaceBinds = instOp.getInterfaceBinds()) {
      for (auto bindAttr : *interfaceBinds) {
        if (failed(processInterfaceBinding(instOp, firrtlInst, cast<ArrayAttr>(bindAttr),
                                            module, ctx, builder, connectedPorts)))
          return failure();
      }
    }
  }

  return success();
}

// Helper: Connect module arguments (clock, reset, etc.) to FIRRTL ports
void LowerCmt2ToFIRRTLPass::connectInstanceModuleArguments(
    cmt2::InstanceOp instOp, firrtl::InstanceOp firrtlInst,
    Operation *referencedModule, ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder, llvm::SmallDenseSet<size_t> &connectedPorts) {

  auto instanceArgs = instOp.getArgs();
  if (auto extModOp = dyn_cast<ExtModuleFirrtlOp>(referencedModule)) {
    // External FIRRTL module - use bind bare operations to get port names
    size_t barePortIdx = 0;
    for (auto &bodyOp : extModOp.getBodyRegion().front()) {
      auto bindBare = dyn_cast<BindBareOp>(bodyOp);
      if (!bindBare || barePortIdx >= instanceArgs.size())
        continue;
      Value firrtlArg = ctx.getIRMapping().lookupOrDefault(instanceArgs[barePortIdx]);
      if (auto portIdx = getPortIndex(firrtlInst, bindBare.getPortAttr().getAttr())) {
        builder.create<ConnectOp>(instOp.getLoc(), firrtlInst.getResult(*portIdx), firrtlArg);
        connectedPorts.insert(*portIdx);
      }
      barePortIdx++;
    }
  } else if (auto cmt2Mod = dyn_cast<cmt2::ModuleOp>(referencedModule)) {
    // Regular cmt2 module - connect arguments directly (first N ports)
    size_t numModuleArgs = cmt2Mod.getBodyRegion().front().getNumArguments();
    for (size_t i = 0; i < instanceArgs.size() && i < numModuleArgs; ++i) {
      if (Value firrtlArg = ctx.getIRMapping().lookupOrDefault(instanceArgs[i])) {
        builder.create<ConnectOp>(instOp.getLoc(), firrtlInst.getResult(i), firrtlArg);
        connectedPorts.insert(i);
      }
    }
  }
}

// Helper: Initialize all unconnected input ports with default values
void LowerCmt2ToFIRRTLPass::initializeUnconnectedInputPorts(
    firrtl::InstanceOp firrtlInst, const SmallVector<PortInfo> &ports,
    const llvm::SmallDenseSet<size_t> &connectedPorts, ImplicitLocOpBuilder &builder) {

  for (size_t i = 0; i < ports.size(); ++i) {
    if (connectedPorts.contains(i) || ports[i].direction != Direction::In)
      continue;

    Value instPort = firrtlInst.getResult(i);
    Type portType = instPort.getType();

    // Create default zero value for UInt/SInt types
    if (auto uintType = dyn_cast<UIntType>(portType)) {
      Value zero = builder.create<ConstantOp>(
          firrtlInst.getLoc(), uintType, APInt(uintType.getWidth().value_or(1), 0));
      builder.create<ConnectOp>(firrtlInst.getLoc(), instPort, zero);
    } else if (auto sintType = dyn_cast<SIntType>(portType)) {
      Value zero = builder.create<ConstantOp>(
          firrtlInst.getLoc(), sintType, APInt(sintType.getWidth().value_or(1), 0));
      builder.create<ConnectOp>(firrtlInst.getLoc(), instPort, zero);
    }
    // Clock/Reset types are typically connected via bare args
  }
}

// Helper: Process a single interface binding
LogicalResult LowerCmt2ToFIRRTLPass::processInterfaceBinding(
    cmt2::InstanceOp instOp, firrtl::InstanceOp firrtlInst, ArrayAttr interfaceBindAttr,
    cmt2::ModuleOp module, ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder, llvm::SmallDenseSet<size_t> &connectedPorts) {

  if (interfaceBindAttr.size() < 2)
    return success();

  // Parse: [@interfaceDefName, @interfaceDeclName]
  auto defRef = cast<mlir::SymbolRefAttr>(interfaceBindAttr[0]);
  auto declRef = cast<mlir::SymbolRefAttr>(interfaceBindAttr[1]);

  // Find InterfaceDefOp in current module
  InterfaceDefOp interfaceDef;
  for (auto &op : module.getBodyRegion().front()) {
    if (auto def = dyn_cast<InterfaceDefOp>(op)) {
      if (def.getSymNameAttr() == defRef.getLeafReference()) {
        interfaceDef = def;
        break;
      }
    }
  }
  if (!interfaceDef)
    return instOp.emitError("InterfaceDefOp not found: ") << defRef;

  // Find InterfaceOp to get method signatures
  auto circuit = module->getParentOfType<cmt2::CircuitOp>();
  InterfaceOp iface;
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto ifaceOp = dyn_cast<InterfaceOp>(op)) {
      if (ifaceOp.getSymNameAttr() == interfaceDef.getInterface().getLeafReference()) {
        iface = ifaceOp;
        break;
      }
    }
  }
  if (!iface)
    return instOp.emitError("InterfaceOp not found: ") << interfaceDef.getInterface();

  // Process each method/value mapping in the interface definition
  for (auto methodEntry : interfaceDef.getMethods()) {
    auto methodArray = cast<mlir::ArrayAttr>(methodEntry);
    if (methodArray.size() < 3)
      continue;

    // Parse: [@instance, @instanceMethod, @interfaceMethod]
    auto instanceRef = cast<mlir::SymbolRefAttr>(methodArray[0]);
    auto instanceMethodRef = cast<mlir::SymbolRefAttr>(methodArray[1]);
    auto ifaceMethodRef = cast<mlir::SymbolRefAttr>(methodArray[2]);

    // Find the interface method/value definition
    Cmt2FunctionLike ifaceFunc;
    for (auto &op : iface.getBodyRegion().front()) {
      if (auto func = dyn_cast<Cmt2FunctionLike>(op)) {
        if (func.functionNameAttr() == ifaceMethodRef.getLeafReference()) {
          ifaceFunc = func;
          break;
        }
      }
    }
    if (!ifaceFunc)
      return instOp.emitError("Interface method not found: ") << ifaceMethodRef;

    // Get the source instance (providing the implementation)
    firrtl::InstanceOp sourceInst = ctx.getInstance(instanceRef.getLeafReference());
    if (!sourceInst)
      return instOp.emitError("Source instance not found: ") << instanceRef;

    // Find the cmt2 instance to check if it's external FIRRTL
    cmt2::InstanceOp sourceCmt2Inst;
    for (auto &op : module.getBodyRegion().front()) {
      if (auto inst = dyn_cast<cmt2::InstanceOp>(op)) {
        if (inst.getSymNameAttr() == instanceRef.getLeafReference()) {
          sourceCmt2Inst = inst;
          break;
        }
      }
    }

    // Extract bind operations for external FIRRTL modules
    bool isExternalFirrtl = false;
    BindMethodOp parentBindMethod;
    BindValueOp parentBindValue;
    if (sourceCmt2Inst) {
      if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(sourceCmt2Inst.getReferencedModule().getOperation())) {
        isExternalFirrtl = true;
        for (auto &op : extMod.getBodyRegion().front()) {
          if (auto bm = dyn_cast<BindMethodOp>(op)) {
            if (bm.getSymNameAttr() == instanceMethodRef.getLeafReference())
              parentBindMethod = bm;
          } else if (auto bv = dyn_cast<BindValueOp>(op)) {
            if (bv.getSymNameAttr() == instanceMethodRef.getLeafReference())
              parentBindValue = bv;
          }
        }
      }
    }

    // Connect interface ports
    Operation *parentBindOp = ifaceFunc.getFunctionKind() == FunctionKind::Method
        ? parentBindMethod.getOperation()
        : parentBindValue.getOperation();
    if (failed(connectInterfaceBinding(
            firrtlInst, sourceInst,
            declRef.getLeafReference().getValue(),
            ifaceMethodRef.getLeafReference().getValue(),
            instanceMethodRef.getLeafReference().getValue(),
            ifaceFunc, parentBindOp, isExternalFirrtl, builder, connectedPorts)))
      return failure();
  }

  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::connectOutputPorts(cmt2::ModuleOp module,
                                                          ModuleConversionContext &ctx,
                                                          ImplicitLocOpBuilder &builder) {
  FModuleOp firrtlModule = ctx.getFIRRTLModule();
  size_t portIndex = module.getBodyRegion().front().getNumArguments();

  // Connect generated signals to module output ports
  for (auto &op : module.getBodyRegion().front()) {
    auto func = dyn_cast<Cmt2FunctionLike>(op);
    if (!func)
      continue;

    if (func.getFunctionKind() == FunctionKind::Method) {
      // Skip enable input port
      portIndex++;

      // Connect ready output
      Value readyPort = firrtlModule.getBodyBlock()->getArgument(portIndex++);
      if (Value readySignal = ctx.getSignalTracker().getReady(func.functionNameAttr())) {
        builder.create<ConnectOp>(func.getLoc(), readyPort, readySignal);
      }

      // Skip arg input ports
      auto funcType = cast<FunctionType>(func.getFunctionType());
      portIndex += funcType.getInputs().size();

      // Connect result outputs
      ArrayRef<Value> bodyResults = ctx.getSignalTracker().getBodyResults(func.functionNameAttr());
      for (auto [idx, _] : llvm::enumerate(funcType.getResults())) {
        Value resultPort = firrtlModule.getBodyBlock()->getArgument(portIndex++);
        if (idx < bodyResults.size() && bodyResults[idx]) {
          builder.create<ConnectOp>(func.getLoc(), resultPort, bodyResults[idx]);
        }
      }

    } else if (func.getFunctionKind() == FunctionKind::Value) {
      // Connect ready output
      Value readyPort = firrtlModule.getBodyBlock()->getArgument(portIndex++);
      if (Value readySignal = ctx.getSignalTracker().getReady(func.functionNameAttr())) {
        builder.create<ConnectOp>(func.getLoc(), readyPort, readySignal);
      }

      // Connect result outputs
      auto funcType = cast<FunctionType>(func.getFunctionType());
      ArrayRef<Value> bodyResults = ctx.getSignalTracker().getBodyResults(func.functionNameAttr());
      for (auto [idx, _] : llvm::enumerate(funcType.getResults())) {
        Value resultPort = firrtlModule.getBodyBlock()->getArgument(portIndex++);
        if (idx < bodyResults.size() && bodyResults[idx]) {
          builder.create<ConnectOp>(func.getLoc(), resultPort, bodyResults[idx]);
        }
      }
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Function-Level Operations
//===----------------------------------------------------------------------===//

LogicalResult LowerCmt2ToFIRRTLPass::processFunction(Cmt2FunctionLike func,
                                                   ModuleConversionContext &ctx,
                                                   ImplicitLocOpBuilder &builder) {

  // Map function parameters to FIRRTL module ports
  if (!func.isExternal()) {
    mapFunctionArgumentsToports(func, ctx, builder);
  }

  // Clone guard region to compute guard condition
  SmallVector<Value> guardResults;
  if (!func.isExternal()) {
    Region &guardRegion = func->getRegion(0);
    if (failed(cloneRegionOps(guardRegion, ctx, builder, guardResults))) {
      return failure();
    }
  }

  // Default guard to true if not specified
  Value guardResult = guardResults.empty()
      ? builder.create<ConstantOp>(func.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 1))
      : guardResults[0];

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

  // Generate control signals
  Value readySignal = generateReadySignal(func, guardResult, *containingGroup, ctx, builder);
  ctx.getSignalTracker().setReady(func.functionNameAttr(), readySignal);

  Value fireSignal = generateFireSignal(func, readySignal, ctx, builder);
  ctx.getSignalTracker().setFire(func.functionNameAttr(), fireSignal);

  // Clone body region inside firrtl.when(fire) block
  if (!func.isExternal() && func->getNumRegions() > 1) {
    Region &bodyRegion = func->getRegion(1);

    // Get result names if available
    ArrayAttr bodyResNames = getBodyResNames(func);

    // Create wires for result values (to escape the when block's region)
    // Only create wires for Methods and Values (which have output ports), not for Rules
    auto funcType = cast<FunctionType>(func.getFunctionType());
    SmallVector<Value> resultWires;
    bool needsResultWires = (func.getFunctionKind() == FunctionKind::Method ||
                             func.getFunctionKind() == FunctionKind::Value) &&
                            !funcType.getResults().empty();

    if (needsResultWires) {
      for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
        auto firrtlType = cast<FIRRTLBaseType>(resType);
        StringRef resName = bodyResNames && idx < bodyResNames.size()
                                ? cast<StringAttr>(bodyResNames[idx]).getValue()
                                : ("result" + std::to_string(idx));
        auto wire = builder.create<WireOp>(func.getLoc(), firrtlType,
                                            builder.getStringAttr(func.functionName().str() + "_" + resName.str()));

        // Initialize wire with invalidvalue to satisfy FIRRTL's full initialization requirement
        Value invalid = builder.create<InvalidValueOp>(func.getLoc(), firrtlType);
        builder.create<ConnectOp>(func.getLoc(), wire.getResult(), invalid);

        resultWires.push_back(wire.getResult());
      }
    }

    auto whenOp = builder.create<WhenOp>(func.getLoc(), fireSignal, /*withElseRegion=*/false);

    OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(&whenOp.getThenBlock());

    SmallVector<Value> bodyResults;
    if (failed(cloneRegionOps(bodyRegion, ctx, builder, bodyResults))) {
      return failure();
    }

    // Connect body results to wires inside the when block (only if there are results)
    if (!resultWires.empty()) {
      for (auto [wire, result] : llvm::zip(resultWires, bodyResults)) {
        if (result) {
          builder.create<ConnectOp>(func.getLoc(), wire, result);
        }
      }
    }

    // Store the wires (not the body results) for later connection to ports
    ctx.getSignalTracker().setBodyResults(func.functionNameAttr(), resultWires);
  }

  return success();
}

void LowerCmt2ToFIRRTLPass::mapFunctionArgumentsToports(Cmt2FunctionLike func,
                                                         ModuleConversionContext &ctx,
                                                         OpBuilder &builder) {
  FModuleOp firrtlModule = ctx.getFIRRTLModule();
  auto argNames = getArgNames(func);

  // Map block arguments from all regions (guard and body) to FIRRTL ports
  for (Region &region : func->getRegions()) {
    if (region.empty())
      continue;

    for (auto [idx, blockArg] : llvm::enumerate(region.front().getArguments())) {
      // Use the same naming convention as createFunctionPorts
      StringRef argName = idx < argNames.size()
                              ? cast<StringAttr>(argNames[idx]).getValue()
                              : ("arg" + std::to_string(idx));
      StringAttr argPortName = builder.getStringAttr(
          func.functionName().str() + "_" + argName.str());

      // Find the corresponding port in the FIRRTL module
      for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts(); ++portIdx) {
        if (firrtlModule.getPortName(portIdx) == argPortName) {
          ctx.getIRMapping().map(blockArg, firrtlModule.getArgument(portIdx));
          break;
        }
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Signal Generation
//===----------------------------------------------------------------------===//

Value LowerCmt2ToFIRRTLPass::generateReadySignal(
    Cmt2FunctionLike func, Value guardResult,
    // const SmallVector<CallInfo> &calls,
    const ScheduleGroup &group,
    ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder) {

  // ready = guard ∧ called_readies ∧ ¬(conflicting_fires)
  Value ready = guardResult;

  FModuleOp firrtlModule = ctx.getFIRRTLModule();

  // AND with ready signals of called functions
  // for (const auto &call : calls) {
  func->walk([&](cmt2::CallOp call) {
    // if (auto call = dyn_cast<cmt2::CallOp>(op)) {
      // Check if this is an interface call
      InterfaceDeclOp interfaceDecl;
      for (auto &op : ctx.getCmt2Module().getBodyRegion().front()) {
        if (auto decl = dyn_cast<InterfaceDeclOp>(op)) {
          if (decl.getSymNameAttr() == call.getCallee().getLeafReference()) {
            interfaceDecl = decl;
            break;
          }
        }
      }

      if (interfaceDecl) {
        // Interface call - get ready from interface port
        StringRef declName = interfaceDecl.getSymName();
        StringRef methodName = call.getMethodOrValue().getLeafReference().getValue();
        StringAttr readyPortName = builder.getStringAttr(
            declName.str() + "_" + methodName.str() + "_ready");

        Value readyPort = nullptr;
        for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts(); ++portIdx) {
          if (firrtlModule.getPortName(portIdx) == readyPortName) {
            readyPort = firrtlModule.getArgument(portIdx);
            break;
          }
        }

        if (readyPort) {
          ready = builder.create<AndPrimOp>(func.getLoc(), ready, readyPort);
        } 
      } else {
        // Regular instance call - external or cmt2.module

        auto instanceName = call.getCallee().getRootReference();
        auto methodName = call.getMethodOrValue().getLeafReference();
        firrtl::InstanceOp firrtlInst = ctx.getInstance(instanceName);

        cmt2::InstanceOp cmt2Inst;
        
        for (auto &op : ctx.getCmt2Module().getBodyRegion().front()) {
          if (auto inst = dyn_cast<cmt2::InstanceOp>(op)) {
            if (inst.getSymNameAttr() == instanceName) {
              cmt2Inst = inst;
              break;
            }
          }
        }

        auto referencedModule = cmt2Inst.getReferencedModule();

        if (auto extModOp = dyn_cast<ExtModuleFirrtlOp>(referencedModule.getOperation())) {
          // External FIRRTL module - use bind operations
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

          std::optional<llvm::StringRef> readyName;
          if (auto bindMethod = dyn_cast<BindMethodOp>(bindFunc.getOperation())) {
            readyName = bindMethod.getReady();
          } else if (auto bindValue = dyn_cast<BindValueOp>(bindFunc.getOperation())) {
            readyName = bindValue.getReady();
          }

          if (readyName) {
            auto readyPortName = builder.getStringAttr(readyName.value());
            if (auto readyPortIdx = getPortIndex(firrtlInst, readyPortName)) {
              LLVM_DEBUG(llvm::dbgs() << "ready port found from external: " << readyPortName << "\n");
              ready = builder.create<AndPrimOp>(call.getLoc(), ready, firrtlInst.getResult(*readyPortIdx));
            }
          }
        } else if (auto cmt2Mod = dyn_cast<cmt2::ModuleOp>(referencedModule.getOperation())) {
          Cmt2FunctionLike targetFunc;
          for (auto &op : cmt2Mod.getBodyRegion().front()) {
            if (auto func = dyn_cast<Cmt2FunctionLike>(op)) {
              if (func.functionNameAttr() == methodName) {
                targetFunc = func;
                break;
              }
            }
          }

          StringAttr readyPortName = builder.getStringAttr(methodName.str() + "_ready");
          if (auto readyPortIdx = getPortIndex(firrtlInst, readyPortName)) {
            LLVM_DEBUG(llvm::dbgs() << "ready port found for " << referencedModule.moduleName() << " : " << readyPortName << "\n");
            ready = builder.create<AndPrimOp>(call.getLoc(), ready, firrtlInst.getResult(*readyPortIdx));
          }
        }
        // // std::string readyPortName = call.calleeEntity.getLeafReference().str() + "_ready";
        // firrtl::InstanceOp firrtlInst = ctx.getInstance(call.calleeInstance.getLeafReference());

        // if (Value calleeReady = getInstancePort(readyPortName, ctx.getInstance(call.calleeInstance.getLeafReference()), ctx, builder)) {
        //   ready = builder.create<AndPrimOp>(func.getLoc(), ready, calleeReady);
        // }
      }
    // }

  });

  // AND with NOT(preceding conflicting functions fired)
  // Only ConflictMatrix relationships prevent concurrent firing
  const auto &funcs = group.getFunctions();
  auto funcIt = llvm::find(funcs, func.functionNameAttr());
  if (funcIt != funcs.end()) {
    for (auto it = funcs.begin(); it != funcIt; ++it) {
      StringAttr precedingFunc = *it;
      auto rel = ctx.getConflictMatrix()->getRelationship(precedingFunc, func.functionNameAttr());

      // If preceding function conflicts or must execute before, ensure it hasn't fired
      if (rel == Relationship::Conflict || rel == Relationship::SequentialBefore) {
        if (Value precedingFire = ctx.getSignalTracker().getFire(precedingFunc)) {
          Value notFired = builder.create<XorPrimOp>(func.getLoc(), precedingFire,
              builder.create<ConstantOp>(func.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 1)));
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

  // For methods: fire = ready ∧ enable
  // For rules/values: fire = ready
  if (func.getFunctionKind() == FunctionKind::Method) {
    Value enableSignal = ctx.getSignalTracker().getEnable(func.functionNameAttr());

    if (!enableSignal) {
      // Find enable port in FIRRTL module
      FModuleOp firrtlModule = ctx.getFIRRTLModule();
      StringAttr enablePortName = builder.getStringAttr(func.functionName().str() + "_enable");

      for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts(); ++portIdx) {
        if (firrtlModule.getPortName(portIdx) == enablePortName) {
          enableSignal = firrtlModule.getArgument(portIdx);
          ctx.getSignalTracker().setEnable(func.functionNameAttr(), enableSignal);
          break;
        }
      }

      // Default to 0 if not found (method never fires)
      if (!enableSignal) {
        enableSignal = builder.create<ConstantOp>(
            func.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 0));
      }
    }

    return builder.create<AndPrimOp>(func.getLoc(), readySignal, enableSignal);
  }

  return readySignal;
}

//===----------------------------------------------------------------------===//
// Region Cloning and Call Conversion
//===----------------------------------------------------------------------===//

LogicalResult LowerCmt2ToFIRRTLPass::cloneRegionOps(
    Region &sourceRegion,
    ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder,
    SmallVectorImpl<Value> &results) {

  if (sourceRegion.empty())
    return success();

  Block &sourceBlock = sourceRegion.front();

  // Clone each operation, handling cmt2.return, cmt2.call, and cmt2.if specially
  for (Operation &op : sourceBlock) {
    if (auto returnOp = dyn_cast<ReturnOp>(op)) {
      // Collect return values as results
      for (Value result : returnOp.getOperands()) {
        results.push_back(ctx.getIRMapping().lookupOrDefault(result));
      }
      continue;
    }

    if (auto callOp = dyn_cast<CallOp>(op)) {
      // Convert calls to FIRRTL signal connections
      if (failed(convertCallOp(callOp, ctx, builder))) {
        return failure();
      }
      continue;
    }

    if (auto ifOp = dyn_cast<IfOp>(op)) {
      // Convert cmt2.if to firrtl.when
      Value condition = ctx.getIRMapping().lookupOrDefault(ifOp.getCondition());
      bool hasElse = !ifOp.getElseRegion().empty();

      // Create result wires if the if operation has results
      SmallVector<Value> resultWires;
      for (auto resultType : ifOp.getResults().getTypes()) {
        auto wire = builder.create<WireOp>(
            ifOp.getLoc(),
            resultType,
            builder.getStringAttr("if_result"));
        resultWires.push_back(wire.getResult());
      }

      auto whenOp = builder.create<WhenOp>(ifOp.getLoc(), condition, hasElse);

      // Clone then region
      {
        OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(&whenOp.getThenBlock());

        SmallVector<Value> thenResults;
        if (failed(cloneRegionOps(ifOp.getThenRegion(), ctx, builder, thenResults))) {
          return failure();
        }

        // Connect then results to wires
        for (auto [wire, result] : llvm::zip(resultWires, thenResults)) {
          if (result) {
            builder.create<ConnectOp>(ifOp.getLoc(), wire, result);
          }
        }
      }

      // Clone else region if present
      if (hasElse) {
        OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(&whenOp.getElseBlock());

        SmallVector<Value> elseResults;
        if (failed(cloneRegionOps(ifOp.getElseRegion(), ctx, builder, elseResults))) {
          return failure();
        }

        // Connect else results to wires
        for (auto [wire, result] : llvm::zip(resultWires, elseResults)) {
          if (result) {
            builder.create<ConnectOp>(ifOp.getLoc(), wire, result);
          }
        }
      }

      // Map the if results to the wires
      for (auto [oldResult, newResult] : llvm::zip(ifOp.getResults(), resultWires)) {
        ctx.getIRMapping().map(oldResult, newResult);
      }

      continue;
    }

    if (auto yieldOp = dyn_cast<YieldOp>(op)) {
      // Collect yield values as results (similar to return)
      for (Value result : yieldOp.getOperands()) {
        results.push_back(ctx.getIRMapping().lookupOrDefault(result));
      }
      continue;
    }

    // Clone other operations normally
    Operation *cloned = builder.clone(op, ctx.getIRMapping());
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

  // Get callee information from the call
  StringAttr instanceName = callOp.getCallee().getRootReference();
  StringAttr methodName = callOp.getMethodOrValue().getLeafReference();

  // Check if this is an interface call first
  if (isInterfaceCall(callOp, ctx.getCmt2Module())) {
    // Find the InterfaceDeclOp
    InterfaceDeclOp interfaceDecl;
    for (auto &op : ctx.getCmt2Module().getBodyRegion().front()) {
      if (auto decl = dyn_cast<InterfaceDeclOp>(op)) {
        if (decl.getSymNameAttr() == instanceName) {
          interfaceDecl = decl;
          break;
        }
      }
    }

    if (!interfaceDecl) {
      return callOp.emitError("Interface declaration not found: ") << instanceName;
    }

    return connectInterfaceCall(callOp, interfaceDecl, ctx, builder);
  }

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

  auto referencedModule = cmt2Inst.getReferencedModule();
  if (!referencedModule) {
    return callOp.emitError("Referenced module not found for instance: ") << instanceName;
  }

  // Check if it's an external FIRRTL module or a regular cmt2 module
  if (auto extModOp = dyn_cast<ExtModuleFirrtlOp>(referencedModule.getOperation())) {
    // External FIRRTL module - use bind operations
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

    // Dispatch to method or value conversion
    if (auto bindMethod = dyn_cast<BindMethodOp>(bindFunc.getOperation())) {
      return connectMethodCall(callOp, bindMethod, firrtlInst, ctx, builder);
    } else if (auto bindValue = dyn_cast<BindValueOp>(bindFunc.getOperation())) {
      return connectValueCall(callOp, bindValue, firrtlInst, ctx, builder);
    }
  } else if (auto cmt2Mod = dyn_cast<cmt2::ModuleOp>(referencedModule.getOperation())) {
    // Regular cmt2 module - directly access method/value ports
    // Find the function in the module to determine if it's a method or value
    Cmt2FunctionLike targetFunc;
    for (auto &op : cmt2Mod.getBodyRegion().front()) {
      if (auto func = dyn_cast<Cmt2FunctionLike>(op)) {
        if (func.functionNameAttr() == methodName) {
          targetFunc = func;
          break;
        }
      }
    }

    if (!targetFunc) {
      return callOp.emitError("Function not found in module: ") << methodName;
    }

    auto targetArgNames = getArgNames(targetFunc);
    auto targetBodyResNames = getBodyResNames(targetFunc);

    SmallVector<Value> mappedResults;

    if (targetFunc.getFunctionKind() == FunctionKind::Method) {
      // Drive enable signal
      StringAttr enablePortName = builder.getStringAttr(methodName.str() + "_enable");
      if (auto enablePortIdx = getPortIndex(firrtlInst, enablePortName)) {
        Value one = builder.create<ConstantOp>(
            callOp.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 1));
        builder.create<ConnectOp>(callOp.getLoc(), firrtlInst.getResult(*enablePortIdx), one);
      } else {
        return callOp.emitError("Enable port not found: ") << enablePortName;
      }

      // Connect input arguments using proper names
      for (auto [idx, operand] : llvm::enumerate(callOp.getOperands())) {
        StringRef argName = idx < targetArgNames.size()
                                ? cast<StringAttr>(targetArgNames[idx]).getValue()
                                : ("arg" + std::to_string(idx));
        StringAttr argPortName = builder.getStringAttr(
            methodName.str() + "_" + argName.str());
        if (auto portIdx = getPortIndex(firrtlInst, argPortName)) {
          Value mappedOperand = ctx.getIRMapping().lookupOrDefault(operand);
          if (!mappedOperand) {
            return callOp.emitError("Call operand was not properly mapped to FIRRTL context");
          }
          builder.create<ConnectOp>(callOp.getLoc(), firrtlInst.getResult(*portIdx), mappedOperand);
        } else {
          return callOp.emitError("Argument port not found: ") << argPortName;
        }
      }

      // Read output results using proper names
      for (size_t idx = 0; idx < callOp.getNumResults(); ++idx) {
        StringRef resName = targetBodyResNames && idx < targetBodyResNames.size()
                                ? cast<StringAttr>(targetBodyResNames[idx]).getValue()
                                : ("result" + std::to_string(idx));
        StringAttr resultPortName = builder.getStringAttr(
            methodName.str() + "_" + resName.str());
        if (auto portIdx = getPortIndex(firrtlInst, resultPortName)) {
          mappedResults.push_back(firrtlInst.getResult(*portIdx));
        } else {
          return callOp.emitError("Result port not found: ") << resultPortName;
        }
      }
    } 
    
    else if (targetFunc.getFunctionKind() == FunctionKind::Value) {
      // Read data results directly (values have no enable signal) using proper names
      for (size_t idx = 0; idx < callOp.getNumResults(); ++idx) {
        StringRef resName = targetBodyResNames && idx < targetBodyResNames.size()
                                ? cast<StringAttr>(targetBodyResNames[idx]).getValue()
                                : ("result" + std::to_string(idx));
        StringAttr resultPortName = builder.getStringAttr(
            methodName.str() + "_" + resName.str());
        if (auto portIdx = getPortIndex(firrtlInst, resultPortName)) {
          mappedResults.push_back(firrtlInst.getResult(*portIdx));
        } else {
          return callOp.emitError("Result port not found: ") << resultPortName;
        }
      }
    }

    // Map call results to instance ports
    for (auto [callResult, portValue] : llvm::zip(callOp.getResults(), mappedResults)) {
      ctx.getIRMapping().map(callResult, portValue);
    }

    return success();
  } else {
    return callOp.emitError("Unsupported module type for call");
  }

  return failure();
}

LogicalResult LowerCmt2ToFIRRTLPass::connectMethodCall(
    CallOp callOp, BindMethodOp bindMethod,
    firrtl::InstanceOp firrtlInst,
    ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder) {

  // Drive enable signal to 1
  if (auto enableAttr = bindMethod.getEnable()) {
    StringAttr enablePortName = builder.getStringAttr(enableAttr.value());
    if (auto enablePortIdx = getPortIndex(firrtlInst, enablePortName)) {
      Value one = builder.create<ConstantOp>(
          callOp.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 1));
      builder.create<ConnectOp>(callOp.getLoc(), firrtlInst.getResult(*enablePortIdx), one);
    } else {
      return callOp.emitError("Enable port not found: ") << enablePortName;
    }
  }

  // Connect input arguments
  auto inputsAttr = bindMethod.getInputs();
  if (inputsAttr.size() != callOp.getNumOperands()) {
    return callOp.emitError("Operand count mismatch: expected ")
           << inputsAttr.size() << " but got " << callOp.getNumOperands();
  }

  for (auto [operand, inputAttr] : llvm::zip(callOp.getOperands(), inputsAttr)) {
    auto portAttr = cast<FlatSymbolRefAttr>(inputAttr);
    auto portIdx = getPortIndex(firrtlInst, portAttr.getAttr());
    if (!portIdx) {
      return callOp.emitError("Input port not found: ") << portAttr;
    }

    Value mappedOperand = ctx.getIRMapping().lookupOrDefault(operand);
    if (!mappedOperand) {
      return callOp.emitError("Call operand was not properly mapped to FIRRTL context");
    }

    builder.create<ConnectOp>(callOp.getLoc(), firrtlInst.getResult(*portIdx), mappedOperand);
  }

  // Read output results
  auto outputsAttr = bindMethod.getOutputs();
  if (outputsAttr.size() != callOp.getNumResults()) {
    return callOp.emitError("Result count mismatch: expected ")
           << outputsAttr.size() << " but got " << callOp.getNumResults();
  }

  SmallVector<Value> mappedResults;
  for (auto outputAttr : outputsAttr) {
    auto portAttr = cast<FlatSymbolRefAttr>(outputAttr);
    auto portIdx = getPortIndex(firrtlInst, portAttr.getAttr());
    if (!portIdx) {
      return callOp.emitError("Output port not found: ") << portAttr;
    }
    mappedResults.push_back(firrtlInst.getResult(*portIdx));
  }

  // Map call results to instance ports
  for (auto [callResult, portValue] : llvm::zip(callOp.getResults(), mappedResults)) {
    ctx.getIRMapping().map(callResult, portValue);
  }

  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::connectValueCall(
    CallOp callOp, BindValueOp bindValue,
    firrtl::InstanceOp firrtlInst,
    ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder) {

  // Read data results directly (values have no enable signal)
  auto dataAttr = bindValue.getData();
  if (dataAttr.size() != callOp.getNumResults()) {
    return callOp.emitError("Result count mismatch: expected ")
           << dataAttr.size() << " but got " << callOp.getNumResults();
  }

  SmallVector<Value> mappedResults;
  for (auto dataPortAttr : dataAttr) {
    auto portAttr = cast<FlatSymbolRefAttr>(dataPortAttr);
    auto portIdx = getPortIndex(firrtlInst, portAttr.getAttr());
    if (!portIdx) {
      return callOp.emitError("Data port not found: ") << portAttr;
    }
    mappedResults.push_back(firrtlInst.getResult(*portIdx));
  }

  // Map call results to instance ports
  for (auto [callResult, portValue] : llvm::zip(callOp.getResults(), mappedResults)) {
    ctx.getIRMapping().map(callResult, portValue);
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Helper Utilities
//===----------------------------------------------------------------------===//

FModuleOp LowerCmt2ToFIRRTLPass::findFIRRTLModule(StringRef moduleName,
                                                    Operation *searchRoot) {
  FModuleOp result;

  // Search for FIRRTL module in circuits
  searchRoot->walk([&](firrtl::CircuitOp circuit) {
    circuit.walk([&](FModuleOp mod) {
      if (mod.getModuleName() == moduleName) {
        result = mod;
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
    return result ? WalkResult::interrupt() : WalkResult::advance();
  });

  // If not found in circuits, search directly
  if (!result) {
    searchRoot->walk([&](FModuleOp mod) {
      if (mod.getModuleName() == moduleName) {
        result = mod;
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
  }

  return result;
}

std::optional<size_t> LowerCmt2ToFIRRTLPass::getPortIndex(
    firrtl::InstanceOp inst, StringAttr portName) {
  for (size_t i = 0; i < inst.getNumResults(); ++i) {
    if (inst.getPortName(i) == portName) {
      return i;
    }
  }
  return std::nullopt;
}

cmt2::ModuleOp LowerCmt2ToFIRRTLPass::findTopModule(cmt2::CircuitOp circuit) {
  // Collect all instantiated module names
  DenseSet<StringAttr> instantiatedModules;
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      module.walk([&](cmt2::InstanceOp inst) {
        if (auto refModule = inst.getReferencedModule()) {
          instantiatedModules.insert(refModule.moduleNameAttr());
        }
      });
    }
  }

  // Find module not instantiated by any other (the top module)
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      if (!instantiatedModules.contains(module.getSymNameAttr())) {
        return module;
      }
    }
  }

  return cmt2::ModuleOp();
}

ArrayAttr LowerCmt2ToFIRRTLPass::getArgNames(Cmt2FunctionLike func) {
  if (auto method = dyn_cast<MethodOp>(func.getOperation()))
    return method.getArgNames();
  if (auto value = dyn_cast<ValueOp>(func.getOperation()))
    return value.getArgNames();
  if (auto rule = dyn_cast<RuleOp>(func.getOperation()))
    return rule.getArgNames();
  // BindMethodOp and BindValueOp don't have argNames
  return ArrayAttr();
}

ArrayAttr LowerCmt2ToFIRRTLPass::getBodyResNames(Cmt2FunctionLike func) {
  if (auto method = dyn_cast<MethodOp>(func.getOperation()))
    return method.getBodyResNames();
  if (auto value = dyn_cast<ValueOp>(func.getOperation()))
    return value.getBodyResNames();
  // Rules and bind operations don't have bodyResNames
  return ArrayAttr();
}

std::string LowerCmt2ToFIRRTLPass::buildPortName(StringRef base, StringRef suffix) {
  return base.str() + "_" + suffix.str();
}

std::string LowerCmt2ToFIRRTLPass::buildInterfacePortName(StringRef declName,
                                                            StringRef methodName,
                                                            StringRef suffix) {
  return declName.str() + "_" + methodName.str() + "_" + suffix.str();
}

LogicalResult LowerCmt2ToFIRRTLPass::connectInterfaceBinding(
    firrtl::InstanceOp childInst, firrtl::InstanceOp parentInst,
    StringRef childDeclName, StringRef childMethodName, StringRef parentMethodName,
    Cmt2FunctionLike ifaceFunc, Operation *parentBindOp, bool isExternalFirrtl,
    ImplicitLocOpBuilder &builder, llvm::SmallDenseSet<size_t> &connectedPorts) {

  auto funcType = cast<FunctionType>(ifaceFunc.getFunctionType());
  Location loc = childInst.getLoc();

  // Determine if this is a method or value binding
  auto parentBindMethod = dyn_cast_or_null<BindMethodOp>(parentBindOp);
  auto parentBindValue = dyn_cast_or_null<BindValueOp>(parentBindOp);
  bool isMethod = ifaceFunc.getFunctionKind() == FunctionKind::Method;

  // Connect enable port (methods only, child out -> parent in)
  if (isMethod) {
    if (auto childIdx = getPortIndex(childInst, builder.getStringAttr(buildInterfacePortName(childDeclName, childMethodName, "enable")))) {
      StringAttr parentName = isExternalFirrtl && parentBindMethod && parentBindMethod.getEnable()
          ? builder.getStringAttr(parentBindMethod.getEnable()->str())
          : builder.getStringAttr(buildPortName(parentMethodName, "enable"));
      if (auto parentIdx = getPortIndex(parentInst, parentName)) {
        builder.create<ConnectOp>(loc, parentInst.getResult(*parentIdx), childInst.getResult(*childIdx));
        connectedPorts.insert(*childIdx);
      }
    }
  }

  // Connect ready port (parent out -> child in)
  if (auto childIdx = getPortIndex(childInst, builder.getStringAttr(buildInterfacePortName(childDeclName, childMethodName, "ready")))) {
    StringAttr parentName;
    if (isExternalFirrtl) {
      if (parentBindMethod && parentBindMethod.getReady())
        parentName = builder.getStringAttr(parentBindMethod.getReady()->str());
      else if (parentBindValue && parentBindValue.getReady())
        parentName = builder.getStringAttr(parentBindValue.getReady()->str());
    }
    if (!parentName)
      parentName = builder.getStringAttr(buildPortName(parentMethodName, "ready"));

    if (auto parentIdx = getPortIndex(parentInst, parentName)) {
      builder.create<ConnectOp>(loc, childInst.getResult(*childIdx), parentInst.getResult(*parentIdx));
      connectedPorts.insert(*childIdx);
    }
  }

  // Connect argument ports (methods only, child out -> parent in)
  if (isMethod) {
    auto ifaceArgNames = getArgNames(ifaceFunc);
    auto inputsAttr = isExternalFirrtl && parentBindMethod ? parentBindMethod.getInputs() : ArrayAttr();

    for (auto [idx, argType] : llvm::enumerate(funcType.getInputs())) {
      StringRef argName = idx < ifaceArgNames.size()
          ? cast<StringAttr>(ifaceArgNames[idx]).getValue()
          : ("arg" + std::to_string(idx));

      if (auto childIdx = getPortIndex(childInst, builder.getStringAttr(buildInterfacePortName(childDeclName, childMethodName, argName)))) {
        StringAttr parentName;
        if (isExternalFirrtl && inputsAttr && idx < inputsAttr.size()) {
          auto portAttr = cast<FlatSymbolRefAttr>(inputsAttr[idx]);
          parentName = builder.getStringAttr(portAttr.getValue().str());
        } else {
          parentName = builder.getStringAttr(buildPortName(parentMethodName, argName));
        }

        if (auto parentIdx = getPortIndex(parentInst, parentName)) {
          builder.create<ConnectOp>(loc, parentInst.getResult(*parentIdx), childInst.getResult(*childIdx));
          connectedPorts.insert(*childIdx);
        }
      }
    }
  }

  // Connect result ports (parent out -> child in)
  auto ifaceBodyResNames = getBodyResNames(ifaceFunc);
  ArrayAttr outputsAttr;
  if (isExternalFirrtl) {
    if (parentBindMethod)
      outputsAttr = parentBindMethod.getOutputs();
    else if (parentBindValue)
      outputsAttr = parentBindValue.getData();
  }

  for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
    StringRef resName = ifaceBodyResNames && idx < ifaceBodyResNames.size()
        ? cast<StringAttr>(ifaceBodyResNames[idx]).getValue()
        : ("result" + std::to_string(idx));

    if (auto childIdx = getPortIndex(childInst, builder.getStringAttr(buildInterfacePortName(childDeclName, childMethodName, resName)))) {
      StringAttr parentName;
      if (isExternalFirrtl && outputsAttr && idx < outputsAttr.size()) {
        auto portAttr = cast<FlatSymbolRefAttr>(outputsAttr[idx]);
        parentName = builder.getStringAttr(portAttr.getValue().str());
      } else {
        parentName = builder.getStringAttr(buildPortName(parentMethodName, resName));
      }

      if (auto parentIdx = getPortIndex(parentInst, parentName)) {
        builder.create<ConnectOp>(loc, childInst.getResult(*childIdx), parentInst.getResult(*parentIdx));
        connectedPorts.insert(*childIdx);
      }
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Interface Helpers
//===----------------------------------------------------------------------===//

bool LowerCmt2ToFIRRTLPass::isInterfaceCall(CallOp callOp, cmt2::ModuleOp module) {
  StringAttr calleeName = callOp.getCallee().getRootReference();

  // Check if the callee is an InterfaceDeclOp in the module
  for (auto &op : module.getBodyRegion().front()) {
    if (auto decl = dyn_cast<InterfaceDeclOp>(op)) {
      if (decl.getSymNameAttr() == calleeName) {
        return true;
      }
    }
  }

  return false;
}

InterfaceOp LowerCmt2ToFIRRTLPass::getInterfaceForDecl(InterfaceDeclOp decl) {
  auto circuit = decl->getParentOfType<cmt2::CircuitOp>();
  if (!circuit)
    return nullptr;

  // Look up the interface in the circuit
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto iface = dyn_cast<InterfaceOp>(op)) {
      if (iface.getSymNameAttr() == decl.getInterface().getLeafReference()) {
        return iface;
      }
    }
  }

  return nullptr;
}

void LowerCmt2ToFIRRTLPass::createInterfacePorts(cmt2::ModuleOp module,
                                                   OpBuilder &builder,
                                                   SmallVectorImpl<PortInfo> &ports) {
  // For each InterfaceDeclOp in the module, create ports for all methods/values
  for (auto &op : module.getBodyRegion().front()) {
    auto decl = dyn_cast<InterfaceDeclOp>(op);
    if (!decl)
      continue;

    InterfaceOp iface = getInterfaceForDecl(decl);
    if (!iface) {
      module.emitWarning("Interface not found for declaration: ") << decl.getSymName();
      continue;
    }

    StringRef declName = decl.getSymName();

    // For each method/value in the interface, create corresponding ports
    for (auto &ifaceOp : iface.getBodyRegion().front()) {
      if (auto method = dyn_cast<MethodOp>(ifaceOp)) {
        auto funcType = cast<FunctionType>(method.getFunctionType());
        StringRef methodName = method.getSymName();
        auto argNames = method.getArgNames();
        auto bodyResNames = method.getBodyResNames();

        // Methods: enable (in), ready (out), args (in), results (out)
        ports.push_back(PortInfo(
            builder.getStringAttr(declName.str() + "_" + methodName.str() + "_enable"),
            UIntType::get(builder.getContext(), 1),
            Direction::Out, {}, method.getLoc()));

        ports.push_back(PortInfo(
            builder.getStringAttr(declName.str() + "_" + methodName.str() + "_ready"),
            UIntType::get(builder.getContext(), 1),
            Direction::In, {}, method.getLoc()));

        // Arguments
        for (auto [idx, argType] : llvm::enumerate(funcType.getInputs())) {
          StringRef argName = idx < argNames.size()
                                  ? cast<StringAttr>(argNames[idx]).getValue()
                                  : ("arg" + std::to_string(idx));
          ports.push_back(PortInfo(
              builder.getStringAttr(declName.str() + "_" + methodName.str() + "_" + argName.str()),
              cast<FIRRTLBaseType>(argType), Direction::Out, {}, method.getLoc()));
        }

        // Results
        for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
          StringRef resName = bodyResNames && idx < bodyResNames.size()
                                  ? cast<StringAttr>(bodyResNames[idx]).getValue()
                                  : ("result" + std::to_string(idx));
          ports.push_back(PortInfo(
              builder.getStringAttr(declName.str() + "_" + methodName.str() + "_" + resName.str()),
              cast<FIRRTLBaseType>(resType), Direction::In, {}, method.getLoc()));
        }

      } else if (auto value = dyn_cast<ValueOp>(ifaceOp)) {
        auto funcType = cast<FunctionType>(value.getFunctionType());
        StringRef valueName = value.getSymName();
        auto bodyResNames = value.getBodyResNames();

        // Values: ready (out), results (out)
        ports.push_back(PortInfo(
            builder.getStringAttr(declName.str() + "_" + valueName.str() + "_ready"),
            UIntType::get(builder.getContext(), 1),
            Direction::In, {}, value.getLoc()));

        // Results
        for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
          StringRef resName = bodyResNames && idx < bodyResNames.size()
                                  ? cast<StringAttr>(bodyResNames[idx]).getValue()
                                  : ("result" + std::to_string(idx));
          ports.push_back(PortInfo(
              builder.getStringAttr(declName.str() + "_" + valueName.str() + "_" + resName.str()),
              cast<FIRRTLBaseType>(resType), Direction::In, {}, value.getLoc()));
        }
      }
    }
  }
}

LogicalResult LowerCmt2ToFIRRTLPass::connectInterfaceCall(
    CallOp callOp, InterfaceDeclOp interfaceDecl,
    ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder) {

  StringAttr declName = interfaceDecl.getSymNameAttr();
  StringAttr methodName = callOp.getMethodOrValue().getLeafReference();
  FModuleOp firrtlModule = ctx.getFIRRTLModule();

  // Find the method/value in the interface to determine its kind
  InterfaceOp iface = getInterfaceForDecl(interfaceDecl);
  if (!iface) {
    return callOp.emitError("Interface not found for declaration: ") << declName;
  }

  Cmt2FunctionLike targetFunc;
  for (auto &op : iface.getBodyRegion().front()) {
    if (auto func = dyn_cast<Cmt2FunctionLike>(op)) {
      if (func.functionNameAttr() == methodName) {
        targetFunc = func;
        break;
      }
    }
  }

  if (!targetFunc) {
    return callOp.emitError("Method/value not found in interface: ") << methodName;
  }

  auto targetArgNames = getArgNames(targetFunc);
  auto targetBodyResNames = getBodyResNames(targetFunc);

  // Find the corresponding ports in the FIRRTL module
  SmallVector<Value> mappedResults;

  if (targetFunc.getFunctionKind() == FunctionKind::Method) {
    // Drive enable signal
    StringAttr enablePortName = builder.getStringAttr(
        declName.str() + "_" + methodName.str() + "_enable");

    Value enablePort = nullptr;
    for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts(); ++portIdx) {
      if (firrtlModule.getPortName(portIdx) == enablePortName) {
        enablePort = firrtlModule.getArgument(portIdx);
        break;
      }
    }

    if (!enablePort) {
      return callOp.emitError("Enable port not found for interface call: ") << enablePortName;
    }

    Value one = builder.create<ConstantOp>(
        callOp.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 1));
    builder.create<ConnectOp>(callOp.getLoc(), enablePort, one);

    // Connect input arguments
    for (auto [idx, operand] : llvm::enumerate(callOp.getOperands())) {
      StringRef argName = idx < targetArgNames.size()
                              ? cast<StringAttr>(targetArgNames[idx]).getValue()
                              : ("arg" + std::to_string(idx));
      StringAttr argPortName = builder.getStringAttr(
          declName.str() + "_" + methodName.str() + "_" + argName.str());

      Value argPort = nullptr;
      for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts(); ++portIdx) {
        if (firrtlModule.getPortName(portIdx) == argPortName) {
          argPort = firrtlModule.getArgument(portIdx);
          break;
        }
      }

      if (!argPort) {
        return callOp.emitError("Argument port not found for interface call: ") << argPortName;
      }

      Value mappedOperand = ctx.getIRMapping().lookupOrDefault(operand);
      if (!mappedOperand) {
        return callOp.emitError("Call operand was not properly mapped to FIRRTL context");
      }

      builder.create<ConnectOp>(callOp.getLoc(), argPort, mappedOperand);
    }

    // Read output results
    for (size_t idx = 0; idx < callOp.getNumResults(); ++idx) {
      StringRef resName = targetBodyResNames && idx < targetBodyResNames.size()
                              ? cast<StringAttr>(targetBodyResNames[idx]).getValue()
                              : ("result" + std::to_string(idx));
      StringAttr resultPortName = builder.getStringAttr(
          declName.str() + "_" + methodName.str() + "_" + resName.str());

      Value resultPort = nullptr;
      for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts(); ++portIdx) {
        if (firrtlModule.getPortName(portIdx) == resultPortName) {
          resultPort = firrtlModule.getArgument(portIdx);
          break;
        }
      }

      if (!resultPort) {
        return callOp.emitError("Result port not found for interface call: ") << resultPortName;
      }

      mappedResults.push_back(resultPort);
    }

  } else if (targetFunc.getFunctionKind() == FunctionKind::Value) {
    // Read data results directly (values have no enable signal)
    for (size_t idx = 0; idx < callOp.getNumResults(); ++idx) {
      StringRef resName = targetBodyResNames && idx < targetBodyResNames.size()
                              ? cast<StringAttr>(targetBodyResNames[idx]).getValue()
                              : ("result" + std::to_string(idx));
      StringAttr resultPortName = builder.getStringAttr(
          declName.str() + "_" + methodName.str() + "_" + resName.str());

      Value resultPort = nullptr;
      for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts(); ++portIdx) {
        if (firrtlModule.getPortName(portIdx) == resultPortName) {
          resultPort = firrtlModule.getArgument(portIdx);
          break;
        }
      }

      if (!resultPort) {
        return callOp.emitError("Result port not found for interface call: ") << resultPortName;
      }

      mappedResults.push_back(resultPort);
    }
  }

  // Map call results to interface ports
  for (auto [callResult, portValue] : llvm::zip(callOp.getResults(), mappedResults)) {
    ctx.getIRMapping().map(callResult, portValue);
  }

  return success();
}

} // namespace

//===----------------------------------------------------------------------===//
// Pass Creation
//===----------------------------------------------------------------------===//

std::unique_ptr<Pass> circt::createLowerCmt2ToFIRRTLPass() {
  return std::make_unique<LowerCmt2ToFIRRTLPass>();
}

//===- Cmt2ToFIRRTL.cpp - Cmt2 to FIRRTL conversion --------------*- C++
//-*-===//
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
// each Cmt2 module into a FIRRTL module with proper scheduling and control
// logic.
//
// High-level steps:
// 1. Run analyses (Scheduler, ConflictMatrix) to understand module behavior
// 2. For each Cmt2 module:
//    a. Create FIRRTL module with ports for methods/values (enable, ready,
//    args, results) b. Instantiate external FIRRTL modules for cmt2.instance
//    operations c. Process each function (rule/method/value) in scheduled
//    order:
//       - Clone guard region to compute guard result
//       - Generate ready signal = guard ∧ called_readies ∧ ¬(conflicting_fires)
//       - Generate fire signal = ready (for rules/values) or ready ∧ enable
//       (for methods)
//       - Clone body region inside firrtl.when(fire) block
//       - Convert cmt2.call operations to FIRRTL signal connections
//
// SIGNAL GENERATION:
// ==================
// - ready: Indicates a function can execute (guard is true, dependencies ready,
// no conflicts)
// - enable: Input port for methods, driven by callers to request execution
// - fire: Indicates a function is actually executing (ready for rules/values,
// ready∧enable for methods)
//
// CALL CONVERSION:
// ================
// cmt2.call operations are converted to FIRRTL port connections:
// - For method calls: Drive enable=1, connect arguments to input ports, read
// output ports
// - For value calls: Read data output ports directly
//
//===----------------------------------------------------------------------===//

#include "circt/Conversion/Cmt2ToFIRRTL.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
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
#include "llvm/Support/ErrorHandling.h"

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
  void setReady(StringAttr funcName, Value ready) {
    readySignals[funcName] = ready;
  }
  Value getReadyName(StringAttr funcName) const {
    auto it = readySignals.find(funcName);
    return it != readySignals.end() ? it->second : Value();
  }

  void setEnable(StringAttr funcName, Value enable) {
    enableSignals[funcName] = enable;
  }
  Value getEnableName(StringAttr funcName) const {
    auto it = enableSignals.find(funcName);
    return it != enableSignals.end() ? it->second : Value();
  }

  void setFire(StringAttr funcName, Value fire) {
    fireSignals[funcName] = fire;
  }
  Value getFire(StringAttr funcName) const {
    auto it = fireSignals.find(funcName);
    return it != fireSignals.end() ? it->second : Value();
  }

  void setBodyResults(StringAttr funcName, ValueRange results) {
    bodyResults[funcName] = SmallVector<Value>(results.begin(), results.end());
  }
  ArrayRef<Value> getBodyResults(StringAttr funcName) const {
    auto it = bodyResults.find(funcName);
    return it != bodyResults.end() ? ArrayRef<Value>(it->second)
                                   : ArrayRef<Value>();
  }

private:
  DenseMap<StringAttr, Value> readySignals;
  DenseMap<StringAttr, Value> enableSignals;
  DenseMap<StringAttr, Value> fireSignals;
  DenseMap<StringAttr, SmallVector<Value>> bodyResults;
};

/// Holds per-module conversion state including analysis results, signal
/// tracker, and mappings between Cmt2 and FIRRTL constructs.
class ModuleConversionContext {
public:
  ModuleConversionContext(cmt2::ModuleOp cmt2Module, FModuleOp firrtlModule,
                          const ModuleScheduleResult *schedule,
                          const ModuleConflictMatrix *conflictMatrix)
      : cmt2Module(cmt2Module), firrtlModule(firrtlModule), schedule(schedule),
        conflictMatrix(conflictMatrix) {}

  cmt2::ModuleOp getCmt2Module() const { return cmt2Module; }
  FModuleOp getFIRRTLModule() const { return firrtlModule; }
  const ModuleScheduleResult *getSchedule() const { return schedule; }
  const ModuleConflictMatrix *getConflictMatrix() const {
    return conflictMatrix;
  }

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
  LogicalResult
  convertModule(cmt2::ModuleOp module, firrtl::CircuitOp firrtlCircuit,
                const SchedulerAnalysis &scheduler,
                const ConflictMatrixAnalysis &conflictAnalysis,
                const DenseMap<StringAttr, FModuleOp> &convertedModules,
                FModuleOp &outFirrtlModule);

  // Module-level operations
  void createFunctionPorts(cmt2::ModuleOp module, OpBuilder &builder,
                           SmallVectorImpl<PortInfo> &ports);
  LogicalResult
  createInstances(cmt2::ModuleOp module, ModuleConversionContext &ctx,
                  ImplicitLocOpBuilder &builder,
                  const DenseMap<StringAttr, FModuleOp> &convertedModules);
  LogicalResult connectOutputPorts(cmt2::ModuleOp module,
                                   ModuleConversionContext &ctx,
                                   ImplicitLocOpBuilder &builder);

  // Function-level operations
  LogicalResult processFunction(Cmt2FunctionLike func,
                                ModuleConversionContext &ctx,
                                ImplicitLocOpBuilder &builder);
  void mapFunctionArgumentsToPorts(Cmt2FunctionLike func,
                                   ModuleConversionContext &ctx,
                                   OpBuilder &builder);

  // Signal generation
  Value generateReadySignal(Cmt2FunctionLike func, Value guardResult,
                            const ScheduleStep &step,
                            ModuleConversionContext &ctx,
                            ImplicitLocOpBuilder &builder);
  Value generateFireSignal(Cmt2FunctionLike func, Value readySignal,
                           ModuleConversionContext &ctx,
                           ImplicitLocOpBuilder &builder);

  // Region cloning and call conversion
  LogicalResult cloneRegionOps(Region &sourceRegion,
                               ModuleConversionContext &ctx,
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
  firrtl::FExtModuleOp findFIRRTLExtModule(StringRef moduleName, Operation *searchRoot);
  FModuleLike findFIRRTLModuleLike(StringRef moduleName, Operation *searchRoot);
  LogicalResult createExtModules(cmt2::CircuitOp circuit, firrtl::CircuitOp firrtlCircuit);
  std::optional<size_t> getPortIndex(firrtl::InstanceOp &inst,
                                     std::string portName);
  std::optional<size_t> getPortIndex(firrtl::FModuleOp &fMod,
                                     std::string portName);
  cmt2::ModuleOp findTopModule(cmt2::CircuitOp circuit);

  enum PortKind { Argument, Result, Help };

  std::string getFunctionPortName(cmt2::Cmt2FunctionLike function,
                                  PortKind portKind, size_t index);

  std::string getItfcDeclFunctionPortName(cmt2::InterfaceDeclOp decl,
                                          cmt2::Cmt2FunctionLike function,
                                          PortKind portKind, size_t index);

  // Instance creation helpers
  void connectInstanceModuleArguments(
      cmt2::InstanceOp instOp, firrtl::InstanceOp firrtlInst,
      Operation *referencedModule, ModuleConversionContext &ctx,
      ImplicitLocOpBuilder &builder,
      llvm::SmallDenseSet<size_t> &connectedPorts);
  void initializeUnconnectedInputPorts(
      firrtl::InstanceOp firrtlInst, const SmallVector<PortInfo> &ports,
      const llvm::SmallDenseSet<size_t> &connectedPorts,
      ImplicitLocOpBuilder &builder);
  LogicalResult processInterfaceBinding(cmt2::InstanceOp instOp,
                                        firrtl::InstanceOp firrtlInst,
                                        ArrayAttr interfaceBindAttr,
                                        cmt2::ModuleOp module,
                                        cmt2::ModuleOp referencedModule,
                                        ModuleConversionContext &ctx,
                                        ImplicitLocOpBuilder &builder);

  // Interface binding helper
  LogicalResult connectInterfaceBinding(
      firrtl::InstanceOp declInst,  // decl's instance
      StringAttr defEntity,         // either an instance, or an decl
      std::string declFuncName,     // used also as decl's default func name,
                                    // same as the interface's func name
      std::string defFuncName,      // def's func name
      cmt2::InterfaceDeclOp declOp, // decl op
      cmt2::InterfaceOp iface, cmt2::InstanceOp declCmt2Inst,
      ModuleConversionContext &ctx, ImplicitLocOpBuilder &builder);

  // Interface helpers
  bool isInterfaceCall(CallOp callOp, cmt2::ModuleOp module);
  InterfaceOp getInterfaceForDecl(InterfaceDeclOp decl);
  void createInterfacePorts(cmt2::ModuleOp module, OpBuilder &builder,
                            SmallVectorImpl<PortInfo> &ports);
  void initializeInterfaceOutPorts(cmt2::ModuleOp module,
                                   ModuleConversionContext &ctx,
                                   OpBuilder &builder);
  LogicalResult connectInterfaceCall(CallOp callOp,
                                     InterfaceDeclOp interfaceDecl,
                                     ModuleConversionContext &ctx,
                                     ImplicitLocOpBuilder &builder);
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
    llvm::dbgs() << "conversion failed\n";
    signalPassFailure();
  }
}

LogicalResult LowerCmt2ToFIRRTLPass::convertCircuit(cmt2::CircuitOp circuit) {
  OpBuilder builder(circuit);

  // Run analyses on the circuit to gather scheduling and dependency information
  SchedulerAnalysis scheduler(circuit);
  ConflictMatrixAnalysis conflictAnalysis(circuit);

  // Build instance graph to determine conversion order (bottom-up)
  InstanceGraph instanceGraph(circuit);

  // Find the top module (not instantiated by any other module)
  cmt2::ModuleOp cmt2TopModule = findTopModule(circuit);
  if (!cmt2TopModule) {
    return circuit.emitError(
        "No top module found (all modules are instantiated)");
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
    firrtlCircuit =
        builder.create<firrtl::CircuitOp>(circuit.getLoc(), topModuleName);
  } else if (firrtlCircuit.getName() != topModuleName) {
    firrtlCircuit.setNameAttr(topModuleName);
  }

  // Create FIRRTL external modules from CMT2 ExtModuleFirrtlOp
  if (failed(createExtModules(circuit, firrtlCircuit))) {
    return failure();
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
                             convertedModules, firrtlMod))) {
      return failure();
    }
    // Track the converted module
    convertedModules[module.getSymNameAttr()] = firrtlMod;
  }

  // firrtlCircuit.print(llvm::dbgs());

  // Erase the original cmt2 circuit
  circuit.erase();
  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::convertModule(
    cmt2::ModuleOp module, firrtl::CircuitOp firrtlCircuit,
    const SchedulerAnalysis &scheduler,
    const ConflictMatrixAnalysis &conflictAnalysis,
    const DenseMap<StringAttr, FModuleOp> &convertedModules,
    FModuleOp &outFirrtlModule) {

  OpBuilder builder(firrtlCircuit.getBodyBlock(),
                    firrtlCircuit.getBodyBlock()->end());
  ImplicitLocOpBuilder implicitBuilder(module.getLoc(), builder);

  // Retrieve analysis results for this module
  StringAttr moduleName = module.getSymNameAttr();
  const ModuleScheduleResult *schedule =
      scheduler.getModuleSchedule(moduleName);
  const ModuleConflictMatrix *conflictMatrix =
      conflictAnalysis.getModuleMatrix(moduleName);

  if (!schedule) {
    return module.emitError("No schedule found for module");
  }

  // Create FIRRTL module with ports for module arguments and function
  // interfaces
  SmallVector<PortInfo> ports;

  // Add input ports for module arguments
  auto moduleArgNames = module.getArgNames();
  for (auto arg : module.getBodyRegion().front().getArguments()) {
    auto type = cast<FIRRTLBaseType>(arg.getType());
    std::string argName =
        arg.getArgNumber() < moduleArgNames.size()
            ? cast<StringAttr>(moduleArgNames[arg.getArgNumber()])
                  .getValue()
                  .str()
            : ("arg" + std::to_string(arg.getArgNumber()));
    StringAttr portName = builder.getStringAttr(argName);
    ports.push_back(
        PortInfo(portName, type, Direction::In, {}, module.getLoc()));
  }

  // Add ports for methods and values (enable, ready, args, results)
  createFunctionPorts(module, builder, ports);

  // Add ports for interface declarations
  createInterfacePorts(module, builder, ports);

  auto firrtlModule = builder.create<FModuleOp>(
      module.getLoc(), moduleName,
      ConventionAttr::get(builder.getContext(), Convention::Internal), ports);

  implicitBuilder.setInsertionPointToStart(firrtlModule.getBodyBlock());

  // Create conversion context to track state during conversion
  ModuleConversionContext ctx(module, firrtlModule, schedule, conflictMatrix);

  // Map module arguments to FIRRTL ports
  for (auto [cmt2Arg, firrtlArg] :
       llvm::zip(module.getBodyRegion().front().getArguments(),
                 firrtlModule.getBodyBlock()->getArguments().take_front(
                     module.getBodyRegion().front().getNumArguments()))) {
    ctx.getIRMapping().map(cmt2Arg, firrtlArg);
  }

  initializeInterfaceOutPorts(module, ctx, implicitBuilder);

  // Create FIRRTL instances for all cmt2.instance operations
  if (failed(createInstances(module, ctx, implicitBuilder, convertedModules))) {
    llvm::dbgs() << "createInstances failed\n";
    return failure();
  }

  // Process each function in scheduled order
  for (const auto &step : schedule->getSteps()) {
    for (auto funcName : step.getFunctions()) {
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
        llvm::dbgs() << "processFunction failed\n";
        return failure();
      }
    }
  }

  // Connect function outputs to FIRRTL module ports
  if (failed(connectOutputPorts(module, ctx, implicitBuilder))) {
    llvm::dbgs() << "connectOutputPorts failed\n";
    return failure();
  }

  // Return the created FIRRTL module
  outFirrtlModule = firrtlModule;
  return success();
}

//===----------------------------------------------------------------------===//
// Module-Level Operations
//===----------------------------------------------------------------------===//

// method: argument, results, ready, enable
// value: agument, results, ready
void LowerCmt2ToFIRRTLPass::createFunctionPorts(
    cmt2::ModuleOp module, OpBuilder &builder,
    SmallVectorImpl<PortInfo> &ports) {
  // For each function (method/value/rule), create appropriate ports
  for (auto &op : module.getBodyRegion().front()) {
    auto func = dyn_cast<Cmt2FunctionLike>(op);
    if (!func)
      continue;

    auto funcType = cast<FunctionType>(func.getFunctionType());

    // Add enable (in) for method
    if (func.getFunctionKind() == FunctionKind::Method) {
      auto enableName = getFunctionPortName(func, PortKind::Help, 1);
      ports.push_back(PortInfo(builder.getStringAttr(enableName),
                               UIntType::get(builder.getContext(), 1),
                               Direction::In, {}, func.getLoc()));
    }

    // Add ready (out) for method/value
    if (func.getFunctionKind() == FunctionKind::Method ||
        func.getFunctionKind() == FunctionKind::Value) {
      auto readyName = getFunctionPortName(func, PortKind::Help, 0);
      ports.push_back(PortInfo(builder.getStringAttr(readyName),
                               UIntType::get(builder.getContext(), 1),
                               Direction::Out, {}, func.getLoc()));
    }

    // Add argument ports with meaningful names
    for (auto [idx, argType] : llvm::enumerate(funcType.getInputs())) {
      std::string argName = getFunctionPortName(func, PortKind::Argument, idx);
      ports.push_back(PortInfo(builder.getStringAttr(argName),
                               cast<FIRRTLBaseType>(argType), Direction::In, {},
                               func.getLoc()));
    }

    // Add result ports with meaningful names
    for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
      std::string resName = getFunctionPortName(func, PortKind::Result, idx);
      ports.push_back(PortInfo(builder.getStringAttr(resName),
                               cast<FIRRTLBaseType>(resType), Direction::Out,
                               {}, func.getLoc()));
    }
  }

  // Add debug firing ports if requested (via debug.firing_ports attribute)
  if (auto firingPortsAttr = module->getAttrOfType<ArrayAttr>("debug.firing_ports")) {
    for (auto attr : firingPortsAttr) {
      auto portName = cast<StringAttr>(attr).getValue();
      ports.push_back(PortInfo(builder.getStringAttr(portName),
                               UIntType::get(builder.getContext(), 1),
                               Direction::Out, {}, module.getLoc()));
    }
  }
}

LogicalResult LowerCmt2ToFIRRTLPass::createInstances(
    cmt2::ModuleOp module, ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder,
    const DenseMap<StringAttr, FModuleOp> &convertedModules) {

  // For each cmt2.instance, create a corresponding firrtl.instance
  for (auto &op : module.getBodyRegion().front()) {
    auto instOp = dyn_cast<cmt2::InstanceOp>(op);
    if (!instOp)
      continue;

    LLVM_DEBUG(llvm::dbgs()
               << "create instance " << instOp.getSymName() << "\n");

    auto referencedModule = instOp.getReferencedModule();
    if (!referencedModule)
      return instOp.emitError("Referenced module not found: ")
             << instOp.getModuleName();

    // Determine target FIRRTL module (external or converted cmt2 module)
    FModuleLike firrtlMod;
    StringRef targetModuleName;
    if (auto extModOp =
            dyn_cast<ExtModuleFirrtlOp>(referencedModule.getOperation())) {
      targetModuleName = extModOp.getExtModuleName();
      firrtlMod = findFIRRTLModuleLike(targetModuleName,
                                       module->getParentOfType<mlir::ModuleOp>());
      if (!firrtlMod)
        return instOp.emitError("FIRRTL module not found: ")
               << targetModuleName;
    } else if (auto cmt2Mod =
                   dyn_cast<cmt2::ModuleOp>(referencedModule.getOperation())) {
      targetModuleName = cmt2Mod.getSymName();
      auto it = convertedModules.find(cmt2Mod.getSymNameAttr());
      if (it == convertedModules.end())
        return instOp.emitError("Cmt2 module not yet converted: ")
               << targetModuleName;
      firrtlMod = it->second;
    } else {
      return instOp.emitError("Unsupported module type for instantiation");
    }

    // Create the FIRRTL instance
    SmallVector<PortInfo> ports = firrtlMod.getPorts();
    auto firrtlInst = builder.create<firrtl::InstanceOp>(
        instOp.getLoc(), ports, targetModuleName, instOp.getSymName(),
        NameKindEnum::DroppableName);
    ctx.registerInstance(instOp.getSymNameAttr(), firrtlInst);

    // Track connected ports
    llvm::SmallDenseSet<size_t> connectedPorts;

    // Connect module arguments to FIRRTL ports
    connectInstanceModuleArguments(instOp, firrtlInst,
                                   referencedModule.getOperation(), ctx,
                                   builder, connectedPorts);

    // Initialize unconnected input ports with default values
    initializeUnconnectedInputPorts(firrtlInst, ports, connectedPorts, builder);

    LLVM_DEBUG(llvm::dbgs() << "initializeUnconnectedInputPorts done\n");
    // Process interface bindings (referenceMOdule must be ModuleOp)
    if (auto interfaceBinds = instOp.getInterfaceBinds()) {
      // llvm::dbgs() << "process " << (*interfaceBinds).size()
      //              << " interface bindings\n";
      if (auto referenceMod =
              dyn_cast<cmt2::ModuleOp>(referencedModule.getOperation())) {
        for (auto bindAttr : *interfaceBinds) {
          if (failed(processInterfaceBinding(instOp, firrtlInst,
                                             cast<ArrayAttr>(bindAttr), module,
                                             referenceMod, ctx, builder))) {
            llvm::dbgs() << "processInterfaceBinding failed\n";
            return failure();
          }
        }

      } else if ((*interfaceBinds).size() > 0) {
        llvm::dbgs() << "reach here?\n";
        return instOp.emitError(
            "instantiate extern module with interface, illegal!");
      }
    }

    LLVM_DEBUG(llvm::dbgs()
               << "create instance " << instOp.getSymName() << " succeed \n");
  }

  return success();
}

// Helper: Connect module arguments (clock, reset, etc.) to FIRRTL ports
void LowerCmt2ToFIRRTLPass::connectInstanceModuleArguments(
    cmt2::InstanceOp instOp, firrtl::InstanceOp firrtlInst,
    Operation *referencedModule, ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder,
    llvm::SmallDenseSet<size_t> &connectedPorts) {

  auto instanceArgs = instOp.getArgs();
  if (auto extModOp = dyn_cast<ExtModuleFirrtlOp>(referencedModule)) {
    // External FIRRTL module - use bind.bare operations to get port names
    size_t barePortIdx = 0;
    for (auto &bodyOp : extModOp.getBodyRegion().front()) {
      auto bindBare = dyn_cast<BindBareOp>(bodyOp);
      if (!bindBare || barePortIdx >= instanceArgs.size())
        continue;
      Value firrtlArg =
          ctx.getIRMapping().lookupOrDefault(instanceArgs[barePortIdx]);
      if (auto portIdx = getPortIndex(
              firrtlInst, bindBare.getPortAttr().getAttr().getValue().str())) {
        builder.create<ConnectOp>(instOp.getLoc(),
                                  firrtlInst.getResult(*portIdx), firrtlArg);
        connectedPorts.insert(*portIdx);
      }
      barePortIdx++;
    }
  } else if (auto cmt2Mod = dyn_cast<cmt2::ModuleOp>(referencedModule)) {
    // Regular cmt2 module - connect arguments directly (first N ports)
    size_t numModuleArgs = cmt2Mod.getBodyRegion().front().getNumArguments();
    for (size_t i = 0; i < instanceArgs.size() && i < numModuleArgs; ++i) {
      if (Value firrtlArg =
              ctx.getIRMapping().lookupOrDefault(instanceArgs[i])) {
        builder.create<ConnectOp>(instOp.getLoc(), firrtlInst.getResult(i),
                                  firrtlArg);
        connectedPorts.insert(i);
      }
    }
  }
}

// Helper: Initialize all unconnected input ports with default values
void LowerCmt2ToFIRRTLPass::initializeUnconnectedInputPorts(
    firrtl::InstanceOp firrtlInst, const SmallVector<PortInfo> &ports,
    const llvm::SmallDenseSet<size_t> &connectedPorts,
    ImplicitLocOpBuilder &builder) {

  for (size_t i = 0; i < ports.size(); ++i) {
    if (connectedPorts.contains(i) || ports[i].direction != Direction::In)
      continue;

    Value instPort = firrtlInst.getResult(i);
    Type portType = instPort.getType();
    auto ftype = dyn_cast<UIntType>(portType);
    Value init;
    if (ftype && (ftype.getWidth().value_or(1) == 1)) {
      init = builder.create<ConstantOp>(firrtlInst.getLoc(), ftype,
                                        APInt(1, 0));
    } else {
      init = builder.create<InvalidValueOp>(firrtlInst.getLoc(), portType);
    }
    builder.create<ConnectOp>(firrtlInst.getLoc(), instPort, init);
  }
}

// Helper: Process a single interface binding
LogicalResult LowerCmt2ToFIRRTLPass::processInterfaceBinding(
    cmt2::InstanceOp instOp, firrtl::InstanceOp firrtlInst,
    ArrayAttr interfaceBindAttr, cmt2::ModuleOp module,
    cmt2::ModuleOp referencedModule, ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder) {

  if (interfaceBindAttr.size() < 2)
    return instOp.emitError("wrong number of interface binding attributes");

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

  InterfaceDeclOp interfaceDecl;
  for (auto &op : referencedModule.getBodyRegion().front()) {
    if (auto decl = dyn_cast<InterfaceDeclOp>(op)) {
      if (decl.getSymNameAttr() == declRef.getLeafReference()) {
        interfaceDecl = decl;
        break;
      }
    }
  }

  // Find InterfaceOp to get method signatures
  InterfaceOp iface = getInterfaceForDecl(interfaceDecl);
  if (!iface)
    return instOp.emitError("InterfaceOp not found: ")
           << interfaceDef.getInterface();

  if (interfaceDef) {

    llvm::dbgs() << "reach A\n";
    // Process each method/value mapping in the interface definition
    for (auto methodEntry : interfaceDef.getMethods()) {
      auto methodArray = cast<mlir::ArrayAttr>(methodEntry);
      if (methodArray.size() < 3)
        continue;

      // Parse: [@def, @defFunc, @declFun]
      auto defRef = cast<mlir::SymbolRefAttr>(methodArray[0]);
      auto defFuncRef = cast<mlir::SymbolRefAttr>(methodArray[1]);
      auto declFuncRef = cast<mlir::SymbolRefAttr>(methodArray[2]);

      if (failed(connectInterfaceBinding(
              firrtlInst, defRef.getLeafReference(),
              declFuncRef.getLeafReference().getValue().str(),
              defFuncRef.getLeafReference().getValue().str(), interfaceDecl,
              iface, instOp, ctx, builder))) {
        llvm::dbgs() << "reach B\n";

        return failure();
      }
    }

  } else {
    // There must be an interface-decl with the name defRef
    InterfaceDeclOp defDecl;
    for (auto &op : module.getBodyRegion().front()) {
      if (auto itfcDecl = dyn_cast<InterfaceDeclOp>(op)) {
        if (itfcDecl.getSymNameAttr() == defRef.getLeafReference()) {
          defDecl = itfcDecl;
        }
      }
    }

    if (!defDecl) {
      return instOp.emitError("InterfaceDef/Decl not found: ") << defRef;
    }

    // Process each method/value from the defDecl (same signature as interface)
    for (auto &op : iface.getBodyRegion().front()) {
      if (auto func = dyn_cast<Cmt2FunctionLike>(op)) {
        if (failed(connectInterfaceBinding(
                firrtlInst, defRef.getLeafReference(),
                func.functionName().str(), func.functionName().str(),
                interfaceDecl, iface, instOp, ctx, builder))) {
          llvm::dbgs() << "reach here C\n";
          return failure();
        }
      }
    }
  }

  return success();
}

LogicalResult
LowerCmt2ToFIRRTLPass::connectOutputPorts(cmt2::ModuleOp module,
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
      if (Value readySignal =
              ctx.getSignalTracker().getReadyName(func.functionNameAttr())) {
        builder.create<ConnectOp>(func.getLoc(), readyPort, readySignal);
      }

      // Skip arg input ports
      auto funcType = cast<FunctionType>(func.getFunctionType());
      portIndex += funcType.getInputs().size();

      // Connect result outputs
      ArrayRef<Value> bodyResults =
          ctx.getSignalTracker().getBodyResults(func.functionNameAttr());
      for (auto [idx, _] : llvm::enumerate(funcType.getResults())) {
        Value resultPort =
            firrtlModule.getBodyBlock()->getArgument(portIndex++);
        if (idx < bodyResults.size() && bodyResults[idx]) {
          builder.create<ConnectOp>(func.getLoc(), resultPort,
                                    bodyResults[idx]);
        }
      }

    } else if (func.getFunctionKind() == FunctionKind::Value) {
      // Connect ready output
      Value readyPort = firrtlModule.getBodyBlock()->getArgument(portIndex++);
      if (Value readySignal =
              ctx.getSignalTracker().getReadyName(func.functionNameAttr())) {
        builder.create<ConnectOp>(func.getLoc(), readyPort, readySignal);
      }

      // Skip arg input ports
      auto funcType = cast<FunctionType>(func.getFunctionType());
      portIndex += funcType.getInputs().size();

      // Connect result outputs
      ArrayRef<Value> bodyResults =
          ctx.getSignalTracker().getBodyResults(func.functionNameAttr());
      for (auto [idx, _] : llvm::enumerate(funcType.getResults())) {
        Value resultPort =
            firrtlModule.getBodyBlock()->getArgument(portIndex++);
        if (idx < bodyResults.size() && bodyResults[idx]) {
          builder.create<ConnectOp>(func.getLoc(), resultPort,
                                    bodyResults[idx]);
        }
      }
    } else {
      // Rules: wire arg input ports and result output ports
      // Rules with results need their computed values wired to output ports
      auto funcType = cast<FunctionType>(func.getFunctionType());
      portIndex += funcType.getInputs().size();   // Skip arg ports

      // Connect result outputs (for rules that return values, e.g., from
      // dataflow pipelines)
      ArrayRef<Value> bodyResults =
          ctx.getSignalTracker().getBodyResults(func.functionNameAttr());
      for (auto [idx, _] : llvm::enumerate(funcType.getResults())) {
        Value resultPort =
            firrtlModule.getBodyBlock()->getArgument(portIndex++);
        if (idx < bodyResults.size() && bodyResults[idx]) {
          builder.create<ConnectOp>(func.getLoc(), resultPort,
                                    bodyResults[idx]);
        }
      }
    }
  }

  // Connect debug firing ports
  if (auto firingPortsAttr = module->getAttrOfType<ArrayAttr>("debug.firing_ports")) {
    for (auto attr : firingPortsAttr) {
      auto firingPortName = cast<StringAttr>(attr).getValue();
      Value firingPort = firrtlModule.getBodyBlock()->getArgument(portIndex++);

      // Find the rule with this debug.firing_port attribute
      Value fireSignal;
      for (auto &op : module.getBodyRegion().front()) {
        if (auto rule = dyn_cast<RuleOp>(op)) {
          if (auto debugAttr = rule->getAttrOfType<StringAttr>("debug.firing_port")) {
            if (debugAttr.getValue() == firingPortName) {
              auto func = cast<Cmt2FunctionLike>(rule.getOperation());
              fireSignal = ctx.getSignalTracker().getFire(func.functionNameAttr());
              break;
            }
          }
        }
      }

      if (fireSignal) {
        builder.create<ConnectOp>(module.getLoc(), firingPort, fireSignal);
      } else {
        // No fire signal found - connect to constant 0
        Value zero = builder.create<ConstantOp>(
            module.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 0));
        builder.create<ConnectOp>(module.getLoc(), firingPort, zero);
      }
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Function-Level Operations
//===----------------------------------------------------------------------===//

LogicalResult
LowerCmt2ToFIRRTLPass::processFunction(Cmt2FunctionLike func,
                                       ModuleConversionContext &ctx,
                                       ImplicitLocOpBuilder &builder) {

  // Map function parameters to FIRRTL module ports
  if (!func.isExternal()) {
    mapFunctionArgumentsToPorts(func, ctx, builder);
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
  Value guardResult =
      guardResults.empty()
          ? builder.create<ConstantOp>(func.getLoc(),
                                       UIntType::get(builder.getContext(), 1),
                                       APInt(1, 1))
          : guardResults[0];

  // Find the schedule step containing this function
  const ScheduleStep *containingStep = nullptr;
  for (const auto &step : ctx.getSchedule()->getSteps()) {
    if (llvm::is_contained(step.getFunctions(), func.functionNameAttr())) {
      containingStep = &step;
      break;
    }
  }

  if (!containingStep) {
    return func.emitError("Function not found in any schedule step");
  }

  // Generate control signals
  Value readySignal =
      generateReadySignal(func, guardResult, *containingStep, ctx, builder);
  ctx.getSignalTracker().setReady(func.functionNameAttr(), readySignal);

  Value fireSignal = generateFireSignal(func, readySignal, ctx, builder);
  ctx.getSignalTracker().setFire(func.functionNameAttr(), fireSignal);

  // Clone body region inside firrtl.when(fire) block
  if (!func.isExternal() && func->getNumRegions() > 1) {
    Region &bodyRegion = func->getRegion(1);

    // Create wires for result values (to escape the when block's region)
    // Methods, Values, and Rules with results need output port wiring
    auto funcType = cast<FunctionType>(func.getFunctionType());
    SmallVector<Value> resultWires;
    bool needsResultWires = (func.getFunctionKind() == FunctionKind::Method ||
                             func.getFunctionKind() == FunctionKind::Value ||
                             func.getFunctionKind() == FunctionKind::Rule) &&
                            !funcType.getResults().empty();

    if (needsResultWires) {
      for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
        auto firrtlType = cast<FIRRTLBaseType>(resType);

        std::string resultName =
            getFunctionPortName(func, PortKind::Result, idx);
        auto wire = builder.create<WireOp>(func.getLoc(), firrtlType,
                                           builder.getStringAttr(resultName));

        // Initialize wire with invalidvalue to satisfy FIRRTL's full
        // initialization requirement
        Value invalid =
            builder.create<InvalidValueOp>(func.getLoc(), firrtlType);
        builder.create<ConnectOp>(func.getLoc(), wire.getResult(), invalid);

        resultWires.push_back(wire.getResult());
      }
    }

    auto whenOp = builder.create<WhenOp>(func.getLoc(), fireSignal,
                                         /*withElseRegion=*/false);

    OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(&whenOp.getThenBlock());

    SmallVector<Value> bodyResults;
    if (failed(cloneRegionOps(bodyRegion, ctx, builder, bodyResults))) {
      return failure();
    }

    // Connect body results to wires inside the when block (only if there are
    // results)
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

void LowerCmt2ToFIRRTLPass::mapFunctionArgumentsToPorts(
    Cmt2FunctionLike func, ModuleConversionContext &ctx, OpBuilder &builder) {
  FModuleOp firrtlModule = ctx.getFIRRTLModule();

  // Map block arguments from all regions (guard and body) to FIRRTL ports
  for (Region &region : func->getRegions()) {
    if (region.empty())
      continue;

    for (auto [idx, blockArg] :
         llvm::enumerate(region.front().getArguments())) {
      // Use the same naming convention as createFunctionPorts
      std::string argPortName =
          getFunctionPortName(func, PortKind::Argument, idx);

      // Find the corresponding port in the FIRRTL module
      for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts();
           ++portIdx) {
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
    const ScheduleStep &step, ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder) {

  // ready = guard ∧ called_readies ∧ ¬(conflicting_fires)
  Value ready = guardResult;

  FModuleOp firrtlModule = ctx.getFIRRTLModule();

  // AND with ready signals of called functions
  func->walk([&](cmt2::CallOp call) {
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

      // get the callee function
      auto iface = getInterfaceForDecl(interfaceDecl);
      auto func =
          iface.getFunctionOfName(call.getMethodOrValue().getLeafReference());
      std::string readyPortName =
          getItfcDeclFunctionPortName(interfaceDecl, func, PortKind::Help, 0);

      if (auto readyPortIdx = getPortIndex(firrtlModule, readyPortName)) {
        ready = builder.create<AndPrimOp>(
            func.getLoc(), ready, firrtlModule.getArgument(*readyPortIdx));
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

      if (auto extModOp =
              dyn_cast<ExtModuleFirrtlOp>(referencedModule.getOperation())) {
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

        std::optional<std::string> readyName;
        if (auto bindMethod = dyn_cast<BindMethodOp>(bindFunc.getOperation())) {
          readyName = bindMethod.getReadyName();
        } else if (auto bindValue =
                       dyn_cast<BindValueOp>(bindFunc.getOperation())) {
          readyName = bindValue.getReadyName();
        }

        if (readyName) {
          auto readyPortName = builder.getStringAttr(readyName.value());
          if (auto readyPortIdx =
                  getPortIndex(firrtlInst, readyPortName.getValue().str())) {
            LLVM_DEBUG(llvm::dbgs() << "ready port found from external: "
                                    << readyPortName << "\n");
            ready = builder.create<AndPrimOp>(
                call.getLoc(), ready, firrtlInst.getResult(*readyPortIdx));
          }
        }
      } else if (auto cmt2Mod = dyn_cast<cmt2::ModuleOp>(
                     referencedModule.getOperation())) {
        Cmt2FunctionLike targetFunc;
        for (auto &op : cmt2Mod.getBodyRegion().front()) {
          if (auto func = dyn_cast<Cmt2FunctionLike>(op)) {
            if (func.functionNameAttr() == methodName) {
              targetFunc = func;
              break;
            }
          }
        }

        std::string readyPortName =
            getFunctionPortName(targetFunc, PortKind::Help, 0);
        if (auto readyPortIdx = getPortIndex(firrtlInst, readyPortName)) {
          LLVM_DEBUG(llvm::dbgs()
                     << "ready port found for " << referencedModule.moduleName()
                     << " : " << readyPortName << "\n");
          ready = builder.create<AndPrimOp>(
              call.getLoc(), ready, firrtlInst.getResult(*readyPortIdx));
        }
      }
    }
  });

  // AND with NOT(preceding conflicting functions fired)
  // Only ConflictMatrix relationships prevent concurrent firing
  // const auto &funcs = step.getFunctions();
  const auto &preventing = step.getPreventingFirings();
  auto funcName = func.functionNameAttr();

  for (auto prevent : preventing) {
    if (prevent.later == funcName) {
      if (auto precedingFire =
              ctx.getSignalTracker().getFire(prevent.earlier)) {
        Value notFired = builder.create<XorPrimOp>(
            func.getLoc(), precedingFire,
            builder.create<ConstantOp>(func.getLoc(),
                                       UIntType::get(builder.getContext(), 1),
                                       APInt(1, 1)));
        ready = builder.create<AndPrimOp>(func.getLoc(), ready, notFired);
      }
    }
  }
  return ready;
}

Value LowerCmt2ToFIRRTLPass::generateFireSignal(Cmt2FunctionLike func,
                                                Value readySignal,
                                                ModuleConversionContext &ctx,
                                                ImplicitLocOpBuilder &builder) {

  // For methods: fire = ready ∧ enable
  // For rules/values: fire = ready
  if (func.getFunctionKind() == FunctionKind::Method) {
    Value enableSignal =
        ctx.getSignalTracker().getEnableName(func.functionNameAttr());

    if (!enableSignal) {
      // Find enable port in FIRRTL module
      FModuleOp firrtlModule = ctx.getFIRRTLModule();
      StringAttr enablePortName =
          builder.getStringAttr(func.functionName().str() + "_enable");

      for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts();
           ++portIdx) {
        if (firrtlModule.getPortName(portIdx) == enablePortName) {
          enableSignal = firrtlModule.getArgument(portIdx);
          ctx.getSignalTracker().setEnable(func.functionNameAttr(),
                                           enableSignal);
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
    Region &sourceRegion, ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder, SmallVectorImpl<Value> &results) {

  if (sourceRegion.empty())
    return success();

  Block &sourceBlock = sourceRegion.front();

  // Clone each operation, handling cmt2.return, cmt2.call, and cmt2.if
  // specially
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
        auto wire = builder.create<WireOp>(ifOp.getLoc(), resultType,
                                           builder.getStringAttr("if_result"));
        resultWires.push_back(wire.getResult());
      }

      auto whenOp = builder.create<WhenOp>(ifOp.getLoc(), condition, hasElse);

      // Clone then region
      {
        OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(&whenOp.getThenBlock());

        SmallVector<Value> thenResults;
        if (failed(cloneRegionOps(ifOp.getThenRegion(), ctx, builder,
                                  thenResults))) {
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
        if (failed(cloneRegionOps(ifOp.getElseRegion(), ctx, builder,
                                  elseResults))) {
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
      for (auto [oldResult, newResult] :
           llvm::zip(ifOp.getResults(), resultWires)) {
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

    // NOTE: Token operations (token.create, token.valid, token.data, token.join)
    // should be eliminated by the cmt2-token-materialize pass BEFORE this pass runs.
    // If token ops reach here, it means the pass pipeline is misconfigured.
    // See docs/Cmt2/features/Lowering.md (dataflow/proc lowering overview).
    if (isa<TokenCreateOp, TokenValidOp, TokenDataOp, TokenJoinOp>(op)) {
      return op.emitError("Token operation reached cmt2-to-firrtl. "
                          "Run cmt2-token-materialize pass first to convert "
                          "token ops to storage instances and calls.");
    }

    // Clone other operations normally
    Operation *cloned = builder.clone(op, ctx.getIRMapping());
    for (auto [oldResult, newResult] :
         llvm::zip(op.getResults(), cloned->getResults())) {
      ctx.getIRMapping().map(oldResult, newResult);
    }
  }

  return success();
}

LogicalResult
LowerCmt2ToFIRRTLPass::convertCallOp(CallOp callOp,
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
      return callOp.emitError("Interface declaration not found: ")
             << instanceName;
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
    return callOp.emitError("Referenced module not found for instance: ")
           << instanceName;
  }

  // Check if it's an external FIRRTL module or a regular cmt2 module
  if (auto extModOp =
          dyn_cast<ExtModuleFirrtlOp>(referencedModule.getOperation())) {
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
    } else if (auto bindValue =
                   dyn_cast<BindValueOp>(bindFunc.getOperation())) {
      return connectValueCall(callOp, bindValue, firrtlInst, ctx, builder);
    }
  } else if (auto cmt2Mod =
                 dyn_cast<cmt2::ModuleOp>(referencedModule.getOperation())) {
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

    SmallVector<Value> mappedResults;

    if (targetFunc.getFunctionKind() == FunctionKind::Method) {
      // Drive enable signal
      std::string enablePortName =
          getFunctionPortName(targetFunc, PortKind::Help, 1);
      if (auto enablePortIdx = getPortIndex(firrtlInst, enablePortName)) {
        Value one = builder.create<ConstantOp>(
            callOp.getLoc(), UIntType::get(builder.getContext(), 1),
            APInt(1, 1));
        builder.create<ConnectOp>(callOp.getLoc(),
                                  firrtlInst.getResult(*enablePortIdx), one);
      } else {
        return callOp.emitError("Enable port not found: ") << enablePortName;
      }
    }

    // Connect input arguments using proper names
    for (auto [idx, operand] : llvm::enumerate(callOp.getOperands())) {
      std::string argName =
          getFunctionPortName(targetFunc, PortKind::Argument, idx);
      if (auto portIdx = getPortIndex(firrtlInst, argName)) {
        Value mappedOperand = ctx.getIRMapping().lookupOrDefault(operand);
        if (!mappedOperand) {
          return callOp.emitError(
              "Call operand was not properly mapped to FIRRTL context");
        }
        builder.create<ConnectOp>(
            callOp.getLoc(), firrtlInst.getResult(*portIdx), mappedOperand);
      } else {
        return callOp.emitError("Argument port not found: ") << argName;
      }
    }

    // Read output results using proper names
    for (size_t idx = 0; idx < callOp.getNumResults(); ++idx) {
      std::string resName =
          getFunctionPortName(targetFunc, PortKind::Result, idx);
      if (auto portIdx = getPortIndex(firrtlInst, resName)) {
        mappedResults.push_back(firrtlInst.getResult(*portIdx));
      } else {
        return callOp.emitError("Result port not found: ") << resName;
      }
    }

    // Map call results to instance ports
    for (auto [callResult, portValue] :
         llvm::zip(callOp.getResults(), mappedResults)) {
      ctx.getIRMapping().map(callResult, portValue);
    }

  } else {
    return callOp.emitError("Unsupported module type for call");
  }

  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::connectMethodCall(
    CallOp callOp, BindMethodOp bindMethod, firrtl::InstanceOp firrtlInst,
    ModuleConversionContext &ctx, ImplicitLocOpBuilder &builder) {

  // Drive enable signal to 1
  if (auto enableAttr = bindMethod.getEnableName()) {
    StringAttr enablePortName = builder.getStringAttr(enableAttr.value());
    if (auto enablePortIdx =
            getPortIndex(firrtlInst, enablePortName.getValue().str())) {
      bool isGetRes = false;
      if (auto callTy = callOp->getAttrOfType<StringAttr>("call_ty"))
        isGetRes = callTy.getValue() == "GetRes";

      Value en = builder.create<ConstantOp>(
          callOp.getLoc(), UIntType::get(builder.getContext(), 1),
          APInt(1, isGetRes ? 0 : 1));
      builder.create<ConnectOp>(callOp.getLoc(),
                                firrtlInst.getResult(*enablePortIdx), en);
    } else {
      return callOp.emitError("Enable port not found: ") << enablePortName;
    }
  }

  // Connect input arguments
  auto inputsAttr = bindMethod.getArgNames();
  if (inputsAttr.size() != callOp.getNumOperands()) {
    return callOp.emitError("Operand count mismatch: expected ")
           << inputsAttr.size() << " but got " << callOp.getNumOperands();
  }

  for (auto [operand, inputAttr] :
       llvm::zip(callOp.getOperands(), inputsAttr)) {
    auto portAttr = cast<StringAttr>(inputAttr);
    auto portIdx = getPortIndex(firrtlInst, portAttr.getValue().str());
    if (!portIdx) {
      return callOp.emitError("Input port not found: ") << portAttr;
    }

    Value mappedOperand = ctx.getIRMapping().lookupOrDefault(operand);
    if (!mappedOperand) {
      return callOp.emitError(
          "Call operand was not properly mapped to FIRRTL context");
    }

    builder.create<ConnectOp>(callOp.getLoc(), firrtlInst.getResult(*portIdx),
                              mappedOperand);
  }

  // Read output results
  auto outputsAttr = bindMethod.getBodyResNames();
  if (outputsAttr.size() != callOp.getNumResults()) {
    return callOp.emitError("Result count mismatch: expected ")
           << outputsAttr.size() << " but got " << callOp.getNumResults();
  }

  SmallVector<Value> mappedResults;
  for (auto outputAttr : outputsAttr) {
    auto portAttr = cast<StringAttr>(outputAttr);
    auto portIdx = getPortIndex(firrtlInst, portAttr.getValue().str());
    if (!portIdx) {
      return callOp.emitError("Output port not found: ") << portAttr;
    }
    mappedResults.push_back(firrtlInst.getResult(*portIdx));
  }

  // Map call results to instance ports
  for (auto [callResult, portValue] :
       llvm::zip(callOp.getResults(), mappedResults)) {
    ctx.getIRMapping().map(callResult, portValue);
  }

  return success();
}

LogicalResult LowerCmt2ToFIRRTLPass::connectValueCall(
    CallOp callOp, BindValueOp bindValue, firrtl::InstanceOp firrtlInst,
    ModuleConversionContext &ctx, ImplicitLocOpBuilder &builder) {

  // Connect input arguments
  auto inputsAttr = bindValue.getArgNames();
  if (inputsAttr.size() != callOp.getNumOperands()) {
    return callOp.emitError("Operand count mismatch: expected ")
           << inputsAttr.size() << " but got " << callOp.getNumOperands();
  }

  for (auto [operand, inputAttr] :
       llvm::zip(callOp.getOperands(), inputsAttr)) {
    auto portAttr = cast<StringAttr>(inputAttr);
    auto portIdx = getPortIndex(firrtlInst, portAttr.getValue().str());
    if (!portIdx) {
      return callOp.emitError("Input port not found: ") << portAttr;
    }

    Value mappedOperand = ctx.getIRMapping().lookupOrDefault(operand);
    if (!mappedOperand) {
      return callOp.emitError(
          "Call operand was not properly mapped to FIRRTL context");
    }

    builder.create<ConnectOp>(callOp.getLoc(), firrtlInst.getResult(*portIdx),
                              mappedOperand);
  }

  // Read data results
  auto dataAttr = bindValue.getBodyResNames();
  if (dataAttr.size() != callOp.getNumResults()) {
    return callOp.emitError("Result count mismatch: expected ")
           << dataAttr.size() << " but got " << callOp.getNumResults();
  }

  SmallVector<Value> mappedResults;
  for (auto dataPortAttr : dataAttr) {
    auto portAttr = cast<StringAttr>(dataPortAttr);
    auto portIdx = getPortIndex(firrtlInst, portAttr.getValue().str());
    if (!portIdx) {
      return callOp.emitError("Data port not found: ") << portAttr;
    }
    mappedResults.push_back(firrtlInst.getResult(*portIdx));
  }

  // Map call results to instance ports
  for (auto [callResult, portValue] :
       llvm::zip(callOp.getResults(), mappedResults)) {
    ctx.getIRMapping().map(callResult, portValue);
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Helper Utilities
//===----------------------------------------------------------------------===//

// if PortKind == Help, index== 0 (ready), 1 (enable)
std::string
LowerCmt2ToFIRRTLPass::getFunctionPortName(cmt2::Cmt2FunctionLike function,
                                           PortKind portKind, size_t index) {
  std::string prefix;

  if (!function) {
    llvm::report_fatal_error("Function is nullptr in getFunctionPortName!\n");
  }

  if (auto p = ::llvm::dyn_cast_or_null<::mlir::StringAttr>(
          function.getOperation()->getAttr("prefix"))) {
    prefix = p.getValue().str();
  } else if (function.isExternal()) {
    prefix = "";
  } else {
    prefix = function.functionName().str() + "_";
  }

  std::string base;

  if (portKind == PortKind::Argument) {
    if (index < function.getNumArguments())
      base = function.getArgumentName(index);
    else
      base = "ArgumentOutOfBound";
  } else if (portKind == PortKind::Result) {
    if (index < function.getNumResults())
      base = function.getResultName(index);
    else
      base = "ResultOutOfBound";
  } else {
    if (index == 0) {
      // ready
      if (auto ready = function.getReadyNameAttr()) {
        base = ready.getValue().str();
      } else {
        base = "ready";
      }
    } else {
      // enable
      if (auto enable = function.getEnableNameAttr()) {
        base = enable.getValue().str();
      } else {
        base = "enable";
      }
    }
  }
  return prefix + base;
}

std::string LowerCmt2ToFIRRTLPass::getItfcDeclFunctionPortName(
    cmt2::InterfaceDeclOp decl, cmt2::Cmt2FunctionLike function,
    PortKind portKind, size_t index) {

  std::string portName = getFunctionPortName(function, portKind, index);
  std::string declName;
  if (auto prefix = decl.getOperation()->getAttrOfType<StringAttr>("prefix")) {
    declName = prefix.getValue().str();
  } else {
    declName = decl.getSymName().str() + "_";
  }

  return declName + portName;
}

FModuleOp LowerCmt2ToFIRRTLPass::findFIRRTLModule(StringRef moduleName,
                                                  Operation *searchRoot) {
  FModuleOp result;

  // Search for FIRRTL module in circuits
  searchRoot->walk([&](firrtl::CircuitOp circuit) {
    // First search for regular modules (FModuleOp)
    circuit.walk([&](FModuleOp mod) {
      if (mod.getModuleName() == moduleName) {
        result = mod;
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
    if (result)
      return WalkResult::interrupt();

    // Also search for external modules (FExtModuleOp)
    circuit.walk([&](firrtl::FExtModuleOp extMod) {
      if (extMod.getModuleName() == moduleName) {
        // For external modules, we create a stub FModuleOp to satisfy
        // the interface requirements. The caller will handle this case.
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

firrtl::FExtModuleOp LowerCmt2ToFIRRTLPass::findFIRRTLExtModule(
    StringRef moduleName, Operation *searchRoot) {
  firrtl::FExtModuleOp result;

  // Search for FIRRTL external module in circuits
  searchRoot->walk([&](firrtl::CircuitOp circuit) {
    circuit.walk([&](firrtl::FExtModuleOp extMod) {
      if (extMod.getModuleName() == moduleName) {
        result = extMod;
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
    return result ? WalkResult::interrupt() : WalkResult::advance();
  });

  return result;
}

FModuleLike LowerCmt2ToFIRRTLPass::findFIRRTLModuleLike(
    StringRef moduleName, Operation *searchRoot) {
  // First try to find a regular module
  if (auto mod = findFIRRTLModule(moduleName, searchRoot))
    return mod;
  // Then try external module
  return findFIRRTLExtModule(moduleName, searchRoot);
}

LogicalResult LowerCmt2ToFIRRTLPass::createExtModules(
    cmt2::CircuitOp circuit, firrtl::CircuitOp firrtlCircuit) {
  OpBuilder builder(firrtlCircuit.getBodyBlock(),
                    firrtlCircuit.getBodyBlock()->begin());

  // Find all ExtModuleFirrtlOp in the CMT2 circuit and create corresponding
  // FIRRTL FExtModuleOp
  for (auto &op : circuit.getBodyRegion().front()) {
    auto extMod = dyn_cast<ExtModuleFirrtlOp>(op);
    if (!extMod)
      continue;

    StringRef extModName = extMod.getExtModuleName();

    // Check if already exists (either as FExtModule or regular FModule)
    if (findFIRRTLModuleLike(extModName, firrtlCircuit))
      continue;

    // Build port list from the external module's arguments and bindings
    SmallVector<PortInfo> ports;

    // Add ports from the external module block arguments (clock, reset, etc.).
    // Use the CMT2 extmodule argument types directly (no name-based guessing),
    // so nonstandard clock/reset names still get correct FIRRTL types.
    auto modArgNames = extMod.getArgNames();
    auto &bodyBlock = extMod.getBodyRegion().front();
    if (modArgNames.size() != bodyBlock.getNumArguments()) {
      return extMod.emitOpError("argNames size (")
             << modArgNames.size()
             << ") does not match number of block arguments ("
             << bodyBlock.getNumArguments() << ")";
    }
    for (size_t i = 0; i < modArgNames.size(); ++i) {
      StringRef portName = cast<StringAttr>(modArgNames[i]).getValue();
      Type portType = bodyBlock.getArgument(i).getType();
      ports.push_back({builder.getStringAttr(portName), portType, Direction::In,
                       {}, extMod.getLoc()});
    }

    // Add ports from bind.value and bind.method operations
    // Port names are taken directly from argNames/bodyResNames - they should be unique
    for (auto &bodyOp : extMod.getBodyRegion().front()) {
      if (auto bindValue = dyn_cast<BindValueOp>(bodyOp)) {
        // Value methods: add argument inputs (if any), ready output, and result
        // outputs.

        // Ready port (use readyName if specified)
        if (auto readyName = bindValue.getReadyNameAttr()) {
          ports.push_back({readyName, firrtl::UIntType::get(builder.getContext(), 1),
                           Direction::Out, {}, bindValue.getLoc()});
        }

        // Argument inputs - use names directly from argNames.
        auto funcType = bindValue.getFunctionType();
        auto argNames = bindValue.getArgNames();
        if (!argNames.empty()) {
          if (argNames.size() != funcType.getNumInputs()) {
            return bindValue.emitOpError("argNames size (")
                   << argNames.size() << ") does not match function type inputs ("
                   << funcType.getNumInputs() << ")";
          }
          for (size_t i = 0; i < argNames.size(); ++i) {
            StringRef portName = cast<StringAttr>(argNames[i]).getValue();
            Type portType = funcType.getInput(i);
            ports.push_back({builder.getStringAttr(portName), portType,
                             Direction::In, {}, bindValue.getLoc()});
          }
        }

        // Result outputs - use names directly from bodyResNames
        auto resNames = bindValue.getBodyResNames();
        if (resNames.size() != funcType.getNumResults()) {
          return bindValue.emitOpError("bodyResNames size (")
                 << resNames.size() << ") does not match function type results ("
                 << funcType.getNumResults() << ")";
        }
        for (size_t i = 0; i < funcType.getNumResults(); ++i) {
          StringRef portName = cast<StringAttr>(resNames[i]).getValue();
          ports.push_back({builder.getStringAttr(portName), funcType.getResult(i),
                           Direction::Out, {}, bindValue.getLoc()});
        }
      } else if (auto bindMethod = dyn_cast<BindMethodOp>(bodyOp)) {
        // Action methods: add enable input, ready output, arg inputs, result outputs

        // Enable port
        if (auto enableName = bindMethod.getEnableNameAttr()) {
          ports.push_back({enableName, firrtl::UIntType::get(builder.getContext(), 1),
                           Direction::In, {}, bindMethod.getLoc()});
        }

        // Ready port
        if (auto readyName = bindMethod.getReadyNameAttr()) {
          ports.push_back({readyName, firrtl::UIntType::get(builder.getContext(), 1),
                           Direction::Out, {}, bindMethod.getLoc()});
        }

        // Argument inputs - use names directly from argNames
        auto funcType = bindMethod.getFunctionType();
        auto methodArgNames = bindMethod.getArgNames();
        if (methodArgNames.size() != funcType.getNumInputs()) {
          return bindMethod.emitOpError("argNames size (")
                 << methodArgNames.size()
                 << ") does not match function type inputs ("
                 << funcType.getNumInputs() << ")";
        }
        for (size_t i = 0; i < funcType.getNumInputs(); ++i) {
          StringRef portName = cast<StringAttr>(methodArgNames[i]).getValue();
          ports.push_back({builder.getStringAttr(portName), funcType.getInput(i),
                           Direction::In, {}, bindMethod.getLoc()});
        }

        // Result outputs - use names directly from bodyResNames
        auto resNames = bindMethod.getBodyResNames();
        if (resNames.size() != funcType.getNumResults()) {
          return bindMethod.emitOpError("bodyResNames size (")
                 << resNames.size()
                 << ") does not match function type results ("
                 << funcType.getNumResults() << ")";
        }
        for (size_t i = 0; i < funcType.getNumResults(); ++i) {
          StringRef portName = cast<StringAttr>(resNames[i]).getValue();
          ports.push_back({builder.getStringAttr(portName), funcType.getResult(i),
                           Direction::Out, {}, bindMethod.getLoc()});
        }
      }
    }

    // Create the FIRRTL external module
    builder.create<firrtl::FExtModuleOp>(
        extMod.getLoc(),
        builder.getStringAttr(extModName),
        firrtl::ConventionAttr::get(builder.getContext(), Convention::Internal),
        ports,
        ArrayAttr() /* knownLayers */);
  }

  return success();
}

std::optional<size_t>
LowerCmt2ToFIRRTLPass::getPortIndex(firrtl::InstanceOp &inst,
                                    std::string portName) {
  for (size_t i = 0; i < inst.getNumResults(); ++i) {
    if (inst.getPortName(i).getValue() == portName) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<size_t>
LowerCmt2ToFIRRTLPass::getPortIndex(firrtl::FModuleOp &fMod,
                                    std::string portName) {
  for (size_t i = 0; i < fMod.getNumPorts(); ++i) {
    if (fMod.getPortName(i) == portName) {
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

LogicalResult LowerCmt2ToFIRRTLPass::connectInterfaceBinding(
    firrtl::InstanceOp declInst,  // decl's instance
    StringAttr defEntity,         // either an instance, or an decl
    std::string declFuncName,     // used also as decl's default func name, same
                                  // as the interface's func name
    std::string defFuncName,      // def's func name
    cmt2::InterfaceDeclOp declOp, // decl op
    cmt2::InterfaceOp iface, cmt2::InstanceOp declCmt2Inst,
    ModuleConversionContext &ctx, ImplicitLocOpBuilder &builder) {

  // Find the interface method/value definition
  Cmt2FunctionLike ifaceFunc =
      iface.getFunctionOfName(builder.getStringAttr(declFuncName));

  auto funcType = ifaceFunc.getFunctionType();

  Location loc = declInst.getLoc();
  auto module = ctx.getCmt2Module();
  auto firrtlMod = ctx.getFIRRTLModule();
  auto defCmt2Inst = module.getInstanceOfName(defEntity.getValue());
  auto defDecl = module.getItfcDeclOfName(defEntity.getValue());

  firrtl::InstanceOp defInst;
  Cmt2FunctionLike defFunc;
  if (defCmt2Inst) {
    defInst = ctx.getInstance(defEntity);
    if (auto defMod = dyn_cast<Cmt2ModuleLike>(
            defCmt2Inst.getReferencedModule().getOperation())) {
      for (auto &op : defMod.body().front()) {
        if (auto func = dyn_cast<Cmt2FunctionLike>(op)) {
          if (func.functionName() == defFuncName)
            defFunc = func;
        }
      }
    }
  } else if (defDecl) {

  } else {
    return declCmt2Inst.emitError("Cannot find " + defEntity.getValue().str() +
                                  " in " + module.getName());
  }

  // Connect enable port (methods only, decl out -> def in)
  if (ifaceFunc.getFunctionKind() == FunctionKind::Method) {
    if (auto declIdx =
            getPortIndex(declInst, getItfcDeclFunctionPortName(
                                       declOp, ifaceFunc, PortKind::Help, 1))) {

      Value defValue;
      if (defInst) {
        if (auto defIdx = getPortIndex(
                defInst, getFunctionPortName(defFunc, PortKind::Help, 1)))
          defValue = defInst.getResult(*defIdx);
      } else {
        if (auto defIdx = getPortIndex(
                firrtlMod, getItfcDeclFunctionPortName(defDecl, ifaceFunc,
                                                       PortKind::Help, 1)))
          defValue = firrtlMod.getArgument(*defIdx);
      }

      builder.create<ConnectOp>(loc, defValue, declInst.getResult(*declIdx));
    }
  }

  // Connect ready port (decl in <- def out)
  if (auto declIdx = getPortIndex(
          declInst,
          getItfcDeclFunctionPortName(declOp, ifaceFunc, PortKind::Help, 0))) {

    Value defValue;
    if (defInst) {
      if (auto defIdx = getPortIndex(
              defInst, getFunctionPortName(defFunc, PortKind::Help, 0)))
        defValue = defInst.getResult(*defIdx);
      else {
        return declCmt2Inst.emitError(
            "cannot find " + getFunctionPortName(defFunc, PortKind::Help, 0) +
            " port from def instance " + defInst.getName() + " of module " +
            defInst.getModuleName() + "\n");
      }
    } else {
      if (auto defIdx = getPortIndex(
              firrtlMod, getItfcDeclFunctionPortName(defDecl, ifaceFunc,
                                                     PortKind::Help, 0)))
        defValue = firrtlMod.getArgument(*defIdx);
    }

    // llvm::dbgs() << "declInst has " << declInst.getNumResults()
    //              << " ports, and we will connect the " << *declIdx
    //              << "-th one; and defValue is " << defValue << "\n";

    builder.create<ConnectOp>(loc, declInst.getResult(*declIdx), defValue);
  }

  auto funcInputs = ifaceFunc.getArgumentTypes();
  llvm::dbgs() << funcInputs;

  // Connect Argument ports (decl out -> def in)
  for (auto [idx, argType] : llvm::enumerate(funcType.getInputs())) {
    if (auto declIdx = getPortIndex(
            declInst, getItfcDeclFunctionPortName(declOp, ifaceFunc,
                                                  PortKind::Argument, idx))) {

      Value defValue;
      if (defInst) {
        if (auto defIdx = getPortIndex(
                defInst, getFunctionPortName(defFunc, PortKind::Argument, idx)))
          defValue = defInst.getResult(*defIdx);
      } else {
        if (auto defIdx = getPortIndex(
                firrtlMod, getItfcDeclFunctionPortName(
                               defDecl, ifaceFunc, PortKind::Argument, idx)))
          defValue = firrtlMod.getArgument(*defIdx);
      }

      llvm::dbgs() << "connect with declInst's " << *declIdx << " result\n";
      llvm::dbgs().flush();
      builder.create<ConnectOp>(loc, defValue, declInst.getResult(*declIdx));
    }
  }

  // Connect Result ports (decl in <- def out)
  for (auto [idx, argType] : llvm::enumerate(funcType.getResults())) {
    if (auto declIdx = getPortIndex(
            declInst, getItfcDeclFunctionPortName(declOp, ifaceFunc,
                                                  PortKind::Result, idx))) {
      Value defValue;
      if (defInst) {
        if (auto defIdx = getPortIndex(
                defInst, getFunctionPortName(defFunc, PortKind::Result, idx)))
          defValue = defInst.getResult(*defIdx);
      } else {
        if (auto defIdx = getPortIndex(
                firrtlMod, getItfcDeclFunctionPortName(defDecl, ifaceFunc,
                                                       PortKind::Result, idx)))
          defValue = firrtlMod.getArgument(*defIdx);
      }

      builder.create<ConnectOp>(loc, declInst.getResult(*declIdx), defValue);
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Interface Helpers
//===----------------------------------------------------------------------===//

bool LowerCmt2ToFIRRTLPass::isInterfaceCall(CallOp callOp,
                                            cmt2::ModuleOp module) {
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

void LowerCmt2ToFIRRTLPass::createInterfacePorts(
    cmt2::ModuleOp module, OpBuilder &builder,
    SmallVectorImpl<PortInfo> &ports) {
  // For each InterfaceDeclOp in the module, create ports for all methods/values
  for (auto &op : module.getBodyRegion().front()) {
    auto decl = dyn_cast<InterfaceDeclOp>(op);
    if (!decl)
      continue;

    InterfaceOp iface = getInterfaceForDecl(decl);
    if (!iface) {
      module.emitWarning("Interface not found for declaration: ")
          << decl.getSymName();
      continue;
    }

    // For each method/value in the interface, create corresponding ports
    for (auto &ifaceOp : iface.getBodyRegion().front()) {
      auto func = dyn_cast<cmt2::Cmt2FunctionLike>(ifaceOp);
      auto funcType = func.getFunctionType();
      // Arguments (out)
      for (auto [idx, argType] : llvm::enumerate(funcType.getInputs())) {
        std::string argName =
            getItfcDeclFunctionPortName(decl, func, PortKind::Argument, idx);
        ports.push_back(PortInfo(builder.getStringAttr(argName),
                                 cast<FIRRTLBaseType>(argType), Direction::Out,
                                 {}, func.getLoc()));
      }

      // Results (in)
      for (auto [idx, resType] : llvm::enumerate(funcType.getResults())) {
        std::string resName =
            getItfcDeclFunctionPortName(decl, func, PortKind::Result, idx);
        ports.push_back(PortInfo(builder.getStringAttr(resName),
                                 cast<FIRRTLBaseType>(resType), Direction::In,
                                 {}, func.getLoc()));
      }

      // Ready (in) : method / value
      if (func.getFunctionKind() == FunctionKind::Method ||
          func.getFunctionKind() == FunctionKind::Value) {
        ports.push_back(
            PortInfo(builder.getStringAttr(getItfcDeclFunctionPortName(
                         decl, func, PortKind::Help, 0)),
                     UIntType::get(builder.getContext(), 1), Direction::In, {},
                     func.getLoc()));
      }

      // Enable (out) : method
      if (func.getFunctionKind() == FunctionKind::Method) {
        ports.push_back(
            PortInfo(builder.getStringAttr(getItfcDeclFunctionPortName(
                         decl, func, PortKind::Help, 1)),
                     UIntType::get(builder.getContext(), 1), Direction::Out, {},
                     func.getLoc()));
      }
    }
  }
}

void LowerCmt2ToFIRRTLPass::initializeInterfaceOutPorts(
    cmt2::ModuleOp module, ModuleConversionContext &ctx, OpBuilder &builder) {

  auto firrtlModule = ctx.getFIRRTLModule();
  // Initialize interface output ports with default values
  // Interface ports start after module arguments and function ports
  for (auto &op : module.getBodyRegion().front()) {
    auto decl = dyn_cast<InterfaceDeclOp>(op);
    if (!decl)
      continue;

    InterfaceOp iface = getInterfaceForDecl(decl);
    if (!iface)
      continue;

    // Initialize enable and data output ports for each method/value in the
    // interface
    for (auto &ifaceOp : iface.getBodyRegion().front()) {
      if (auto function = dyn_cast<Cmt2FunctionLike>(ifaceOp)) {
        auto funcType = function.getFunctionType();

        // Argument (out)
        for (auto [idx, argType] : llvm::enumerate(funcType.getInputs())) {
          auto argName = getItfcDeclFunctionPortName(decl, function,
                                                     PortKind::Argument, idx);
          for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts();
               ++portIdx)
            if (firrtlModule.getPortName(portIdx) == argName) {
              Value argPort = firrtlModule.getArgument(portIdx);
              auto firrtlType = cast<FIRRTLBaseType>(argType);
              Value invalid =
                  builder.create<InvalidValueOp>(module.getLoc(), firrtlType);
              builder.create<ConnectOp>(module.getLoc(), argPort, invalid);
            }
        }

        if (function.getFunctionKind() == FunctionKind::Method) {
          auto enableName =
              getItfcDeclFunctionPortName(decl, function, PortKind::Help, 1);
          for (size_t portIdx = 0; portIdx < firrtlModule.getNumPorts();
               ++portIdx)
            if (firrtlModule.getPortName(portIdx) == enableName) {
              Value enablePort = firrtlModule.getArgument(portIdx);
              Value zero = builder.create<ConstantOp>(
                  module.getLoc(), UIntType::get(builder.getContext(), 1),
                  APInt(1, 0));
              builder.create<ConnectOp>(module.getLoc(), enablePort, zero);
            }
        }
      }
    }
  }
}

LogicalResult LowerCmt2ToFIRRTLPass::connectInterfaceCall(
    CallOp callOp, InterfaceDeclOp interfaceDecl, ModuleConversionContext &ctx,
    ImplicitLocOpBuilder &builder) {

  StringAttr declName = interfaceDecl.getSymNameAttr();
  StringAttr funcName = callOp.getMethodOrValue().getLeafReference();
  FModuleOp firrtlModule = ctx.getFIRRTLModule();

  // Find the method/value in the interface to determine its kind
  InterfaceOp iface = getInterfaceForDecl(interfaceDecl);
  if (!iface) {
    return callOp.emitError("Interface not found for declaration: ")
           << declName;
  }

  Cmt2FunctionLike targetFunc = iface.getFunctionOfName(funcName);

  if (!targetFunc) {
    return callOp.emitError("Method/value not found in interface: ")
           << funcName;
  }

  // Find the corresponding ports in the FIRRTL module
  SmallVector<Value> mappedResults;

  if (targetFunc.getFunctionKind() == FunctionKind::Method) {
    // Drive enable signal for method func
    std::string enablePortName = getItfcDeclFunctionPortName(
        interfaceDecl, targetFunc, PortKind::Help, 1);

    if (auto enablePortIdx = getPortIndex(firrtlModule, enablePortName)) {
      auto enablePort = firrtlModule.getArgument(*enablePortIdx);
      Value one = builder.create<ConstantOp>(
          callOp.getLoc(), UIntType::get(builder.getContext(), 1), APInt(1, 1));
      builder.create<ConnectOp>(callOp.getLoc(), enablePort, one);
    } else {
      return callOp.emitError("Enable port not found for interface call: ")
             << enablePortName;
    }
  }

  // Connect input arguments
  for (auto [idx, operand] : llvm::enumerate(callOp.getOperands())) {
    std::string argPortName = getItfcDeclFunctionPortName(
        interfaceDecl, targetFunc, PortKind::Argument, idx);

    if (auto argPortIdx = getPortIndex(firrtlModule, argPortName)) {
      auto argPort = firrtlModule.getArgument(*argPortIdx);
      Value mappedOperand = ctx.getIRMapping().lookupOrDefault(operand);
      if (!mappedOperand) {
        return callOp.emitError(
            "Call operand was not properly mapped to FIRRTL context");
      }

      builder.create<ConnectOp>(callOp.getLoc(), argPort, mappedOperand);
    } else {
      return callOp.emitError("Argument port not found for interface call: ")
             << argPortName;
    }
  }

  // Read output results
  for (auto [idx, operand] : llvm::enumerate(callOp.getResults())) {
    std::string resPortName = getItfcDeclFunctionPortName(
        interfaceDecl, targetFunc, PortKind::Result, idx);

    if (auto resPortIdx = getPortIndex(firrtlModule, resPortName)) {
      auto resPort = firrtlModule.getArgument(*resPortIdx);
      mappedResults.push_back(resPort);
    } else {
      return callOp.emitError("Result port not found for interface call: ")
             << resPortName;
    }
  }

  // Map call results to interface ports
  for (auto [callResult, portValue] :
       llvm::zip(callOp.getResults(), mappedResults)) {
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

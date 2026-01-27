//===- Interpreter.cpp - CMT2 GAA Interpreter -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CMT2 GAA interpreter for debugging and simulation.
//
//===----------------------------------------------------------------------===//

#include "Interpreter.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "circt/Dialect/HW/HWOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/JSON.h"
#include "llvm/ADT/STLExtras.h"

#define DEBUG_TYPE "cmt2-interpreter"

using namespace circt;
using namespace cmt2;
using namespace mlir;

//===----------------------------------------------------------------------===//
// Constructor and Destructor
//===----------------------------------------------------------------------===//

Cmt2Interpreter::Cmt2Interpreter(mlir::ModuleOp module, llvm::raw_ostream &os)
    : module_(module), os_(os) {
  // Initialize control flow plugins
  dynamicControlPlugin_ = std::make_unique<interp::DynamicControlPlugin>();
  staticControlPlugin_ = std::make_unique<interp::StaticControlPlugin>();

  // Initialize default scheduler (ORAAT)
  scheduler_ = std::make_unique<interp::ORAATScheduler>();

  // Register built-in operation handlers
  opRegistry_.registerBuiltinHandlers();
}

Cmt2Interpreter::~Cmt2Interpreter() = default;

//===----------------------------------------------------------------------===//
// Initialization and Reset
//===----------------------------------------------------------------------===//

LogicalResult Cmt2Interpreter::initialize(StringRef circuitName) {
  // Find the circuit (CircuitOp is just a container, doesn't have a name)
  circuit_ = nullptr;
  module_.walk([&](CircuitOp circuit) {
    circuit_ = circuit;
    return WalkResult::interrupt();
  });

  if (!circuit_) {
    os_ << "error: no cmt2.circuit found in input\n";
    return failure();
  }

  // Initialize conflict matrix analysis for all modules
  conflictMatrixAnalysis_ = std::make_unique<ConflictMatrixAnalysis>(circuit_);
  LLVM_DEBUG({
    llvm::dbgs() << "ConflictMatrix analysis initialized:\n";
    conflictMatrixAnalysis_->print(llvm::dbgs());
  });

  // Find the top-level module
  topModule_ = findTopModule();
  if (!topModule_) {
    os_ << "error: no top-level module found in circuit\n";
    return failure();
  }

  // Initialize registers
  initializeRegisters(topModule_);

  // Initialize procedural constructs
  initializeProcConstructs(topModule_);

  // Initialize control flow plugins with the module
  if (dynamicControlPlugin_)
    dynamicControlPlugin_->initialize(topModule_, stateManager_);
  if (staticControlPlugin_)
    staticControlPlugin_->initialize(topModule_, stateManager_);

  // Initialize scheduler with the module
  if (scheduler_)
    scheduler_->initialize(topModule_);

  os_ << "Initialized interpreter\n";
  os_ << "  Top module: " << topModule_.getModuleName() << "\n";
  os_ << "  Registers: " << getRegisterNames().size() << "\n";
  os_ << "  Rules: " << getRuleNames().size() << "\n";
  if (!procFSMStates_.empty())
    os_ << "  Proc rules: " << procFSMStates_.size() << "\n";

  return success();
}

cmt2::ModuleOp Cmt2Interpreter::findTopModule() {
  // Find a module that is not instantiated by any other module
  llvm::DenseSet<StringRef> instantiatedModules;

  circuit_.walk([&](InstanceOp instance) {
    // Get the module name from the instance
    if (auto moduleRef = instance.getModuleNameAttr())
      instantiatedModules.insert(moduleRef.getLeafReference().getValue());
  });

  cmt2::ModuleOp topModule;
  circuit_.walk([&](cmt2::ModuleOp mod) {
    if (!instantiatedModules.contains(mod.getModuleName())) {
      topModule = mod;
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });

  return topModule;
}

void Cmt2Interpreter::initializeRegisters(cmt2::ModuleOp module) {
  registers_.clear();
  cmt2ModuleInstances_.clear();
  moduleRegistry_.clearUnresolvedModules();

  // Recursive helper to initialize instances with hierarchical names
  std::function<void(cmt2::ModuleOp, StringRef)> initializeModule;
  initializeModule = [&](cmt2::ModuleOp mod, StringRef prefix) {
    LLVM_DEBUG(llvm::dbgs() << "Processing module: " << mod.getModuleName()
                            << " (prefix: '" << prefix << "')\n");

    // Walk through direct instances only (not recursively)
    for (auto &op : mod.getBody().front()) {
      auto instance = dyn_cast<InstanceOp>(&op);
      if (!instance)
        continue;

      StringRef localName = instance.getInstanceName();
      std::string fullName = prefix.empty()
          ? localName.str()
          : (prefix.str() + "." + localName.str());

      auto refModule = instance.getReferencedModule();
      if (!refModule) {
        LLVM_DEBUG(llvm::dbgs() << "  Instance: " << fullName << " -> (ref not found)\n");
        continue;
      }

      mlir::Operation *refOp = refModule.getOperation();

      // External FIRRTL module
      if (auto extModule = dyn_cast<ExtModuleFirrtlOp>(refOp)) {
        SmallVector<mlir::NamedAttribute> params;
        if (auto paramsAttr = instance->getAttrOfType<ArrayAttr>("parameters")) {
          for (auto attr : paramsAttr) {
            if (auto namedAttr = dyn_cast<mlir::DictionaryAttr>(attr)) {
              for (auto entry : namedAttr)
                params.push_back(entry);
            }
          }
        }

        // Extract width from the module's read binding if not specified in params
        bool hasWidth = false;
        for (const auto &param : params) {
          if (param.getName() == "width") {
            hasWidth = true;
            break;
          }
        }
        if (!hasWidth) {
          // Try to extract width from the "read" binding's return type
          extModule.walk([&](BindValueOp bindOp) {
            if (bindOp.getSymName() == "read") {
              auto funcType = bindOp.getFunctionType();
              if (funcType.getNumResults() > 0) {
                if (auto firrtlType = dyn_cast<firrtl::FIRRTLBaseType>(funcType.getResult(0))) {
                  int32_t width = firrtlType.getBitWidthOrSentinel();
                  if (width > 0) {
                    params.push_back(NamedAttribute(
                        StringAttr::get(extModule.getContext(), "width"),
                        IntegerAttr::get(IntegerType::get(extModule.getContext(), 32), width)));
                    LLVM_DEBUG(llvm::dbgs() << "  Extracted width=" << width
                                            << " from read binding\n");
                  }
                }
              }
            }
          });
        }

        if (moduleRegistry_.initializeInstance(fullName, extModule, params)) {
          LLVM_DEBUG(llvm::dbgs() << "  Initialized instance via registry: "
                                  << fullName << "\n");
        }
        continue;
      }

      // CMT2 module - track it, create per-instance scheduler, and recurse
      if (auto cmt2Module = dyn_cast<cmt2::ModuleOp>(refOp)) {
        cmt2ModuleInstances_[fullName] = cmt2Module;

        // Create per-instance scheduler for this nested module
        auto instanceScheduler = std::make_unique<interp::ORAATScheduler>();
        instanceScheduler->initialize(cmt2Module);
        instanceSchedulers_[fullName] = std::move(instanceScheduler);

        LLVM_DEBUG(llvm::dbgs() << "  Found CMT2 module instance: " << fullName
                                << " -> " << cmt2Module.getModuleName()
                                << " (scheduler initialized)\n");
        // Recursively initialize nested instances with hierarchical prefix
        initializeModule(cmt2Module, fullName);
        continue;
      }

      // Fallback: Legacy register detection
      StringRef modName = refModule.moduleName();
      if (modName.contains_insensitive("reg") || modName.contains_insensitive("register")) {
        RegisterState state;
        state.name = fullName;
        state.width = 32;
        state.value = APInt(state.width, 0);
        state.hasReset = true;
        state.resetValue = APInt(state.width, 0);
        registers_[fullName] = state;
        LLVM_DEBUG(llvm::dbgs() << "  Found register (legacy): " << fullName << "\n");
      }
    }
  };

  // Start initialization from top module with empty prefix
  initializeModule(module, "");

  // Report any unresolved modules
  const auto &unresolved = moduleRegistry_.getUnresolvedModules();
  if (!unresolved.empty()) {
    LLVM_DEBUG({
      llvm::dbgs() << "  Unresolved external modules:\n";
      for (const auto &name : unresolved)
        llvm::dbgs() << "    - " << name << "\n";
    });
  }
}

void Cmt2Interpreter::reset() {
  cycle_ = 0;
  valueMap_.clear();
  pendingWrites_.clear();
  stepsDoneThisCycle_.clear();
  currentInstancePath_.clear();

  // Reset all instances through the registry
  moduleRegistry_.resetAllInstances();

  // Reset legacy registers to their reset values
  for (auto &entry : registers_) {
    RegisterState &state = entry.second;
    if (state.hasReset) {
      state.value = state.resetValue;
    }
  }

  // Reset all proc FSM states to idle
  for (auto &entry : procFSMStates_) {
    entry.second.currentState = 0;
    entry.second.isRunning = false;
  }

  // Reset modular infrastructure
  stateManager_.reset();
  if (dynamicControlPlugin_)
    dynamicControlPlugin_->reset();
  if (staticControlPlugin_)
    staticControlPlugin_->reset();
  if (scheduler_)
    scheduler_->reset();

  os_ << "Reset to cycle 0\n";
}

void Cmt2Interpreter::setScheduler(std::unique_ptr<interp::Scheduler> scheduler) {
  scheduler_ = std::move(scheduler);
  // If already initialized, re-initialize the new scheduler
  if (topModule_ && scheduler_)
    scheduler_->initialize(topModule_);
}

//===----------------------------------------------------------------------===//
// Execution Control
//===----------------------------------------------------------------------===//

std::vector<RuleResult> Cmt2Interpreter::step() {
  std::vector<RuleResult> results;

  // Clear pending writes from previous cycle
  pendingWrites_.clear();
  stepsDoneThisCycle_.clear();
  methodsCalledThisCycle_.clear();

  // Tick all module instances (start of cycle)
  moduleRegistry_.tickAllInstances();

  // Tick control flow plugins (for plugin-based execution)
  if (usePluginExecution_) {
    if (dynamicControlPlugin_)
      dynamicControlPlugin_->tick();
    if (staticControlPlugin_)
      staticControlPlugin_->tick();
  }

  //===--------------------------------------------------------------------===//
  // On-Demand Guard Evaluation with Wire Propagation
  //
  // This implements correct GAA semantics where:
  // 1. Rules are evaluated in scheduler-determined precedence order
  // 2. Guards see wire values updated by earlier-fired rules in the same cycle
  // 3. This enables bypass patterns like `!full | deqed` to work correctly
  //
  // Integration with Scheduler infrastructure:
  // - Top-level rules: Use scheduler_ for priority ordering and conflict checking
  // - Nested module rules: Use per-instance conflict checking via conflictMatrixAnalysis_
  //===--------------------------------------------------------------------===//

  // Collect all rules from all modules with their precedence
  struct RuleInfo {
    std::string fullName;
    std::string instancePath;
    std::string localName;
    Operation *moduleOp;  // Store as Operation* to avoid const issues
    unsigned precedence;
  };
  std::vector<RuleInfo> allRules;

  // Get scheduler for priority queries
  // Note: We use getName() instead of dynamic_cast since RTTI is disabled
  interp::ORAATScheduler *oraatScheduler = nullptr;
  if (scheduler_ && scheduler_->getName() == "ORAAT") {
    oraatScheduler = static_cast<interp::ORAATScheduler *>(scheduler_.get());
  }

  // Helper to collect rules from a module using scheduler for priorities
  auto collectModuleRules = [&](cmt2::ModuleOp mod, StringRef instancePath) {
    // Get the appropriate scheduler for this module
    interp::ORAATScheduler *moduleScheduler = nullptr;
    if (instancePath.empty()) {
      // Top-level module uses the main scheduler
      moduleScheduler = oraatScheduler;
    } else {
      // Nested module uses its per-instance scheduler
      auto it = instanceSchedulers_.find(instancePath);
      if (it != instanceSchedulers_.end()) {
        moduleScheduler = it->second.get();
      }
    }

    unsigned textualOrder = 0;
    for (auto &op : mod.getBody().front()) {
      if (auto rule = dyn_cast<RuleOp>(&op)) {
        RuleInfo info;
        info.localName = rule.getSymName().str();
        info.instancePath = instancePath.str();
        info.fullName = instancePath.empty()
            ? info.localName
            : (instancePath.str() + "." + info.localName);
        info.moduleOp = mod.getOperation();

        // Use scheduler for priority (works for both top-level and nested modules)
        if (moduleScheduler) {
          info.precedence = moduleScheduler->getPriority(info.localName);
        } else {
          // Fallback to textual order if no scheduler available
          info.precedence = 1000 + textualOrder;
        }
        allRules.push_back(info);
        textualOrder++;
      } else if (auto procRule = dyn_cast<ProcRuleOp>(&op)) {
        RuleInfo info;
        info.localName = procRule.getSymName().str();
        info.instancePath = instancePath.str();
        info.fullName = instancePath.empty()
            ? info.localName
            : (instancePath.str() + "." + info.localName);
        info.moduleOp = mod.getOperation();

        // Use scheduler for priority (works for both top-level and nested modules)
        if (moduleScheduler) {
          info.precedence = moduleScheduler->getPriority(info.localName);
        } else {
          // Fallback to textual order if no scheduler available
          info.precedence = 1000 + textualOrder;
        }
        allRules.push_back(info);
        textualOrder++;
      }
    }
  };

  // Collect from top module and all nested modules (each uses its own scheduler)
  collectModuleRules(topModule_, "");
  for (const auto &entry : cmt2ModuleInstances_) {
    collectModuleRules(entry.second, entry.first());
  }

  // Sort all rules by (instancePath, precedence) so we process in correct order
  // Top-level rules come first, then nested module rules
  // Within each module, lower precedence number = higher priority
  std::stable_sort(allRules.begin(), allRules.end(),
                   [](const RuleInfo &a, const RuleInfo &b) {
                     // Top-level rules (empty instancePath) have highest priority
                     if (a.instancePath.empty() != b.instancePath.empty())
                       return a.instancePath.empty();
                     // Within same module level, sort by precedence
                     if (a.instancePath == b.instancePath)
                       return a.precedence < b.precedence;
                     // Different nested modules - keep original order
                     return a.instancePath < b.instancePath;
                   });

  // Track which rules fired for conflict detection
  llvm::StringSet<> firedRules;
  llvm::StringSet<> firedTopLevelRules;  // Track top-level rules separately for scheduler
  std::vector<std::string> rulesToFire;
  std::vector<std::string> enabledButBlocked;

  // Get AnnotationScheduler if available for conflict checking
  // Note: We use getName() instead of dynamic_cast since RTTI is disabled
  interp::AnnotationScheduler *annotationScheduler = nullptr;
  if (scheduler_ && scheduler_->getName() == "Annotation") {
    annotationScheduler = static_cast<interp::AnnotationScheduler *>(scheduler_.get());
  }

  // Get conflict matrix for top module
  const ModuleConflictMatrix *topModuleConflictMatrix = nullptr;
  if (conflictMatrixAnalysis_) {
    topModuleConflictMatrix = conflictMatrixAnalysis_->getModuleMatrix(
        topModule_.getSymNameAttr());
  }

  // Helper to check if a rule can fire (not conflicting with already-fired rules)
  // Uses scheduler infrastructure for conflict detection.
  auto canFireRule = [&](const RuleInfo &info) -> bool {
    if (info.instancePath.empty()) {
      // Top-level rules: Use scheduler's conflict information
      // 1. Check AnnotationScheduler's explicit conflict annotations
      if (annotationScheduler) {
        for (const auto &firedEntry : firedTopLevelRules) {
          if (annotationScheduler->conflicts(info.localName, firedEntry.getKey())) {
            LLVM_DEBUG(llvm::dbgs() << "Rule " << info.fullName
                                    << " blocked by scheduler conflict with "
                                    << firedEntry.getKey() << "\n");
            return false;
          }
        }
      }

      // 2. Check conflict matrix for rule-to-rule conflicts in top module
      if (topModuleConflictMatrix) {
        StringAttr ruleAttr = StringAttr::get(topModule_.getContext(), info.localName);
        for (const auto &firedEntry : firedTopLevelRules) {
          StringAttr firedRuleAttr = StringAttr::get(topModule_.getContext(),
                                                      firedEntry.getKey());
          Relationship rel = topModuleConflictMatrix->getRelationship(
              firedRuleAttr, ruleAttr);
          if (rel == Relationship::Conflict) {
            LLVM_DEBUG(llvm::dbgs() << "Rule " << info.fullName
                                    << " blocked by conflict matrix with "
                                    << firedEntry.getKey() << "\n");
            return false;
          }
        }
      }

      // No conflicts found - rule can fire
      return true;
    }

    // For nested module rules: check conflict matrix for method-to-rule conflicts
    auto calledMethodsIt = methodsCalledThisCycle_.find(info.instancePath);
    if (calledMethodsIt == methodsCalledThisCycle_.end())
      return true;  // No methods called on this instance

    auto mod = cast<cmt2::ModuleOp>(info.moduleOp);
    const ModuleConflictMatrix *conflictMatrix = nullptr;
    if (conflictMatrixAnalysis_) {
      conflictMatrix = conflictMatrixAnalysis_->getModuleMatrix(
          mod.getSymNameAttr());
    }

    if (!conflictMatrix)
      return true;

    StringAttr ruleAttr = StringAttr::get(mod.getContext(), info.localName);
    for (const auto &methodName : calledMethodsIt->second) {
      StringAttr methodAttr = StringAttr::get(mod.getContext(), methodName.first());
      Relationship rel = conflictMatrix->getRelationship(methodAttr, ruleAttr);
      if (rel == Relationship::Conflict) {
        LLVM_DEBUG(llvm::dbgs() << "Rule " << info.fullName
                                << " blocked by conflict with method " << methodName.first()
                                << "\n");
        return false;
      }
    }
    return true;
  };

  // Helper to evaluate a single rule's guard
  auto evaluateSingleRuleGuard = [&](const RuleInfo &info) -> bool {
    std::string savedPath = currentInstancePath_;
    currentInstancePath_ = info.instancePath;

    bool enabled = false;
    auto mod = cast<cmt2::ModuleOp>(info.moduleOp);

    // Find the rule and evaluate its guard
    for (auto &op : mod.getBody().front()) {
      if (auto rule = dyn_cast<RuleOp>(&op)) {
        if (rule.getSymName() == info.localName) {
          bool explicitGuard = evaluateGuard(rule.getGuard());
          bool methodGuards = evaluateMethodGuards(rule.getBody());
          enabled = explicitGuard && methodGuards;
          break;
        }
      } else if (auto procRule = dyn_cast<ProcRuleOp>(&op)) {
        if (procRule.getSymName() == info.localName) {
          enabled = isProcRuleEnabled(procRule);
          break;
        }
      }
    }

    currentInstancePath_ = savedPath;
    return enabled;
  };

  // Helper to execute a rule body
  auto executeRuleBody = [&](const RuleInfo &info) {
    std::string savedPath = currentInstancePath_;
    currentInstancePath_ = info.instancePath;
    auto mod = cast<cmt2::ModuleOp>(info.moduleOp);

    for (auto &op : mod.getBody().front()) {
      if (auto rule = dyn_cast<RuleOp>(&op)) {
        if (rule.getSymName() == info.localName) {
          executeBody(rule.getBody());
          break;
        }
      } else if (auto procRule = dyn_cast<ProcRuleOp>(&op)) {
        if (procRule.getSymName() == info.localName) {
          auto fsmIt = procFSMStates_.find(info.fullName);
          if (fsmIt != procFSMStates_.end()) {
            executeProcRuleStep(procRule, fsmIt->second);
          }
          break;
        }
      }
    }

    currentInstancePath_ = savedPath;
  };

  // On-demand evaluation: for each rule in precedence order, evaluate guard
  // and execute if enabled
  LLVM_DEBUG(llvm::dbgs() << "Cycle " << cycle_ << ": evaluating " << allRules.size()
                          << " rules in precedence order\n");

  for (const RuleInfo &info : allRules) {
    // Evaluate guard (may see wire values from earlier-fired rules)
    if (evaluateSingleRuleGuard(info)) {
      // Guard passed - check if we can fire (no conflicts)
      if (canFireRule(info)) {
        LLVM_DEBUG(llvm::dbgs() << "  " << info.fullName << ": FIRING\n");

        // Execute the rule body (this may update wire values!)
        executeRuleBody(info);

        // Track that this rule fired
        firedRules.insert(info.fullName);
        rulesToFire.push_back(info.fullName);

        // Track top-level rules separately for scheduler conflict checking
        if (info.instancePath.empty()) {
          firedTopLevelRules.insert(info.localName);
        }

        RuleResult result;
        result.ruleName = info.fullName;
        result.guardEnabled = true;
        result.fired = true;
        results.push_back(result);
      } else {
        LLVM_DEBUG(llvm::dbgs() << "  " << info.fullName << ": enabled but blocked\n");
        enabledButBlocked.push_back(info.fullName);
      }
    } else {
      LLVM_DEBUG(llvm::dbgs() << "  " << info.fullName << ": guard failed\n");
    }
  }

  // Continue executing any running proc.rules
  if (useDirectProcInterpretation_) {
    // Direct interpretation path - check procExecStates_
    for (auto &execEntry : procExecStates_) {
      ProcRuleExecState &execState = execEntry.second;
      if (execState.isRunning) {
        // Execute one cycle of the proc.rule
        ProcRuleOp procRule = execState.ruleOp;
        auto fsmIt = procFSMStates_.find(execEntry.first());
        if (fsmIt != procFSMStates_.end()) {
          executeProcRuleStep(procRule, fsmIt->second);
        }

        // Add to results as running if not already there
        bool alreadyInResults = firedRules.count(execEntry.first().str()) > 0;
        if (!alreadyInResults) {
          RuleResult result;
          result.ruleName = execEntry.first().str();
          result.guardEnabled = true;
          result.fired = true;  // Running counts as fired
          results.push_back(result);
        }
      }
    }
  } else {
    // Legacy FSM path - check procFSMStates_
    for (auto &fsmEntry : procFSMStates_) {
      ProcFSMState &fsm = fsmEntry.second;
      if (fsm.isRunning && fsm.currentState > 0) {
        // Find the proc.rule op
        topModule_.walk([&](ProcRuleOp procRule) {
          if (procRule.getSymName() == fsmEntry.first()) {
            executeProcRuleStep(procRule, fsm);
            return WalkResult::interrupt();
          }
          return WalkResult::advance();
        });

        // Add to results as running if not already there
        bool alreadyInResults = firedRules.count(fsmEntry.first().str()) > 0;
        if (!alreadyInResults) {
          RuleResult result;
          result.ruleName = fsmEntry.first().str();
          result.guardEnabled = true;
          result.fired = true;  // Running counts as fired
          results.push_back(result);
        }
      }
    }
  }

  // Add results for rules that were enabled but didn't fire
  for (const std::string &ruleName : enabledButBlocked) {
    RuleResult result;
    result.ruleName = ruleName;
    result.guardEnabled = true;
    result.fired = false;
    results.push_back(result);
  }

  // Phase 4: Apply state updates atomically
  applyStateUpdates();

  // Add trace entry if tracing is enabled
  if (tracingEnabled_) {
    addTraceEntry(results);
  }

  // Increment cycle
  cycle_++;
  if (usePluginExecution_)
    stateManager_.incrementCycle();

  return results;
}

std::optional<Breakpoint> Cmt2Interpreter::run(uint64_t maxCycles) {
  uint64_t startCycle = cycle_;

  while (cycle_ - startCycle < maxCycles) {
    auto results = step();

    // Check breakpoints
    if (auto bp = checkBreakpoints(results)) {
      return bp;
    }
  }

  return std::nullopt;
}

std::optional<Breakpoint> Cmt2Interpreter::continueExec(uint64_t maxCycles) {
  return run(maxCycles);
}

//===----------------------------------------------------------------------===//
// Rule Evaluation
//===----------------------------------------------------------------------===//

std::vector<std::string> Cmt2Interpreter::evaluateGuards() {
  std::vector<std::string> enabledRules;

  // Helper to evaluate rules in a module with a given instance path
  auto evaluateModuleRules = [&](cmt2::ModuleOp mod, StringRef instancePath) {
    // Save and update current instance path for nested method calls during guard evaluation
    std::string savedPath = currentInstancePath_;
    currentInstancePath_ = instancePath.str();

    // Check regular rules in this module
    for (auto &op : mod.getBody().front()) {
      if (auto rule = dyn_cast<RuleOp>(&op)) {
        Region &guardRegion = rule.getGuard();
        // Evaluate explicit guard AND method guards from body
        bool explicitGuard = evaluateGuard(guardRegion);
        bool methodGuards = evaluateMethodGuards(rule.getBody());
        LLVM_DEBUG(llvm::dbgs() << "Rule " << rule.getSymName()
                                << ": explicitGuard=" << explicitGuard
                                << ", methodGuards=" << methodGuards << "\n");
        if (explicitGuard && methodGuards) {
          std::string ruleName = instancePath.empty()
              ? rule.getSymName().str()
              : (instancePath.str() + "." + rule.getSymName().str());
          enabledRules.push_back(ruleName);
        }
      }
    }

    // Check proc.rules (only enabled when idle)
    for (auto &op : mod.getBody().front()) {
      if (auto procRule = dyn_cast<ProcRuleOp>(&op)) {
        if (isProcRuleEnabled(procRule)) {
          std::string ruleName = instancePath.empty()
              ? procRule.getSymName().str()
              : (instancePath.str() + "." + procRule.getSymName().str());
          enabledRules.push_back(ruleName);
        }
      }
    }

    // Restore instance path
    currentInstancePath_ = savedPath;
  };

  // Evaluate rules in top module
  evaluateModuleRules(topModule_, "");

  // Evaluate rules in all nested CMT2 module instances
  for (const auto &entry : cmt2ModuleInstances_) {
    evaluateModuleRules(entry.second, entry.first());
  }

  return enabledRules;
}

bool Cmt2Interpreter::isRuleEnabled(StringRef ruleName) {
  bool enabled = false;

  // Check regular rules
  topModule_.walk([&](RuleOp rule) {
    if (rule.getSymName() == ruleName) {
      enabled = evaluateGuard(rule.getGuard());
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });

  if (!enabled) {
    // Check proc.rules
    topModule_.walk([&](ProcRuleOp procRule) {
      if (procRule.getSymName() == ruleName) {
        enabled = isProcRuleEnabled(procRule);
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
  }

  return enabled;
}

LogicalResult Cmt2Interpreter::fireRule(StringRef ruleName) {
  bool found = false;

  // Try regular rules first
  topModule_.walk([&](RuleOp rule) {
    if (rule.getSymName() == ruleName) {
      executeBody(rule.getBody());
      found = true;
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });

  if (!found) {
    // Try proc.rules
    topModule_.walk([&](ProcRuleOp procRule) {
      if (procRule.getSymName() == ruleName) {
        auto fsmIt = procFSMStates_.find(ruleName);
        if (fsmIt != procFSMStates_.end()) {
          executeProcRuleStep(procRule, fsmIt->second);
        }
        found = true;
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
  }

  if (!found) {
    os_ << "error: rule '" << ruleName << "' not found\n";
    return failure();
  }

  applyStateUpdates();
  return success();
}

bool Cmt2Interpreter::evaluateGuard(Region &guardRegion) {
  if (guardRegion.empty())
    return true;

  Block &block = guardRegion.front();
  valueMap_.clear();

  // Execute guard operations
  for (Operation &op : block) {
    if (auto returnOp = dyn_cast<ReturnOp>(op)) {
      // Get the return value
      if (returnOp.getNumOperands() > 0) {
        InterpValue result = getValue(returnOp.getOperand(0));
        return result != 0;
      }
      return true;
    }
    executeOp(&op);
  }

  return true;
}

bool Cmt2Interpreter::evaluateMethodGuards(Region &bodyRegion) {
  if (bodyRegion.empty())
    return true;

  // Scan the body for cmt2.call operations and evaluate their method guards
  for (Operation &op : bodyRegion.front()) {
    if (auto callOp = dyn_cast<CallOp>(&op)) {
      StringRef instanceName = callOp.getCalleeAttr().getLeafReference().getValue();
      StringRef methodName = callOp.getMethodOrValueAttr().getLeafReference().getValue();

      // Build the full hierarchical instance name
      std::string fullInstanceName = currentInstancePath_.empty()
          ? instanceName.str()
          : (currentInstancePath_ + "." + instanceName.str());

      LLVM_DEBUG({
        llvm::dbgs() << "evaluateMethodGuards: checking " << fullInstanceName
                     << "." << methodName << "\n";
        llvm::dbgs() << "  cmt2ModuleInstances_ has " << cmt2ModuleInstances_.size() << " entries\n";
        for (const auto &entry : cmt2ModuleInstances_) {
          llvm::dbgs() << "    - " << entry.first() << "\n";
        }
      });

      // Check if this is a CMT2 module instance
      auto cmt2It = cmt2ModuleInstances_.find(fullInstanceName);

      // Debug: print when NOT found
      if (cmt2It == cmt2ModuleInstances_.end()) {
        LLVM_DEBUG(llvm::dbgs() << "  NOT FOUND in cmt2ModuleInstances_\n");
        // Try to find in module registry (external FIRRTL modules)
        // For external modules, we can't evaluate their guards here
        continue;
      }

      if (cmt2It != cmt2ModuleInstances_.end()) {
        cmt2::ModuleOp cmt2Module = cmt2It->second;

        // Save and update current instance path for nested calls
        std::string savedPath = currentInstancePath_;
        currentInstancePath_ = fullInstanceName;

        // Save current valueMap
        auto savedValueMap = std::move(valueMap_);
        valueMap_.clear();

        bool guardPassed = true;

        // Look for the method or value in the CMT2 module
        cmt2::MethodOp methodOp;
        cmt2Module.walk([&](cmt2::MethodOp m) {
          if (m.getSymName() == methodName) {
            methodOp = m;
            return WalkResult::interrupt();
          }
          return WalkResult::advance();
        });

        if (methodOp) {
          // Evaluate method guard
          guardPassed = evaluateGuard(methodOp.getGuard());

          // Also check nested method guards in the method body
          if (guardPassed) {
            guardPassed = evaluateMethodGuards(methodOp.getBody());
          }
        } else {
          // Try as a value
          cmt2::ValueOp valueOp;
          cmt2Module.walk([&](cmt2::ValueOp v) {
            if (v.getSymName() == methodName) {
              valueOp = v;
              return WalkResult::interrupt();
            }
            return WalkResult::advance();
          });

          if (valueOp) {
            // Evaluate value guard
            guardPassed = evaluateGuard(valueOp.getGuard());
            LLVM_DEBUG(llvm::dbgs() << "  Value guard for " << methodName
                                    << ": " << (guardPassed ? "passed" : "failed") << "\n");

            // Also check nested method guards in the value body
            if (guardPassed) {
              guardPassed = evaluateMethodGuards(valueOp.getBody());
            }
          }
        }

        // Restore saved state
        valueMap_ = std::move(savedValueMap);
        currentInstancePath_ = savedPath;

        if (!guardPassed) {
          return false;  // Method guard failed, rule cannot fire
        }
      }
    }
  }

  return true;  // All method guards passed
}

void Cmt2Interpreter::executeBody(Region &bodyRegion) {
  if (bodyRegion.empty())
    return;

  Block &block = bodyRegion.front();
  valueMap_.clear();

  // Execute body operations
  for (Operation &op : block) {
    if (isa<ReturnOp>(op))
      break;
    executeOp(&op);
  }
}

//===----------------------------------------------------------------------===//
// Conflict Resolution
//===----------------------------------------------------------------------===//

std::vector<std::string> Cmt2Interpreter::resolveConflicts(
    const std::vector<std::string> &enabledRules) {
  // For hierarchical modules, each module has its own scheduling domain.
  // Rules in different modules can fire in parallel.
  //
  // Within each module:
  // - Top-level user modules use ORAAT (one rule at a time)
  // - Nested CMT2 modules (like FIFO internals) fire all enabled rules
  //   since they implement atomic module behavior
  //
  // The precedence attribute determines execution order within a module.

  if (enabledRules.empty())
    return {};

  // Group rules by their module instance path
  // e.g., "producer" -> "", "fifo.next" -> "fifo"
  llvm::StringMap<std::vector<std::string>> rulesByModule;
  for (const std::string &ruleName : enabledRules) {
    StringRef fullName(ruleName);
    size_t lastDot = fullName.rfind('.');
    std::string instancePath = "";
    if (lastDot != StringRef::npos) {
      instancePath = fullName.substr(0, lastDot).str();
    }
    rulesByModule[instancePath].push_back(ruleName);
  }

  std::vector<std::string> selectedRules;

  for (auto &entry : rulesByModule) {
    StringRef instancePath = entry.first();
    std::vector<std::string> &moduleRules = entry.second;

    if (moduleRules.empty())
      continue;

    // Get the module for precedence lookup
    cmt2::ModuleOp mod = topModule_;
    if (!instancePath.empty()) {
      auto it = cmt2ModuleInstances_.find(instancePath);
      if (it != cmt2ModuleInstances_.end()) {
        mod = it->second;
      }
    }

    // Build priority map from module's precedence attribute
    llvm::StringMap<unsigned> priorityMap;
    unsigned nextPriority = 0;

    if (auto precAttr = mod->getAttrOfType<ArrayAttr>("precedence")) {
      for (auto chain : precAttr) {
        if (auto chainArray = dyn_cast<ArrayAttr>(chain)) {
          for (auto elem : chainArray) {
            if (auto symRef = dyn_cast<FlatSymbolRefAttr>(elem)) {
              StringRef localRuleName = symRef.getValue();
              if (!priorityMap.count(localRuleName)) {
                priorityMap[localRuleName] = nextPriority++;
              }
            }
          }
        }
      }
    }

    // For top module (empty instance path): use ORAAT - select highest priority rule
    // For nested modules: fire all enabled rules that don't conflict with called methods
    if (instancePath.empty()) {
      // ORAAT for top module
      std::string bestRule = moduleRules[0];
      unsigned bestPriority = UINT_MAX;

      for (const std::string &ruleName : moduleRules) {
        StringRef fullName(ruleName);
        unsigned priority = UINT_MAX;
        auto it = priorityMap.find(fullName);
        if (it != priorityMap.end()) {
          priority = it->second;
        }

        if (priority < bestPriority) {
          bestPriority = priority;
          bestRule = ruleName;
        }
      }
      selectedRules.push_back(bestRule);
    } else {
      // For nested modules: filter rules that conflict with called methods,
      // then sort remaining rules by precedence order.
      //
      // Conflict detection uses the module's conflict matrix, which defines
      // relationships between rules and methods. If a method and rule have
      // Relationship::Conflict in the matrix, they cannot execute in the same cycle.

      auto calledMethodsIt = methodsCalledThisCycle_.find(instancePath);
      llvm::StringSet<> calledMethods;
      if (calledMethodsIt != methodsCalledThisCycle_.end()) {
        calledMethods = calledMethodsIt->second;
      }

      // Get the conflict matrix for this module
      const ModuleConflictMatrix *conflictMatrix = nullptr;
      if (conflictMatrixAnalysis_) {
        conflictMatrix = conflictMatrixAnalysis_->getModuleMatrix(
            mod.getSymNameAttr());
      }

      // Filter out conflicting rules using the conflict matrix
      std::vector<std::string> nonConflictingRules;
      for (const std::string &ruleName : moduleRules) {
        StringRef fullName(ruleName);
        size_t lastDot = fullName.rfind('.');
        StringRef localName = lastDot != StringRef::npos ? fullName.substr(lastDot + 1) : fullName;

        // Check if this rule conflicts with any called method using the conflict matrix
        bool conflicts = false;

        if (conflictMatrix) {
          StringAttr ruleAttr = StringAttr::get(mod.getContext(), localName);
          for (const auto &methodName : calledMethods) {
            StringAttr methodAttr = StringAttr::get(mod.getContext(), methodName.first());
            Relationship rel = conflictMatrix->getRelationship(methodAttr, ruleAttr);
            if (rel == Relationship::Conflict) {
              conflicts = true;
              LLVM_DEBUG(llvm::dbgs() << "resolveConflicts: blocking " << ruleName
                                      << " due to conflict with method " << methodName.first()
                                      << " (relationship: Conflict)\n");
              break;
            }
          }
        }

        if (!conflicts) {
          nonConflictingRules.push_back(ruleName);
        }
      }

      // Sort by precedence order (lower priority number = higher precedence = fires first)
      std::sort(nonConflictingRules.begin(), nonConflictingRules.end(),
                [&](const std::string &a, const std::string &b) {
                  StringRef aFull(a), bFull(b);
                  size_t aLastDot = aFull.rfind('.');
                  size_t bLastDot = bFull.rfind('.');
                  StringRef aLocal = aLastDot != StringRef::npos ? aFull.substr(aLastDot + 1) : aFull;
                  StringRef bLocal = bLastDot != StringRef::npos ? bFull.substr(bLastDot + 1) : bFull;

                  unsigned aPrio = UINT_MAX, bPrio = UINT_MAX;
                  auto aIt = priorityMap.find(aLocal);
                  if (aIt != priorityMap.end()) aPrio = aIt->second;
                  auto bIt = priorityMap.find(bLocal);
                  if (bIt != priorityMap.end()) bPrio = bIt->second;

                  return aPrio < bPrio;
                });

      // Add all non-conflicting rules from nested module in precedence order
      for (const auto &rule : nonConflictingRules) {
        selectedRules.push_back(rule);
      }
    }
  }

  return selectedRules;
}

//===----------------------------------------------------------------------===//
// State Inspection
//===----------------------------------------------------------------------===//

std::optional<InterpValue> Cmt2Interpreter::readRegister(StringRef name) {
  // First check the registry
  if (moduleRegistry_.hasInstance(name)) {
    // Call the read method
    auto result = moduleRegistry_.callMethod(name, "read", {});
    if (result && !result->empty())
      return (*result)[0];
  }

  // Fallback to legacy registers
  auto it = registers_.find(name);
  if (it == registers_.end())
    return std::nullopt;
  return it->second.value;
}

LogicalResult Cmt2Interpreter::writeRegister(StringRef name,
                                              const InterpValue &value) {
  if (moduleRegistry_.hasInstance(name)) {
    // Best-effort: treat this as a register-like instance with a write method.
    // For built-in Reg interpreter, this queues a pending write that will
    // commit at end of cycle.
    auto result = moduleRegistry_.callMethod(name, "write", {value});
    if (!result)
      return failure();
    return success();
  }

  auto it = registers_.find(name);
  if (it == registers_.end()) {
    os_ << "error: register '" << name << "' not found\n";
    return failure();
  }

  it->second.value = value;
  return success();
}

std::vector<std::string> Cmt2Interpreter::getRegisterNames() const {
  std::vector<std::string> names;

  // Get names from the registry
  auto registryNames = moduleRegistry_.getAllInstanceNames();
  names.insert(names.end(), registryNames.begin(), registryNames.end());

  // Add legacy register names
  for (const auto &entry : registers_) {
    // Avoid duplicates
    if (std::find(names.begin(), names.end(), entry.first().str()) == names.end())
      names.push_back(entry.first().str());
  }
  return names;
}

std::vector<std::string> Cmt2Interpreter::getModuleNames() {
  std::vector<std::string> names;

  // Add the top module if present
  if (topModule_) {
    names.push_back(topModule_.getSymName().str());
  }

  // Add nested CMT2 module instances
  for (const auto &entry : cmt2ModuleInstances_) {
    names.push_back(entry.first().str());
  }

  return names;
}

std::vector<std::string> Cmt2Interpreter::getRuleNames() const {
  std::vector<std::string> names;

  // Helper to collect rules from a module with a given instance path
  auto collectModuleRules = [&](cmt2::ModuleOp mod, StringRef instancePath) {
    for (auto &op : mod.getBody().front()) {
      if (auto rule = dyn_cast<RuleOp>(&op)) {
        std::string ruleName = instancePath.empty()
            ? rule.getSymName().str()
            : (instancePath.str() + "." + rule.getSymName().str());
        names.push_back(ruleName);
      } else if (auto procRule = dyn_cast<ProcRuleOp>(&op)) {
        std::string ruleName = instancePath.empty()
            ? procRule.getSymName().str()
            : (instancePath.str() + "." + procRule.getSymName().str());
        names.push_back(ruleName);
      }
    }
  };

  // Collect rules from top module
  if (topModule_) {
    collectModuleRules(topModule_, "");
  }

  // Collect rules from nested CMT2 module instances
  for (const auto &entry : cmt2ModuleInstances_) {
    collectModuleRules(entry.second, entry.first());
  }

  return names;
}

std::vector<std::string> Cmt2Interpreter::getMethodNames() const {
  std::vector<std::string> names;
  if (topModule_) {
    topModule_->walk([&](MethodOp method) {
      names.push_back(method.getSymName().str());
    });
  }
  return names;
}

std::vector<std::string> Cmt2Interpreter::getValueNames() const {
  std::vector<std::string> names;
  if (topModule_) {
    topModule_->walk([&](ValueOp value) {
      names.push_back(value.getSymName().str());
    });
  }
  return names;
}

std::optional<InterpValue> Cmt2Interpreter::readValue(StringRef name) {
  // Find and evaluate the value
  std::optional<InterpValue> result;

  topModule_.walk([&](ValueOp value) {
    if (value.getSymName() == name) {
      // Evaluate the guard first
      if (evaluateGuard(value.getGuard())) {
        // Execute the body
        valueMap_.clear();
        Block &block = value.getBody().front();
        for (Operation &op : block) {
          if (auto returnOp = dyn_cast<ReturnOp>(op)) {
            if (returnOp.getNumOperands() > 0) {
              result = getValue(returnOp.getOperand(0));
            }
            break;
          }
          executeOp(&op);
        }
      }
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });

  return result;
}

std::optional<std::vector<InterpValue>> Cmt2Interpreter::callMethod(
    StringRef instanceName, StringRef methodName,
    const std::vector<InterpValue> &args) {
  // Build the full hierarchical name
  std::string fullInstanceName = currentInstancePath_.empty()
      ? instanceName.str()
      : (currentInstancePath_ + "." + instanceName.str());

  LLVM_DEBUG(llvm::dbgs() << "callMethod: instance='" << instanceName
                          << "' full='" << fullInstanceName
                          << "' method='" << methodName << "'\n");

  // First, try the module interpreter registry (external modules)
  if (moduleRegistry_.hasInstance(fullInstanceName)) {
    llvm::ArrayRef<interp::InterpValue> interpArgs(args);
    return moduleRegistry_.callMethod(fullInstanceName, methodName, interpArgs);
  }

  // Second, try CMT2 module instances (nested modules)
  auto cmt2It = cmt2ModuleInstances_.find(fullInstanceName);
  if (cmt2It != cmt2ModuleInstances_.end()) {
    cmt2::ModuleOp cmt2Module = cmt2It->second;

    // Track this method call for conflict detection in resolveConflicts()
    methodsCalledThisCycle_[fullInstanceName].insert(methodName);

    // Save and update current instance path for nested calls
    std::string savedPath = currentInstancePath_;
    currentInstancePath_ = fullInstanceName;

    // Save current valueMap (we need a fresh one for the nested method)
    auto savedValueMap = std::move(valueMap_);
    valueMap_.clear();

    std::optional<std::vector<InterpValue>> result;

    // Look for the method or value in the CMT2 module
    // Try as a method first
    cmt2::MethodOp methodOp;
    cmt2Module.walk([&](cmt2::MethodOp m) {
      if (m.getSymName() == methodName) {
        methodOp = m;
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });

    if (methodOp) {
      // Set up arguments in valueMap (for guard evaluation too)
      auto methodArgs = methodOp.getBody().getArguments();
      for (size_t i = 0; i < std::min(args.size(), (size_t)methodArgs.size()); ++i) {
        setValue(methodArgs[i], args[i]);
      }

      // Check the guard first
      if (!evaluateGuard(methodOp.getGuard())) {
        LLVM_DEBUG(llvm::dbgs() << "Method '" << methodName << "' guard failed\n");
        result = std::nullopt;
      } else {
        // Execute the method body
        executeBody(methodOp.getBody());

        // Collect results from cmt2.return
        std::vector<InterpValue> results;
        methodOp.getBody().walk([&](cmt2::ReturnOp retOp) {
          for (Value v : retOp.getOperands()) {
            results.push_back(getValue(v));
          }
          return WalkResult::interrupt();
        });

        result = results;
      }
    } else {
      // Try as a value (read-only method)
      cmt2::ValueOp valueOp;
      cmt2Module.walk([&](cmt2::ValueOp v) {
        if (v.getSymName() == methodName) {
          valueOp = v;
          return WalkResult::interrupt();
        }
        return WalkResult::advance();
      });

      if (valueOp) {
        LLVM_DEBUG(llvm::dbgs() << "Calling value '" << methodName
                                << "' on CMT2 module '" << fullInstanceName << "'\n");

        // Set up arguments in valueMap (for guard evaluation too)
        auto valueArgs = valueOp.getBody().getArguments();
        for (size_t i = 0; i < std::min(args.size(), (size_t)valueArgs.size()); ++i) {
          setValue(valueArgs[i], args[i]);
        }

        // Check the guard first
        if (!evaluateGuard(valueOp.getGuard())) {
          LLVM_DEBUG(llvm::dbgs() << "Value '" << methodName << "' guard failed\n");
          result = std::nullopt;
        } else {
          // Execute the value body
          executeBody(valueOp.getBody());

          // Collect results from cmt2.return
          std::vector<InterpValue> results;
          valueOp.getBody().walk([&](cmt2::ReturnOp retOp) {
            for (Value v : retOp.getOperands()) {
              results.push_back(getValue(v));
            }
            return WalkResult::interrupt();
          });

          result = results;
        }
      } else {
        LLVM_DEBUG(llvm::dbgs() << "CMT2 module '" << fullInstanceName
                                << "' has no method/value '" << methodName << "'\n");
      }
    }

    // Restore saved state
    valueMap_ = std::move(savedValueMap);
    currentInstancePath_ = savedPath;

    return result;
  }

  // Fallback: Legacy register handling (also with hierarchical name)
  auto regIt = registers_.find(fullInstanceName);
  if (regIt != registers_.end()) {
    if (methodName == "read") {
      return std::vector<InterpValue>{regIt->second.value};
    } else if (methodName == "write" && !args.empty()) {
      pendingWrites_[fullInstanceName] = args[0];
      return std::vector<InterpValue>{};
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "callMethod: instance '" << fullInstanceName
                          << "' not found\n");
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Breakpoints
//===----------------------------------------------------------------------===//

unsigned Cmt2Interpreter::addBreakpointOnRuleFire(StringRef ruleName) {
  Breakpoint bp;
  bp.type = BreakpointType::RuleFire;
  bp.target = ruleName.str();
  bp.id = nextBreakpointId_++;
  breakpoints_.push_back(bp);
  return bp.id;
}

unsigned Cmt2Interpreter::addBreakpointOnRegisterWrite(StringRef registerName) {
  Breakpoint bp;
  bp.type = BreakpointType::RegisterWrite;
  bp.target = registerName.str();
  bp.id = nextBreakpointId_++;
  breakpoints_.push_back(bp);
  return bp.id;
}

unsigned Cmt2Interpreter::addBreakpointAtCycle(uint64_t cycle) {
  Breakpoint bp;
  bp.type = BreakpointType::Cycle;
  bp.cycleTarget = cycle;
  bp.id = nextBreakpointId_++;
  breakpoints_.push_back(bp);
  return bp.id;
}

bool Cmt2Interpreter::removeBreakpoint(unsigned id) {
  auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(),
                         [id](const Breakpoint &bp) { return bp.id == id; });
  if (it != breakpoints_.end()) {
    breakpoints_.erase(it);
    return true;
  }
  return false;
}

void Cmt2Interpreter::setBreakpointEnabled(unsigned id, bool enabled) {
  for (auto &bp : breakpoints_) {
    if (bp.id == id) {
      bp.enabled = enabled;
      break;
    }
  }
}

void Cmt2Interpreter::clearBreakpoints() {
  breakpoints_.clear();
}

std::optional<Breakpoint> Cmt2Interpreter::checkBreakpoints(
    const std::vector<RuleResult> &results) {
  for (const Breakpoint &bp : breakpoints_) {
    if (!bp.enabled)
      continue;

    switch (bp.type) {
    case BreakpointType::RuleFire:
      for (const auto &result : results) {
        if (result.fired && result.ruleName == bp.target) {
          return bp;
        }
      }
      break;

    case BreakpointType::RegisterWrite:
      if (pendingWrites_.count(bp.target)) {
        return bp;
      }
      break;

    case BreakpointType::Cycle:
      if (cycle_ == bp.cycleTarget) {
        return bp;
      }
      break;

    default:
      break;
    }
  }

  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Tracing
//===----------------------------------------------------------------------===//

std::optional<CycleTrace> Cmt2Interpreter::getTrace(uint64_t cycle) const {
  for (const auto &trace : traces_) {
    if (trace.cycle == cycle)
      return trace;
  }
  return std::nullopt;
}

void Cmt2Interpreter::addTraceEntry(const std::vector<RuleResult> &results) {
  CycleTrace trace;
  trace.cycle = cycle_;
  trace.ruleResults = results;

  // Record pending writes
  for (const auto &write : pendingWrites_) {
    trace.stateChanges[write.first()] = write.second;
  }

  traces_.push_back(trace);

  // Limit trace size if needed
  if (maxTraceEntries_ > 0 && traces_.size() > maxTraceEntries_) {
    traces_.erase(traces_.begin());
  }
}

//===----------------------------------------------------------------------===//
// Output
//===----------------------------------------------------------------------===//

void Cmt2Interpreter::printState() {
  os_ << "=== State at cycle " << cycle_ << " ===\n";

  // Prefer the module registry (external modules like Reg/FIFO/Mem).
  auto instanceNames = moduleRegistry_.getAllInstanceNames();
  llvm::sort(instanceNames);
  for (const auto &name : instanceNames) {
    llvm::json::Value stateVal = moduleRegistry_.getInstanceState(name);
    if (auto *obj = stateVal.getAsObject()) {
      if (auto valueStr = obj->getString("value")) {
        os_ << "  " << name << " = " << *valueStr;
        if (auto typeStr = obj->getString("type"))
          os_ << " (" << *typeStr << ")";
        os_ << "\n";
        continue;
      }
    }
    os_ << "  " << name << " = ";
    stateVal.print(os_);
    os_ << "\n";
  }

  // Legacy registers (kept for backward compatibility).
  for (const auto &entry : registers_) {
    // Avoid duplicates with registry instance names.
    if (std::find(instanceNames.begin(), instanceNames.end(),
                  entry.first().str()) != instanceNames.end())
      continue;
    os_ << "  " << entry.first() << " = ";
    entry.second.value.print(os_, false); // Print as unsigned
    os_ << "\n";
  }
}

void Cmt2Interpreter::printRuleStatus() {
  os_ << "=== Rule status at cycle " << cycle_ << " ===\n";
  auto enabledRules = evaluateGuards();

  for (const auto &ruleName : getRuleNames()) {
    bool enabled = std::find(enabledRules.begin(), enabledRules.end(),
                             ruleName) != enabledRules.end();
    os_ << "  " << ruleName << ": " << (enabled ? "ENABLED" : "disabled")
        << "\n";
  }
}

void Cmt2Interpreter::printTrace(const CycleTrace &trace) {
  os_ << "Cycle " << trace.cycle << ":\n";

  for (const auto &result : trace.ruleResults) {
    os_ << "  Rule " << result.ruleName << ": ";
    if (result.fired)
      os_ << "FIRED\n";
    else if (result.guardEnabled)
      os_ << "enabled (blocked)\n";
    else
      os_ << "disabled\n";
  }

  if (!trace.stateChanges.empty()) {
    os_ << "  State changes:\n";
    for (const auto &change : trace.stateChanges) {
      os_ << "    " << change.first() << " <- ";
      change.second.print(os_, false); // Print as unsigned
      os_ << "\n";
    }
  }
}

//===----------------------------------------------------------------------===//
// Internal Execution Helpers
//===----------------------------------------------------------------------===//

std::optional<InterpValue> Cmt2Interpreter::executeOp(Operation *op) {
  // Handle CMT2 operations
  if (auto callOp = dyn_cast<CallOp>(op)) {
    // Get instance and method names
    StringRef instanceName = callOp.getCalleeAttr().getLeafReference().getValue();
    StringRef methodName = callOp.getMethodOrValueAttr().getLeafReference().getValue();

    // Gather arguments
    std::vector<InterpValue> args;
    for (Value input : callOp.getInputs()) {
      args.push_back(getValue(input));
    }

    // Execute the call
    auto results = callMethod(instanceName, methodName, args);
    if (results && !results->empty() && callOp.getNumResults() > 0) {
      setValue(callOp.getResult(0), (*results)[0]);
      return (*results)[0];
    }
    return std::nullopt;
  }

  // Handle constant operations
  if (auto constOp = dyn_cast<hw::ConstantOp>(op)) {
    InterpValue value = constOp.getValue();
    if (op->getNumResults() > 0)
      setValue(op->getResult(0), value);
    return value;
  }

  // Handle combinational operations
  if (auto addOp = dyn_cast<comb::AddOp>(op)) {
    InterpValue lhs = getValue(addOp.getOperand(0));
    InterpValue rhs = getValue(addOp.getOperand(1));
    InterpValue result = lhs + rhs;
    setValue(addOp.getResult(), result);
    return result;
  }

  if (auto subOp = dyn_cast<comb::SubOp>(op)) {
    InterpValue lhs = getValue(subOp.getOperand(0));
    InterpValue rhs = getValue(subOp.getOperand(1));
    InterpValue result = lhs - rhs;
    setValue(subOp.getResult(), result);
    return result;
  }

  if (auto andOp = dyn_cast<comb::AndOp>(op)) {
    InterpValue result = getValue(andOp.getOperand(0));
    for (unsigned i = 1; i < andOp.getNumOperands(); ++i) {
      result &= getValue(andOp.getOperand(i));
    }
    setValue(andOp.getResult(), result);
    return result;
  }

  if (auto orOp = dyn_cast<comb::OrOp>(op)) {
    InterpValue result = getValue(orOp.getOperand(0));
    for (unsigned i = 1; i < orOp.getNumOperands(); ++i) {
      result |= getValue(orOp.getOperand(i));
    }
    setValue(orOp.getResult(), result);
    return result;
  }

  if (auto xorOp = dyn_cast<comb::XorOp>(op)) {
    InterpValue result = getValue(xorOp.getOperand(0));
    for (unsigned i = 1; i < xorOp.getNumOperands(); ++i) {
      result ^= getValue(xorOp.getOperand(i));
    }
    setValue(xorOp.getResult(), result);
    return result;
  }

  if (auto icmpOp = dyn_cast<comb::ICmpOp>(op)) {
    InterpValue lhs = getValue(icmpOp.getOperand(0));
    InterpValue rhs = getValue(icmpOp.getOperand(1));
    bool result = false;

    switch (icmpOp.getPredicate()) {
    case comb::ICmpPredicate::eq:
      result = lhs == rhs;
      break;
    case comb::ICmpPredicate::ne:
      result = lhs != rhs;
      break;
    case comb::ICmpPredicate::ult:
      result = lhs.ult(rhs);
      break;
    case comb::ICmpPredicate::ule:
      result = lhs.ule(rhs);
      break;
    case comb::ICmpPredicate::ugt:
      result = lhs.ugt(rhs);
      break;
    case comb::ICmpPredicate::uge:
      result = lhs.uge(rhs);
      break;
    case comb::ICmpPredicate::slt:
      result = lhs.slt(rhs);
      break;
    case comb::ICmpPredicate::sle:
      result = lhs.sle(rhs);
      break;
    case comb::ICmpPredicate::sgt:
      result = lhs.sgt(rhs);
      break;
    case comb::ICmpPredicate::sge:
      result = lhs.sge(rhs);
      break;
    default:
      break;
    }

    InterpValue resultVal(1, result ? 1 : 0);
    setValue(icmpOp.getResult(), resultVal);
    return resultVal;
  }

  if (auto muxOp = dyn_cast<comb::MuxOp>(op)) {
    InterpValue cond = getValue(muxOp.getCond());
    InterpValue result = cond != 0 ? getValue(muxOp.getTrueValue())
                                   : getValue(muxOp.getFalseValue());
    setValue(muxOp.getResult(), result);
    return result;
  }

  // Handle FIRRTL operations
  if (auto constOp = dyn_cast<firrtl::ConstantOp>(op)) {
    InterpValue value = constOp.getValue();
    if (op->getNumResults() > 0)
      setValue(op->getResult(0), value);
    return value;
  }

  if (auto addOp = dyn_cast<firrtl::AddPrimOp>(op)) {
    InterpValue lhs = getValue(addOp.getLhs());
    InterpValue rhs = getValue(addOp.getRhs());
    // FIRRTL add extends the width by 1, so extend the operands first
    unsigned resultWidth = std::max(lhs.getBitWidth(), rhs.getBitWidth()) + 1;
    InterpValue lhsExt = lhs.zext(resultWidth);
    InterpValue rhsExt = rhs.zext(resultWidth);
    InterpValue result = lhsExt + rhsExt;
    setValue(addOp.getResult(), result);
    return result;
  }

  if (auto subOp = dyn_cast<firrtl::SubPrimOp>(op)) {
    InterpValue lhs = getValue(subOp.getLhs());
    InterpValue rhs = getValue(subOp.getRhs());
    // FIRRTL sub extends the width by 1
    unsigned resultWidth = std::max(lhs.getBitWidth(), rhs.getBitWidth()) + 1;
    InterpValue lhsExt = lhs.zext(resultWidth);
    InterpValue rhsExt = rhs.zext(resultWidth);
    InterpValue result = lhsExt - rhsExt;
    setValue(subOp.getResult(), result);
    return result;
  }

  if (auto bitsOp = dyn_cast<firrtl::BitsPrimOp>(op)) {
    InterpValue input = getValue(bitsOp.getInput());
    unsigned hi = bitsOp.getHi();
    unsigned lo = bitsOp.getLo();
    unsigned resultWidth = hi - lo + 1;
    // Extract bits [hi:lo]
    InterpValue shifted = input.lshr(lo);
    InterpValue result = shifted.trunc(resultWidth);
    setValue(bitsOp.getResult(), result);
    return result;
  }

  if (auto shrOp = dyn_cast<firrtl::DShrPrimOp>(op)) {
    InterpValue input = getValue(shrOp.getLhs());
    InterpValue amount = getValue(shrOp.getRhs());
    InterpValue result = input.lshr(amount);
    setValue(shrOp.getResult(), result);
    return result;
  }

  if (auto shrOp = dyn_cast<firrtl::ShrPrimOp>(op)) {
    InterpValue input = getValue(shrOp.getInput());
    unsigned amount = shrOp.getAmount();
    unsigned inputWidth = input.getBitWidth();
    unsigned resultWidth = inputWidth > amount ? inputWidth - amount : 1;
    InterpValue shifted = input.lshr(amount);
    InterpValue result = shifted.trunc(resultWidth);
    setValue(shrOp.getResult(), result);
    return result;
  }

  if (auto shlOp = dyn_cast<firrtl::ShlPrimOp>(op)) {
    InterpValue input = getValue(shlOp.getInput());
    unsigned amount = shlOp.getAmount();
    unsigned resultWidth = input.getBitWidth() + amount;
    InterpValue extended = input.zext(resultWidth);
    InterpValue result = extended.shl(amount);
    setValue(shlOp.getResult(), result);
    return result;
  }

  if (auto padOp = dyn_cast<firrtl::PadPrimOp>(op)) {
    InterpValue input = getValue(padOp.getInput());
    unsigned resultWidth = padOp.getAmount();
    InterpValue result = input.zext(resultWidth);
    setValue(padOp.getResult(), result);
    return result;
  }

  if (auto andOp = dyn_cast<firrtl::AndPrimOp>(op)) {
    InterpValue lhs = getValue(andOp.getLhs());
    InterpValue rhs = getValue(andOp.getRhs());
    unsigned resultWidth = std::max(lhs.getBitWidth(), rhs.getBitWidth());
    InterpValue lhsExt = lhs.zext(resultWidth);
    InterpValue rhsExt = rhs.zext(resultWidth);
    InterpValue result = lhsExt & rhsExt;
    setValue(andOp.getResult(), result);
    return result;
  }

  if (auto orOp = dyn_cast<firrtl::OrPrimOp>(op)) {
    InterpValue lhs = getValue(orOp.getLhs());
    InterpValue rhs = getValue(orOp.getRhs());
    unsigned resultWidth = std::max(lhs.getBitWidth(), rhs.getBitWidth());
    InterpValue lhsExt = lhs.zext(resultWidth);
    InterpValue rhsExt = rhs.zext(resultWidth);
    InterpValue result = lhsExt | rhsExt;
    setValue(orOp.getResult(), result);
    return result;
  }

  if (auto xorOp = dyn_cast<firrtl::XorPrimOp>(op)) {
    InterpValue lhs = getValue(xorOp.getLhs());
    InterpValue rhs = getValue(xorOp.getRhs());
    unsigned resultWidth = std::max(lhs.getBitWidth(), rhs.getBitWidth());
    InterpValue lhsExt = lhs.zext(resultWidth);
    InterpValue rhsExt = rhs.zext(resultWidth);
    InterpValue result = lhsExt ^ rhsExt;
    setValue(xorOp.getResult(), result);
    return result;
  }

  if (auto notOp = dyn_cast<firrtl::NotPrimOp>(op)) {
    InterpValue input = getValue(notOp.getInput());
    // FIRRTL not is bitwise inversion
    InterpValue result = ~input;
    setValue(notOp.getResult(), result);
    return result;
  }

  if (auto eqOp = dyn_cast<firrtl::EQPrimOp>(op)) {
    InterpValue lhs = getValue(eqOp.getLhs());
    InterpValue rhs = getValue(eqOp.getRhs());
    InterpValue result(1, lhs == rhs ? 1 : 0);
    setValue(eqOp.getResult(), result);
    return result;
  }

  if (auto neqOp = dyn_cast<firrtl::NEQPrimOp>(op)) {
    InterpValue lhs = getValue(neqOp.getLhs());
    InterpValue rhs = getValue(neqOp.getRhs());
    InterpValue result(1, lhs != rhs ? 1 : 0);
    setValue(neqOp.getResult(), result);
    return result;
  }

  if (auto ltOp = dyn_cast<firrtl::LTPrimOp>(op)) {
    InterpValue lhs = getValue(ltOp.getLhs());
    InterpValue rhs = getValue(ltOp.getRhs());
    InterpValue result(1, lhs.ult(rhs) ? 1 : 0);
    setValue(ltOp.getResult(), result);
    return result;
  }

  if (auto leqOp = dyn_cast<firrtl::LEQPrimOp>(op)) {
    InterpValue lhs = getValue(leqOp.getLhs());
    InterpValue rhs = getValue(leqOp.getRhs());
    InterpValue result(1, lhs.ule(rhs) ? 1 : 0);
    setValue(leqOp.getResult(), result);
    return result;
  }

  if (auto gtOp = dyn_cast<firrtl::GTPrimOp>(op)) {
    InterpValue lhs = getValue(gtOp.getLhs());
    InterpValue rhs = getValue(gtOp.getRhs());
    InterpValue result(1, lhs.ugt(rhs) ? 1 : 0);
    setValue(gtOp.getResult(), result);
    return result;
  }

  if (auto geqOp = dyn_cast<firrtl::GEQPrimOp>(op)) {
    InterpValue lhs = getValue(geqOp.getLhs());
    InterpValue rhs = getValue(geqOp.getRhs());
    InterpValue result(1, lhs.uge(rhs) ? 1 : 0);
    setValue(geqOp.getResult(), result);
    return result;
  }

  if (auto muxOp = dyn_cast<firrtl::MuxPrimOp>(op)) {
    InterpValue sel = getValue(muxOp.getSel());
    InterpValue result = sel != 0 ? getValue(muxOp.getHigh())
                                  : getValue(muxOp.getLow());
    setValue(muxOp.getResult(), result);
    return result;
  }

  LLVM_DEBUG(llvm::dbgs() << "Unhandled op: " << op->getName() << "\n");
  return std::nullopt;
}

/// Helper to extract bit width from a type (handles both MLIR integer and FIRRTL types)
static unsigned getTypeWidth(Type type) {
  if (auto intType = dyn_cast<IntegerType>(type))
    return intType.getWidth();
  if (auto firrtlType = dyn_cast<firrtl::FIRRTLBaseType>(type)) {
    int32_t width = firrtlType.getBitWidthOrSentinel();
    if (width > 0)
      return static_cast<unsigned>(width);
  }
  return 32;  // Default fallback
}

InterpValue Cmt2Interpreter::getValue(Value value) {
  // Check if we have a cached value
  auto it = valueMap_.find(value);
  if (it != valueMap_.end())
    return it->second;

  // For block arguments, return a default value
  // This would need to be set up properly for method arguments
  if (auto arg = dyn_cast<BlockArgument>(value)) {
    unsigned width = getTypeWidth(value.getType());
    return APInt(width, 0);
  }

  // If the defining op is a constant, evaluate it
  if (auto *defOp = value.getDefiningOp()) {
    if (auto result = executeOp(defOp))
      return *result;
  }

  // Default
  unsigned width = getTypeWidth(value.getType());
  return APInt(width, 0);
}

void Cmt2Interpreter::setValue(Value value, const InterpValue &v) {
  valueMap_[value] = v;
}

void Cmt2Interpreter::applyStateUpdates() {
  // Commit all instances through the registry
  moduleRegistry_.commitAllInstances();

  // Apply legacy pending writes atomically
  for (const auto &write : pendingWrites_) {
    auto it = registers_.find(write.first());
    if (it != registers_.end()) {
      LLVM_DEBUG(llvm::dbgs() << "Updating " << write.first() << " = "
                              << write.second << "\n");
      it->second.value = write.second;
    }
  }
  pendingWrites_.clear();

  // Commit control flow plugins (for plugin-based execution)
  if (usePluginExecution_) {
    if (dynamicControlPlugin_)
      dynamicControlPlugin_->commit();
    if (staticControlPlugin_)
      staticControlPlugin_->commit();
    stateManager_.commitWrites();
  }
}

//===----------------------------------------------------------------------===//
// Procedural Construct Support
//===----------------------------------------------------------------------===//

void Cmt2Interpreter::initializeProcConstructs(cmt2::ModuleOp module) {
  procFSMStates_.clear();
  procExecStates_.clear();
  procSteps_.clear();
  procStaticSteps_.clear();

  // Collect proc.step definitions
  module.walk([&](ProcStepOp step) {
    procSteps_[step.getSymName()] = step;
    LLVM_DEBUG(llvm::dbgs() << "  Found proc.step: " << step.getSymName() << "\n");
  });

  // Collect proc.static_step definitions
  module.walk([&](ProcStaticStepOp step) {
    procStaticSteps_[step.getSymName()] = step;
    LLVM_DEBUG(llvm::dbgs() << "  Found proc.static_step: " << step.getSymName()
                            << " <" << step.getLatency() << ">\n");
  });

  // Collect proc.rule definitions and analyze their control regions
  module.walk([&](ProcRuleOp procRule) {
    ProcFSMState fsm;
    fsm.ruleName = procRule.getSymName().str();
    fsm.currentState = 0;  // idle
    fsm.isRunning = false;

    // Check if TDCC attributes are present
    if (procRule->hasAttr("tdcc.num_states")) {
      // Parse TDCC attributes for proper FSM control
      parseTDCCAttributes(procRule, fsm);
      LLVM_DEBUG(llvm::dbgs() << "  Found proc.rule: " << procRule.getSymName()
                              << " with TDCC FSM (" << fsm.numStates << " states)\n");
    } else {
      // Fallback: use simple heuristic (count proc.enable ops)
      unsigned stateCount = 0;
      procRule.getControl().walk([&](ProcEnableOp enable) {
        fsm.stateSteps.push_back(enable.getStepName().str());
        stateCount++;
      });
      fsm.numStates = stateCount + 1;  // +1 for idle state
      LLVM_DEBUG(llvm::dbgs() << "  Found proc.rule: " << procRule.getSymName()
                              << " with " << stateCount << " steps (no TDCC)\n");
    }

    procFSMStates_[procRule.getSymName()] = fsm;

    // Also initialize ProcRuleExecState for direct interpretation
    ProcRuleExecState execState;
    execState.ruleName = procRule.getSymName().str();
    execState.isRunning = false;
    execState.ruleOp = procRule;
    procExecStates_[procRule.getSymName()] = std::move(execState);
  });
}

bool Cmt2Interpreter::executeProcStep(StringRef stepName) {
  // Try to find in proc.step
  auto stepIt = procSteps_.find(stepName);
  if (stepIt != procSteps_.end()) {
    ProcStepOp step = stepIt->second;

    // Execute the step body
    Region &body = step.getBody();
    if (!body.empty()) {
      Block &block = body.front();
      valueMap_.clear();

      bool stepDone = false;
      for (Operation &op : block) {
        if (auto doneOp = dyn_cast<ProcStepDoneOp>(op)) {
          // Evaluate the done condition
          InterpValue doneVal = getValue(doneOp.getDone());
          stepDone = doneVal != 0;
          break;
        }
        executeOp(&op);
      }

      if (stepDone) {
        stepsDoneThisCycle_.insert(stepName);
      }
      return stepDone;
    }
    return true;  // Empty body means done
  }

  // Try to find in proc.static_step
  auto staticIt = procStaticSteps_.find(stepName);
  if (staticIt != procStaticSteps_.end()) {
    ProcStaticStepOp step = staticIt->second;

    // Static steps have a fixed latency
    // For now, we'll just execute the body and assume done after one cycle
    // TODO: Track static step latency properly
    Region &body = step.getBody();
    if (!body.empty()) {
      Block &block = body.front();
      valueMap_.clear();

      for (Operation &op : block) {
        if (isa<ProcStepDoneOp>(op))
          break;
        executeOp(&op);
      }
    }

    stepsDoneThisCycle_.insert(stepName);
    return true;  // Static steps always complete (simplified)
  }

  LLVM_DEBUG(llvm::dbgs() << "Step not found: " << stepName << "\n");
  return false;
}

bool Cmt2Interpreter::isProcRuleEnabled(ProcRuleOp procRule) {
  // Check if already running (using direct or legacy execution)
  if (useDirectProcInterpretation_) {
    auto execIt = procExecStates_.find(procRule.getSymName());
    if (execIt != procExecStates_.end() && execIt->second.isRunning) {
      return false;  // Already running, can't start again
    }
  } else {
    auto fsmIt = procFSMStates_.find(procRule.getSymName());
    if (fsmIt != procFSMStates_.end() && fsmIt->second.isRunning) {
      return false;  // Already running, can't start again
    }
  }

  // Evaluate the guard
  return evaluateGuard(procRule.getGuard());
}

void Cmt2Interpreter::executeProcRuleStep(ProcRuleOp procRule, ProcFSMState &fsm) {
  // Use direct interpretation if enabled (preferred path)
  if (useDirectProcInterpretation_) {
    StringRef ruleName = procRule.getSymName();
    auto it = procExecStates_.find(ruleName);
    if (it != procExecStates_.end()) {
      executeProcRuleDirect(procRule, it->second);
      // Sync running state to legacy FSM for compatibility
      fsm.isRunning = it->second.isRunning;
      return;
    }
  }

  // Use TDCC FSM execution if available
  if (fsm.hasTDCC) {
    executeProcRuleTDCC(procRule, fsm);
    return;
  }

  // Legacy simple FSM execution
  if (fsm.currentState == 0) {
    // Starting from idle - begin execution
    if (!fsm.stateSteps.empty()) {
      fsm.isRunning = true;
      fsm.currentState = 1;

      // Execute first step
      StringRef stepName = fsm.stateSteps[0];
      executeProcStep(stepName);
    }
  } else if (fsm.currentState <= fsm.stateSteps.size()) {
    // In the middle of execution
    StringRef currentStep = fsm.stateSteps[fsm.currentState - 1];

    // Check if current step is done
    if (stepsDoneThisCycle_.count(currentStep)) {
      // Move to next state
      fsm.currentState++;

      if (fsm.currentState > fsm.stateSteps.size()) {
        // All steps done - return to idle
        fsm.currentState = 0;
        fsm.isRunning = false;
      } else {
        // Execute next step
        StringRef nextStep = fsm.stateSteps[fsm.currentState - 1];
        executeProcStep(nextStep);
      }
    } else {
      // Re-execute current step
      executeProcStep(currentStep);
    }
  }
}

//===----------------------------------------------------------------------===//
// TDCC FSM Support for Procedural Rules
//===----------------------------------------------------------------------===//

void Cmt2Interpreter::parseTDCCAttributes(ProcRuleOp procRule, ProcFSMState &fsm) {
  fsm.hasTDCC = true;

  // Parse num_states
  if (auto numStatesAttr = procRule->getAttrOfType<IntegerAttr>("tdcc.num_states")) {
    fsm.numStates = numStatesAttr.getInt();
  }

  // Parse done_state
  if (auto doneStateAttr = procRule->getAttrOfType<IntegerAttr>("tdcc.done_state")) {
    fsm.doneState = doneStateAttr.getInt();
  }

  // Parse transitions
  if (auto transitionsAttr = procRule->getAttrOfType<ArrayAttr>("tdcc.transitions")) {
    for (auto transAttr : transitionsAttr) {
      auto dict = cast<DictionaryAttr>(transAttr);
      TDCCTransition trans;
      trans.fromState = dict.getAs<IntegerAttr>("from").getInt();
      trans.toState = dict.getAs<IntegerAttr>("to").getInt();

      if (auto guardIdAttr = dict.getAs<IntegerAttr>("guard_op_id")) {
        trans.guardOpId = guardIdAttr.getInt();
      }
      if (auto guardInvAttr = dict.getAs<BoolAttr>("guard_inverted")) {
        trans.guardInverted = guardInvAttr.getValue();
      }
      if (auto doneStepAttr = dict.getAs<StringAttr>("done_step")) {
        trans.doneStep = doneStepAttr.getValue().str();
      }
      if (auto parJoinAttr = dict.getAs<ArrayAttr>("par_join_branches")) {
        for (auto branchAttr : parJoinAttr) {
          trans.parJoinBranches.push_back(cast<StringAttr>(branchAttr).getValue().str());
        }
      }

      fsm.transitions.push_back(trans);
    }
  }

  // Parse enables
  if (auto enablesAttr = procRule->getAttrOfType<ArrayAttr>("tdcc.enables")) {
    for (auto enableAttr : enablesAttr) {
      auto dict = cast<DictionaryAttr>(enableAttr);
      TDCCStepEnable enable;
      enable.state = dict.getAs<IntegerAttr>("state").getInt();

      // Step can be FlatSymbolRefAttr (e.g., @push_static) or StringAttr
      if (auto stepSymRef = dict.getAs<FlatSymbolRefAttr>("step")) {
        enable.stepName = stepSymRef.getValue().str();
      } else if (auto stepStr = dict.getAs<StringAttr>("step")) {
        enable.stepName = stepStr.getValue().str();
        // Remove leading '@' from step name if present
        if (!enable.stepName.empty() && enable.stepName[0] == '@') {
          enable.stepName = enable.stepName.substr(1);
        }
      }

      if (auto iterAttr = dict.getAs<IntegerAttr>("iteration")) {
        enable.iteration = iterAttr.getInt();
      }
      fsm.enables.push_back(enable);

      // Build state-to-step map
      fsm.stateToStep[enable.state] = enable.stepName;
    }
  }

  // Parse par_blocks
  if (auto parBlocksAttr = procRule->getAttrOfType<ArrayAttr>("tdcc.par_blocks")) {
    for (auto blockAttr : parBlocksAttr) {
      auto dict = cast<DictionaryAttr>(blockAttr);
      TDCCParBlock parBlock;
      parBlock.forkState = dict.getAs<IntegerAttr>("fork_state").getInt();
      parBlock.joinState = dict.getAs<IntegerAttr>("join_state").getInt();
      if (auto needsFsmAttr = dict.getAs<BoolAttr>("needs_per_branch_fsm")) {
        parBlock.needsPerBranchFsm = needsFsmAttr.getValue();
      }

      if (auto branchesAttr = dict.getAs<ArrayAttr>("branches")) {
        for (auto branchAttr : branchesAttr) {
          auto brDict = cast<DictionaryAttr>(branchAttr);
          TDCCParBranch branch;
          branch.name = brDict.getAs<StringAttr>("name").getValue().str();
          branch.firstState = brDict.getAs<IntegerAttr>("first_state").getInt();
          branch.lastState = brDict.getAs<IntegerAttr>("last_state").getInt();
          branch.exitState = brDict.getAs<IntegerAttr>("exit_state").getInt();
          if (auto needsFsmAttr = brDict.getAs<BoolAttr>("needs_separate_fsm")) {
            branch.needsSeparateFsm = needsFsmAttr.getValue();
          }
          parBlock.branches.push_back(branch);

          // Initialize branch FSM state
          fsm.branchFSMStates[branch.name] = 0;  // Start idle
        }
      }
      fsm.parBlocks.push_back(parBlock);
    }
  }

  // Parse cond_ops
  if (auto condOpsAttr = procRule->getAttrOfType<ArrayAttr>("tdcc.cond_ops")) {
    for (auto condAttr : condOpsAttr) {
      auto dict = cast<DictionaryAttr>(condAttr);
      TDCCCondOp condOp;
      condOp.id = dict.getAs<IntegerAttr>("id").getInt();
      condOp.type = dict.getAs<StringAttr>("type").getValue().str();
      fsm.condOps.push_back(condOp);
    }
  }

  // Build state-to-transitions map (using indices to avoid pointer invalidation)
  for (size_t i = 0; i < fsm.transitions.size(); ++i) {
    fsm.stateTransitions[fsm.transitions[i].fromState].push_back(i);
  }

  LLVM_DEBUG({
    llvm::dbgs() << "    TDCC FSM: " << fsm.numStates << " states, done=" << fsm.doneState << "\n";
    llvm::dbgs() << "    Enables: " << fsm.enables.size() << "\n";
    llvm::dbgs() << "    Transitions: " << fsm.transitions.size() << "\n";
    llvm::dbgs() << "    Par blocks: " << fsm.parBlocks.size() << "\n";
    llvm::dbgs() << "    Cond ops: " << fsm.condOps.size() << "\n";
  });
}

void Cmt2Interpreter::executeProcRuleTDCC(ProcRuleOp procRule, ProcFSMState &fsm) {
  LLVM_DEBUG(llvm::dbgs() << "  TDCC execution: state=" << fsm.currentState
                          << " running=" << fsm.isRunning << "\n");

  // State 0 = idle - transition to state 1 to start
  if (fsm.currentState == 0) {
    fsm.isRunning = true;
    fsm.currentState = 1;

    // Initialize branch FSMs if we have par blocks
    for (auto &parBlock : fsm.parBlocks) {
      for (auto &branch : parBlock.branches) {
        fsm.branchFSMStates[branch.name] = 0;  // 0 = not started
      }
    }
    os_ << "    [TDCC] Started: transitioning to state 1\n";
    return;  // Will execute step on next call
  }

  // Check for done state
  if (fsm.currentState == fsm.doneState) {
    fsm.currentState = 0;
    fsm.isRunning = false;
    os_ << "    [TDCC] Completed: returning to idle\n";
    return;
  }

  // Check if we're at a fork state (need to initialize branches)
  for (auto &parBlock : fsm.parBlocks) {
    if (fsm.currentState == parBlock.forkState) {
      // Check if branches are already initialized (non-zero)
      bool alreadyInitialized = false;
      for (auto &branch : parBlock.branches) {
        auto it = fsm.branchFSMStates.find(branch.name);
        if (it != fsm.branchFSMStates.end() && it->second != 0) {
          alreadyInitialized = true;
          break;
        }
      }

      if (!alreadyInitialized) {
        // Initialize branch FSMs to their first states
        for (auto &branch : parBlock.branches) {
          fsm.branchFSMStates[branch.name] = branch.firstState;
          os_ << "    [TDCC] Fork: branch " << branch.name << " -> state " << branch.firstState << "\n";
        }
      }
    }
  }

  // Check if we're in a parallel region (between fork and join)
  for (auto &parBlock : fsm.parBlocks) {
    // Check if any branch is active in this parallel block
    bool hasActiveBranch = false;
    for (auto &branch : parBlock.branches) {
      auto it = fsm.branchFSMStates.find(branch.name);
      if (it != fsm.branchFSMStates.end() && it->second != 0 &&
          it->second != parBlock.joinState) {
        hasActiveBranch = true;
        break;
      }
    }

    if (!hasActiveBranch)
      continue;

    // Execute all active branches in parallel
    bool allBranchesDone = true;
    for (auto &branch : parBlock.branches) {
      auto branchStateIt = fsm.branchFSMStates.find(branch.name);
      if (branchStateIt == fsm.branchFSMStates.end())
        continue;

      unsigned branchState = branchStateIt->second;

      // Skip if branch not started yet
      if (branchState == 0) {
        allBranchesDone = false;
        continue;
      }
      // Branch is done only when it reaches the join state
      if (branchState == parBlock.joinState) {
        continue;  // Branch is done
      }

      os_ << "    [TDCC] Branch " << branch.name << " at state " << branchState << "\n";

      // Execute step for branch's current state
      auto stepIt = fsm.stateToStep.find(branchState);
      if (stepIt != fsm.stateToStep.end()) {
        StringRef stepName = stepIt->second;
        os_ << "    [TDCC]   Executing step: " << stepName << "\n";
        executeProcStep(stepName);
      }

      // Find next state for this branch
      auto transIt = fsm.stateTransitions.find(branchState);
      if (transIt != fsm.stateTransitions.end()) {
        for (size_t transIdx : transIt->second) {
          const TDCCTransition &trans = fsm.transitions[transIdx];
          // Only consider transitions within this branch's range
          if (trans.toState < branch.firstState ||
              (trans.toState > branch.lastState && trans.toState != branch.exitState &&
               trans.toState != parBlock.joinState))
            continue;

          bool canTake = true;

          // Check done signal guard
          if (!trans.doneStep.empty()) {
            if (!stepsDoneThisCycle_.count(trans.doneStep)) {
              canTake = false;
            }
          }

          // Check while condition guard
          if (trans.guardOpId >= 0) {
            bool condValue = evaluateTDCCWhileCondition(procRule, trans.guardOpId);
            if (trans.guardInverted) {
              condValue = !condValue;
            }
            if (!condValue) {
              canTake = false;
            }
          }

          if (canTake) {
            fsm.branchFSMStates[branch.name] = trans.toState;
            os_ << "    [TDCC]   Branch " << branch.name << " -> state " << trans.toState << "\n";

            // Check if branch reached join state
            if (trans.toState == parBlock.joinState) {
              os_ << "    [TDCC]   Branch " << branch.name << " completed (reached join)\n";
            }
            break;
          }
        }
      }

      // Check if this branch is done now (reached join state)
      unsigned newBranchState = fsm.branchFSMStates[branch.name];
      if (newBranchState != parBlock.joinState) {
        allBranchesDone = false;
      }
    }

    // If all branches done, move to join state
    if (allBranchesDone) {
      os_ << "    [TDCC] All branches completed, moving to join state " << parBlock.joinState << "\n";
      fsm.currentState = parBlock.joinState;

      // Now check transition from join state
      uint64_t nextState = getNextTDCCState(procRule, fsm);
      if (nextState != fsm.currentState) {
        os_ << "    [TDCC] Post-join: state " << fsm.currentState << " -> " << nextState << "\n";
        fsm.currentState = nextState;
      }
    }
    return;  // Parallel region handled
  }

  // Non-parallel execution: single state machine
  // Execute step for current state if any
  auto stepIt = fsm.stateToStep.find(fsm.currentState);
  if (stepIt != fsm.stateToStep.end()) {
    StringRef stepName = stepIt->second;
    os_ << "    [TDCC] Executing step: " << stepName << " (state " << fsm.currentState << ")\n";
    executeProcStep(stepName);
  }

  // Determine next state based on transitions
  uint64_t nextState = getNextTDCCState(procRule, fsm);
  if (nextState != fsm.currentState) {
    os_ << "    [TDCC] State " << fsm.currentState << " -> " << nextState << "\n";
  }

  fsm.currentState = nextState;
}

bool Cmt2Interpreter::evaluateTDCCWhileCondition(ProcRuleOp procRule, int64_t condOpId) {
  // Find the while condition with the given ID
  // The conditions are stored in proc.while operations in the control region
  bool condValue = false;
  int64_t currentCondId = 0;

  procRule.getControl().walk([&](ProcWhileOp whileOp) {
    if (currentCondId == condOpId) {
      // Evaluate the condition region
      Region &condRegion = whileOp.getCondRegion();
      if (!condRegion.empty()) {
        Block &condBlock = condRegion.front();
        valueMap_.clear();

        for (Operation &op : condBlock) {
          if (auto whileCond = dyn_cast<ProcWhileCondYieldOp>(op)) {
            InterpValue val = getValue(whileCond.getCond());
            condValue = val != 0;
            break;
          }
          executeOp(&op);
        }
      }
    }
    currentCondId++;
  });

  LLVM_DEBUG(llvm::dbgs() << "    While cond " << condOpId << " = " << condValue << "\n");
  return condValue;
}

bool Cmt2Interpreter::areAllBranchesDone(const ProcFSMState &fsm, const TDCCParBlock &parBlock) {
  for (const auto &branch : parBlock.branches) {
    auto it = fsm.branchFSMStates.find(branch.name);
    if (it == fsm.branchFSMStates.end() || it->second != 3) {  // 3 = done state
      return false;
    }
  }
  return true;
}

uint64_t Cmt2Interpreter::getNextTDCCState(ProcRuleOp procRule, ProcFSMState &fsm) {
  uint64_t currentState = fsm.currentState;

  // Check for parallel join first - if at join state, need all branches done
  for (auto &parBlock : fsm.parBlocks) {
    if (currentState == parBlock.joinState) {
      if (!areAllBranchesDone(fsm, parBlock)) {
        return currentState;  // Stay at join state until all branches done
      }
    }
  }

  // Find transitions from current state
  auto transIt = fsm.stateTransitions.find(currentState);
  if (transIt == fsm.stateTransitions.end()) {
    return currentState;  // No transitions, stay in current state
  }

  // Evaluate each outgoing transition
  for (size_t transIdx : transIt->second) {
    const TDCCTransition &trans = fsm.transitions[transIdx];
    bool canTake = true;

    // Check done signal guard
    if (!trans.doneStep.empty()) {
      if (!stepsDoneThisCycle_.count(trans.doneStep)) {
        canTake = false;  // Step not done yet
      }
    }

    // Check while condition guard
    if (trans.guardOpId >= 0) {
      bool condValue = evaluateTDCCWhileCondition(procRule, trans.guardOpId);
      if (trans.guardInverted) {
        condValue = !condValue;  // Invert for else/exit branches
      }
      if (!condValue) {
        canTake = false;
      }
    }

    // Check parallel join guard
    if (!trans.parJoinBranches.empty()) {
      for (const auto &branchName : trans.parJoinBranches) {
        auto it = fsm.branchFSMStates.find(branchName);
        if (it == fsm.branchFSMStates.end() || it->second != 3) {  // 3 = done
          canTake = false;
          break;
        }
      }
    }

    if (canTake) {
      // Update branch FSM states if transitioning within a branch
      for (auto &parBlock : fsm.parBlocks) {
        for (auto &branch : parBlock.branches) {
          if (trans.fromState >= branch.firstState && trans.fromState <= branch.lastState) {
            // This transition is within a branch
            if (trans.toState == branch.exitState || trans.toState > branch.lastState) {
              // Transition to exit state - mark branch as done
              fsm.branchFSMStates[branch.name] = 3;  // done
              LLVM_DEBUG(llvm::dbgs() << "    Branch " << branch.name << " completed\n");
            }
          }
        }
      }

      return trans.toState;
    }
  }

  return currentState;  // No transition taken, stay in current state
}

//===----------------------------------------------------------------------===//
// Direct Proc Interpretation (New Architecture)
//===----------------------------------------------------------------------===//

std::unique_ptr<ProcExecState> ProcExecState::createFor(Operation *op) {
  auto state = std::make_unique<ProcExecState>();
  state->op = op;
  state->status = ProcExecStatus::Idle;

  if (isa<ProcSeqOp>(op)) {
    state->state = SeqExecState{};
  } else if (isa<ProcParOp>(op)) {
    state->state = ParExecState{};
  } else if (isa<ProcWhileOp>(op)) {
    state->state = WhileExecState{};
  } else if (auto repeatOp = dyn_cast<ProcStaticRepeatOp>(op)) {
    StaticRepeatExecState repeatState;
    repeatState.totalIterations = repeatOp.getCount();
    state->state = repeatState;
  } else if (auto enableOp = dyn_cast<ProcEnableOp>(op)) {
    EnableExecState enableState;
    enableState.stepName = enableOp.getStepName().str();
    state->state = enableState;
  } else if (isa<ProcIfOp>(op)) {
    // If uses same state as Seq (single child at a time)
    state->state = SeqExecState{};
  }

  return state;
}

void Cmt2Interpreter::executeProcRuleDirect(ProcRuleOp procRule,
                                            ProcRuleExecState &execState) {
  LLVM_DEBUG(llvm::dbgs() << "  Direct interpretation: running="
                          << execState.isRunning << "\n");

  // If idle, initialize control state for the control region
  if (!execState.isRunning) {
    execState.isRunning = true;
    Region &controlRegion = procRule.getControl();
    if (!controlRegion.empty()) {
      Block &controlBlock = controlRegion.front();
      // The control region should have a single top-level control construct
      for (Operation &op : controlBlock) {
        if (isa<ProcSeqOp, ProcParOp, ProcWhileOp, ProcStaticRepeatOp,
                ProcEnableOp, ProcIfOp>(&op)) {
          execState.controlState = ProcExecState::createFor(&op);
          os_ << "    [Direct] Started: initializing control state\n";
          break;
        }
      }
    }
    if (!execState.controlState) {
      // No control region or empty - immediately done
      execState.isRunning = false;
      os_ << "    [Direct] No control region, immediately done\n";
      return;
    }
  }

  // Advance the control state by one cycle
  if (execState.controlState) {
    advanceState(*execState.controlState);

    // Check for completion
    if (execState.controlState->status == ProcExecStatus::Done) {
      execState.isRunning = false;
      execState.controlState.reset();
      os_ << "    [Direct] Control completed, returning to idle\n";
    }
  }
}

void Cmt2Interpreter::advanceState(ProcExecState &state) {
  if (!state.op)
    return;

  if (auto seqOp = dyn_cast<ProcSeqOp>(state.op)) {
    advanceSeq(seqOp, state);
  } else if (auto parOp = dyn_cast<ProcParOp>(state.op)) {
    advancePar(parOp, state);
  } else if (auto whileOp = dyn_cast<ProcWhileOp>(state.op)) {
    advanceWhile(whileOp, state);
  } else if (auto repeatOp = dyn_cast<ProcStaticRepeatOp>(state.op)) {
    advanceStaticRepeat(repeatOp, state);
  } else if (auto enableOp = dyn_cast<ProcEnableOp>(state.op)) {
    advanceEnable(enableOp, state);
  } else if (auto ifOp = dyn_cast<ProcIfOp>(state.op)) {
    advanceIf(ifOp, state);
  }
}

void Cmt2Interpreter::advanceSeq(ProcSeqOp seqOp, ProcExecState &state) {
  auto *seqState = state.getState<SeqExecState>();
  if (!seqState)
    return;

  Block &seqBlock = seqOp.getBody().front();
  SmallVector<Operation *, 8> children;
  for (Operation &op : seqBlock) {
    if (isa<ProcSeqOp, ProcParOp, ProcWhileOp, ProcStaticRepeatOp,
            ProcEnableOp, ProcIfOp>(&op)) {
      children.push_back(&op);
    }
  }

  if (children.empty()) {
    state.status = ProcExecStatus::Done;
    return;
  }

  // Initialize on first call
  if (state.status == ProcExecStatus::Idle) {
    state.status = ProcExecStatus::Running;
    seqState->currentChildIndex = 0;
    state.children.clear();
    state.children.push_back(ProcExecState::createFor(children[0]));
    os_ << "    [Direct] Seq: starting child 0\n";
  }

  if (state.status == ProcExecStatus::Running) {
    if (state.children.empty() || !state.children[0])
      return;

    // Advance current child
    advanceState(*state.children[0]);

    // Check if current child is done
    if (state.children[0]->status == ProcExecStatus::Done) {
      seqState->currentChildIndex++;
      os_ << "    [Direct] Seq: child " << (seqState->currentChildIndex - 1)
          << " done\n";

      if (seqState->currentChildIndex >= children.size()) {
        // All children done
        state.status = ProcExecStatus::Done;
        os_ << "    [Direct] Seq: all children done\n";
      } else {
        // Move to next child
        state.children[0] = ProcExecState::createFor(
            children[seqState->currentChildIndex]);
        os_ << "    [Direct] Seq: starting child "
            << seqState->currentChildIndex << "\n";
      }
    }
  }
}

void Cmt2Interpreter::advancePar(ProcParOp parOp, ProcExecState &state) {
  auto *parState = state.getState<ParExecState>();
  if (!parState)
    return;

  Block &parBlock = parOp.getBody().front();
  SmallVector<Operation *, 8> children;
  for (Operation &op : parBlock) {
    if (isa<ProcSeqOp, ProcParOp, ProcWhileOp, ProcStaticRepeatOp,
            ProcEnableOp, ProcIfOp>(&op)) {
      children.push_back(&op);
    }
  }

  if (children.empty()) {
    state.status = ProcExecStatus::Done;
    return;
  }

  // Initialize on first call - start all children
  if (state.status == ProcExecStatus::Idle) {
    state.status = ProcExecStatus::Running;
    parState->childDone.resize(children.size(), false);
    parState->allStarted = true;
    state.children.clear();
    for (size_t i = 0; i < children.size(); ++i) {
      state.children.push_back(ProcExecState::createFor(children[i]));
    }
    os_ << "    [Direct] Par: starting " << children.size()
        << " parallel branches\n";
  }

  if (state.status == ProcExecStatus::Running) {
    bool allDone = true;

    // Advance all non-done children
    for (size_t i = 0; i < state.children.size(); ++i) {
      if (parState->childDone[i])
        continue;

      if (!state.children[i])
        continue;

      advanceState(*state.children[i]);

      if (state.children[i]->status == ProcExecStatus::Done) {
        parState->childDone[i] = true;
        os_ << "    [Direct] Par: branch " << i << " done\n";
      } else {
        allDone = false;
      }
    }

    if (allDone) {
      state.status = ProcExecStatus::Done;
      os_ << "    [Direct] Par: all branches done (fork-join complete)\n";
    }
  }
}

void Cmt2Interpreter::advanceWhile(ProcWhileOp whileOp, ProcExecState &state) {
  auto *whileState = state.getState<WhileExecState>();
  if (!whileState)
    return;

  // Initialize on first call
  if (state.status == ProcExecStatus::Idle) {
    state.status = ProcExecStatus::Running;
    whileState->evaluatingCond = true;
    state.children.clear();
    os_ << "    [Direct] While: starting condition evaluation\n";
  }

  if (state.status == ProcExecStatus::Running) {
    if (whileState->evaluatingCond) {
      // Evaluate condition region
      Region &condRegion = whileOp.getCondRegion();
      bool condValue = false;

      if (!condRegion.empty()) {
        Block &condBlock = condRegion.front();
        valueMap_.clear();

        for (Operation &op : condBlock) {
          if (auto condYield = dyn_cast<ProcWhileCondYieldOp>(op)) {
            InterpValue val = getValue(condYield.getCond());
            condValue = val != 0;
            break;
          }
          executeOp(&op);
        }
      }

      os_ << "    [Direct] While: condition = " << condValue << "\n";

      if (!condValue) {
        // Condition false - exit loop
        state.status = ProcExecStatus::Done;
        os_ << "    [Direct] While: exiting (condition false)\n";
      } else {
        // Condition true - start body
        whileState->evaluatingCond = false;
        Region &bodyRegion = whileOp.getBody();
        if (!bodyRegion.empty()) {
          Block &bodyBlock = bodyRegion.front();
          for (Operation &op : bodyBlock) {
            if (isa<ProcSeqOp, ProcParOp, ProcWhileOp, ProcStaticRepeatOp,
                    ProcEnableOp, ProcIfOp>(&op)) {
              state.children.clear();
              state.children.push_back(ProcExecState::createFor(&op));
              os_ << "    [Direct] While: starting body\n";
              break;
            }
          }
        }
      }
    } else {
      // Body is active - advance it
      if (!state.children.empty() && state.children[0]) {
        advanceState(*state.children[0]);

        if (state.children[0]->status == ProcExecStatus::Done) {
          // Body complete - re-evaluate condition next cycle
          whileState->evaluatingCond = true;
          state.children.clear();
          os_ << "    [Direct] While: body done, re-evaluating condition\n";
        }
      } else {
        // No body - re-evaluate condition
        whileState->evaluatingCond = true;
      }
    }
  }
}

void Cmt2Interpreter::advanceStaticRepeat(ProcStaticRepeatOp repeatOp,
                                          ProcExecState &state) {
  auto *repeatState = state.getState<StaticRepeatExecState>();
  if (!repeatState)
    return;

  // Initialize on first call
  if (state.status == ProcExecStatus::Idle) {
    state.status = ProcExecStatus::Running;
    repeatState->currentIteration = 0;
    repeatState->totalIterations = repeatOp.getCount();

    if (repeatState->totalIterations == 0) {
      state.status = ProcExecStatus::Done;
      os_ << "    [Direct] StaticRepeat: 0 iterations, done\n";
      return;
    }

    // Initialize first iteration's body
    Region &bodyRegion = repeatOp.getBody();
    if (!bodyRegion.empty()) {
      Block &bodyBlock = bodyRegion.front();
      for (Operation &op : bodyBlock) {
        if (isa<ProcSeqOp, ProcParOp, ProcWhileOp, ProcStaticRepeatOp,
                ProcEnableOp, ProcIfOp>(&op)) {
          state.children.clear();
          state.children.push_back(ProcExecState::createFor(&op));
          os_ << "    [Direct] StaticRepeat: starting iteration 0/"
              << repeatState->totalIterations << "\n";
          break;
        }
      }
    }
  }

  if (state.status == ProcExecStatus::Running) {
    // Advance body
    if (!state.children.empty() && state.children[0]) {
      advanceState(*state.children[0]);

      if (state.children[0]->status == ProcExecStatus::Done) {
        repeatState->currentIteration++;
        os_ << "    [Direct] StaticRepeat: iteration "
            << (repeatState->currentIteration - 1) << " done\n";

        if (repeatState->currentIteration >= repeatState->totalIterations) {
          // All iterations done
          state.status = ProcExecStatus::Done;
          os_ << "    [Direct] StaticRepeat: all iterations done\n";
        } else {
          // Start next iteration
          Region &bodyRegion = repeatOp.getBody();
          if (!bodyRegion.empty()) {
            Block &bodyBlock = bodyRegion.front();
            for (Operation &op : bodyBlock) {
              if (isa<ProcSeqOp, ProcParOp, ProcWhileOp, ProcStaticRepeatOp,
                      ProcEnableOp, ProcIfOp>(&op)) {
                state.children[0] = ProcExecState::createFor(&op);
                os_ << "    [Direct] StaticRepeat: starting iteration "
                    << repeatState->currentIteration << "/"
                    << repeatState->totalIterations << "\n";
                break;
              }
            }
          }
        }
      }
    } else {
      // No body - all iterations trivially done
      state.status = ProcExecStatus::Done;
    }
  }
}

void Cmt2Interpreter::advanceEnable(ProcEnableOp enableOp,
                                    ProcExecState &state) {
  auto *enableState = state.getState<EnableExecState>();
  if (!enableState)
    return;

  StringRef stepName = enableOp.getStepName();

  // Initialize on first call
  if (state.status == ProcExecStatus::Idle) {
    state.status = ProcExecStatus::Running;
    enableState->stepName = stepName.str();
    enableState->activated = false;
    enableState->cycleCount = 0;

    // Determine if this is a static or dynamic step
    auto staticStepIt = procStaticSteps_.find(stepName);
    if (staticStepIt != procStaticSteps_.end()) {
      enableState->isStatic = true;
      enableState->staticLatency = staticStepIt->second.getLatency();
      os_ << "    [Direct] Enable: activating static step '" << stepName
          << "' (latency=" << enableState->staticLatency << ")\n";
    } else {
      enableState->isStatic = false;
      os_ << "    [Direct] Enable: activating dynamic step '" << stepName
          << "'\n";
    }
  }

  if (state.status == ProcExecStatus::Running) {
    // Activate the step (execute its body)
    if (!enableState->activated) {
      activateStep(stepName);
      enableState->activated = true;
    }

    // Check for completion
    if (enableState->isStatic) {
      // Static step: done after latency cycles
      enableState->cycleCount++;
      if (enableState->cycleCount >= enableState->staticLatency) {
        state.status = ProcExecStatus::Done;
        os_ << "    [Direct] Enable: static step '" << stepName
            << "' done (after " << enableState->cycleCount << " cycles)\n";
      }
    } else {
      // Dynamic step: check done signal
      if (isStepDone(stepName)) {
        state.status = ProcExecStatus::Done;
        os_ << "    [Direct] Enable: dynamic step '" << stepName
            << "' signaled done\n";
      }
    }
  }
}

void Cmt2Interpreter::advanceIf(ProcIfOp ifOp, ProcExecState &state) {
  // For now, treat if similar to seq - evaluate condition and execute
  // appropriate branch
  auto *seqState = state.getState<SeqExecState>();
  if (!seqState)
    return;

  // Initialize on first call - evaluate condition
  if (state.status == ProcExecStatus::Idle) {
    state.status = ProcExecStatus::Running;

    // Evaluate the if condition - ProcIfOp takes condition as a value
    InterpValue condVal = getValue(ifOp.getCond());
    bool condValue = condVal != 0;

    os_ << "    [Direct] If: condition = " << condValue << "\n";

    // Select appropriate branch
    Region &thenRegion = ifOp.getThenRegion();
    Region &elseRegion = ifOp.getElseRegion();
    Region *selectedRegion = condValue ? &thenRegion : &elseRegion;

    if (!selectedRegion->empty()) {
      Block &block = selectedRegion->front();
      for (Operation &op : block) {
        if (isa<ProcSeqOp, ProcParOp, ProcWhileOp, ProcStaticRepeatOp,
                ProcEnableOp, ProcIfOp>(&op)) {
          state.children.clear();
          state.children.push_back(ProcExecState::createFor(&op));
          os_ << "    [Direct] If: executing "
              << (condValue ? "then" : "else") << " branch\n";
          break;
        }
      }
    }

    if (state.children.empty()) {
      // No branch to execute
      state.status = ProcExecStatus::Done;
    }
  }

  if (state.status == ProcExecStatus::Running) {
    if (!state.children.empty() && state.children[0]) {
      advanceState(*state.children[0]);

      if (state.children[0]->status == ProcExecStatus::Done) {
        state.status = ProcExecStatus::Done;
        os_ << "    [Direct] If: branch done\n";
      }
    } else {
      state.status = ProcExecStatus::Done;
    }
  }
}

bool Cmt2Interpreter::isStepDone(StringRef stepName) {
  // Check if the step signaled done this cycle
  return stepsDoneThisCycle_.count(stepName) > 0;
}

void Cmt2Interpreter::activateStep(StringRef stepName) {
  // Execute the step's body
  executeProcStep(stepName);
}

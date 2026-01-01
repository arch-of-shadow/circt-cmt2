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
#include "circt/Dialect/HW/HWOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-interpreter"

using namespace circt;
using namespace cmt2;
using namespace mlir;

//===----------------------------------------------------------------------===//
// Constructor and Destructor
//===----------------------------------------------------------------------===//

Cmt2Interpreter::Cmt2Interpreter(mlir::ModuleOp module, llvm::raw_ostream &os)
    : module_(module), os_(os) {}

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

  os_ << "Initialized interpreter\n";
  os_ << "  Top module: " << topModule_.getModuleName() << "\n";
  os_ << "  Registers: " << registers_.size() << "\n";
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

  // Walk through instances to find registers
  module.walk([&](InstanceOp instance) {
    StringRef instanceName = instance.getInstanceName();

    // Check if this is a register (external FIRRTL module with read/write)
    auto refModule = instance.getReferencedModule();
    if (!refModule)
      return;

    // For now, we treat any instance as a potential register
    // We'll detect registers by looking for read/write methods
    bool hasRead = false, hasWrite = false;

    // Check for read/write methods by looking at the module name
    // For now, treat all external modules with reg/Reg/Register in name as registers
    StringRef modName = refModule.moduleName();
    if (modName.contains_insensitive("reg") || modName.contains_insensitive("register")) {
      hasRead = true;
      hasWrite = true;
    }

    if (hasRead && hasWrite) {
      // This looks like a register
      RegisterState state;
      state.name = instanceName.str();
      state.width = 32; // Default, should be extracted from type
      state.value = APInt(state.width, 0);
      state.hasReset = true;
      state.resetValue = APInt(state.width, 0);

      // Try to extract width from the read method return type
      // For now, use default

      registers_[instanceName] = state;
      LLVM_DEBUG(llvm::dbgs() << "  Found register: " << instanceName << "\n");
    }
  });
}

void Cmt2Interpreter::reset() {
  cycle_ = 0;
  valueMap_.clear();
  pendingWrites_.clear();
  stepsDoneThisCycle_.clear();

  // Reset all registers to their reset values
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

  os_ << "Reset to cycle 0\n";
}

//===----------------------------------------------------------------------===//
// Execution Control
//===----------------------------------------------------------------------===//

std::vector<RuleResult> Cmt2Interpreter::step() {
  std::vector<RuleResult> results;

  // Clear pending writes from previous cycle
  pendingWrites_.clear();
  stepsDoneThisCycle_.clear();

  // Phase 1: Evaluate all rule guards
  std::vector<std::string> enabledRules = evaluateGuards();

  LLVM_DEBUG(llvm::dbgs() << "Cycle " << cycle_ << ": " << enabledRules.size()
                          << " rules enabled\n");

  // Phase 2: Resolve conflicts
  std::vector<std::string> rulesToFire = resolveConflicts(enabledRules);

  // Phase 3: Execute selected rules
  for (const std::string &ruleName : rulesToFire) {
    RuleResult result;
    result.ruleName = ruleName;
    result.guardEnabled = true;
    result.fired = true;

    // Find and execute the rule (check both regular rules and proc.rules)
    bool found = false;
    topModule_.walk([&](RuleOp rule) {
      if (rule.getSymName() == ruleName) {
        executeBody(rule.getBody());
        found = true;
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });

    if (!found) {
      // Check if it's a proc.rule
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

    results.push_back(result);
  }

  // Phase 3b: Continue executing any running proc.rules
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

      // Add to results as running
      bool alreadyInResults = false;
      for (const auto &r : results) {
        if (r.ruleName == fsmEntry.first()) {
          alreadyInResults = true;
          break;
        }
      }
      if (!alreadyInResults) {
        RuleResult result;
        result.ruleName = fsmEntry.first().str();
        result.guardEnabled = true;
        result.fired = true;  // Running counts as fired
        results.push_back(result);
      }
    }
  }

  // Add results for rules that were enabled but didn't fire
  for (const std::string &ruleName : enabledRules) {
    bool fired = std::find(rulesToFire.begin(), rulesToFire.end(), ruleName) !=
                 rulesToFire.end();
    if (!fired) {
      RuleResult result;
      result.ruleName = ruleName;
      result.guardEnabled = true;
      result.fired = false;
      results.push_back(result);
    }
  }

  // Phase 4: Apply state updates atomically
  applyStateUpdates();

  // Add trace entry if tracing is enabled
  if (tracingEnabled_) {
    addTraceEntry(results);
  }

  // Increment cycle
  cycle_++;

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

  // Check regular rules
  topModule_.walk([&](RuleOp rule) {
    // Evaluate the guard region
    Region &guardRegion = rule.getGuard();
    if (evaluateGuard(guardRegion)) {
      enabledRules.push_back(rule.getSymName().str());
    }
  });

  // Check proc.rules (only enabled when idle)
  topModule_.walk([&](ProcRuleOp procRule) {
    if (isProcRuleEnabled(procRule)) {
      enabledRules.push_back(procRule.getSymName().str());
    }
  });

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
  // Use precedence attribute from module to determine priority
  // precedence = [[@a, @b], [@c, @d]] means a > b and c > d in priority
  // In GAA semantics, only one rule fires per cycle by default

  if (enabledRules.empty())
    return {};

  // Build priority map from precedence attribute
  llvm::StringMap<unsigned> priorityMap;
  unsigned nextPriority = 0;

  // Parse precedence attribute from module
  if (auto precAttr = topModule_->getAttrOfType<ArrayAttr>("precedence")) {
    for (auto chain : precAttr) {
      if (auto chainArray = dyn_cast<ArrayAttr>(chain)) {
        // Each chain is [[@a, @b, ...]] meaning a > b > ... in priority
        for (auto elem : chainArray) {
          if (auto symRef = dyn_cast<FlatSymbolRefAttr>(elem)) {
            StringRef ruleName = symRef.getValue();
            if (!priorityMap.count(ruleName)) {
              priorityMap[ruleName] = nextPriority++;
            }
          }
        }
      }
    }
  }

  // Find the highest priority rule among enabled rules
  std::string bestRule = enabledRules[0];
  unsigned bestPriority = UINT_MAX;

  for (const std::string &ruleName : enabledRules) {
    unsigned priority = UINT_MAX;

    // Check precedence-based priority first
    auto it = priorityMap.find(ruleName);
    if (it != priorityMap.end()) {
      priority = it->second;
    } else {
      // Check for explicit priority attribute on the rule
      topModule_.walk([&](RuleOp rule) {
        if (rule.getSymName() == ruleName) {
          if (auto prioAttr = rule->getAttrOfType<IntegerAttr>("priority")) {
            priority = prioAttr.getInt();
          }
          return WalkResult::interrupt();
        }
        return WalkResult::advance();
      });

      // Also check proc.rules
      if (priority == UINT_MAX) {
        topModule_.walk([&](ProcRuleOp procRule) {
          if (procRule.getSymName() == ruleName) {
            if (auto prioAttr = procRule->getAttrOfType<IntegerAttr>("priority")) {
              priority = prioAttr.getInt();
            }
            return WalkResult::interrupt();
          }
          return WalkResult::advance();
        });
      }
    }

    // Lower priority number = higher priority
    if (priority < bestPriority) {
      bestPriority = priority;
      bestRule = ruleName;
    }
  }

  // Return only the highest priority rule (ORAAT semantics)
  return {bestRule};
}

//===----------------------------------------------------------------------===//
// State Inspection
//===----------------------------------------------------------------------===//

std::optional<InterpValue> Cmt2Interpreter::readRegister(StringRef name) {
  auto it = registers_.find(name);
  if (it == registers_.end())
    return std::nullopt;
  return it->second.value;
}

LogicalResult Cmt2Interpreter::writeRegister(StringRef name,
                                              const InterpValue &value) {
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
  for (const auto &entry : registers_) {
    names.push_back(entry.first().str());
  }
  return names;
}

std::vector<std::string> Cmt2Interpreter::getRuleNames() const {
  std::vector<std::string> names;
  if (topModule_) {
    // Regular rules
    topModule_->walk([&](RuleOp rule) {
      names.push_back(rule.getSymName().str());
    });
    // Proc rules
    topModule_->walk([&](ProcRuleOp procRule) {
      names.push_back(procRule.getSymName().str());
    });
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
  // Find the instance and execute the method
  // For now, handle register read/write specially
  auto regIt = registers_.find(instanceName);
  if (regIt != registers_.end()) {
    if (methodName == "read") {
      return std::vector<InterpValue>{regIt->second.value};
    } else if (methodName == "write" && !args.empty()) {
      pendingWrites_[instanceName] = args[0];
      return std::vector<InterpValue>{};
    }
  }

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
  for (const auto &entry : registers_) {
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

InterpValue Cmt2Interpreter::getValue(Value value) {
  // Check if we have a cached value
  auto it = valueMap_.find(value);
  if (it != valueMap_.end())
    return it->second;

  // For block arguments, return a default value
  // This would need to be set up properly for method arguments
  if (auto arg = dyn_cast<BlockArgument>(value)) {
    unsigned width = 32;
    if (auto intType = dyn_cast<IntegerType>(value.getType()))
      width = intType.getWidth();
    return APInt(width, 0);
  }

  // If the defining op is a constant, evaluate it
  if (auto *defOp = value.getDefiningOp()) {
    if (auto result = executeOp(defOp))
      return *result;
  }

  // Default
  unsigned width = 32;
  if (auto intType = dyn_cast<IntegerType>(value.getType()))
    width = intType.getWidth();
  return APInt(width, 0);
}

void Cmt2Interpreter::setValue(Value value, const InterpValue &v) {
  valueMap_[value] = v;
}

void Cmt2Interpreter::applyStateUpdates() {
  // Apply all pending writes atomically
  for (const auto &write : pendingWrites_) {
    auto it = registers_.find(write.first());
    if (it != registers_.end()) {
      LLVM_DEBUG(llvm::dbgs() << "Updating " << write.first() << " = "
                              << write.second << "\n");
      it->second.value = write.second;
    }
  }
  pendingWrites_.clear();
}

//===----------------------------------------------------------------------===//
// Procedural Construct Support
//===----------------------------------------------------------------------===//

void Cmt2Interpreter::initializeProcConstructs(cmt2::ModuleOp module) {
  procFSMStates_.clear();
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

    // Analyze control region to determine number of states
    // For now, we use a simple heuristic: count proc.enable ops
    unsigned stateCount = 0;
    procRule.getControl().walk([&](ProcEnableOp enable) {
      fsm.stateSteps.push_back(enable.getStepName().str());
      stateCount++;
    });

    // States: 0 = idle, 1..n = running states, n+1 = done (transitions back to idle)
    fsm.numStates = stateCount + 1;  // +1 for idle state

    procFSMStates_[procRule.getSymName()] = fsm;
    LLVM_DEBUG(llvm::dbgs() << "  Found proc.rule: " << procRule.getSymName()
                            << " with " << stateCount << " steps\n");
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
  // Check if FSM is idle
  auto fsmIt = procFSMStates_.find(procRule.getSymName());
  if (fsmIt != procFSMStates_.end() && fsmIt->second.isRunning) {
    return false;  // Already running, can't start again
  }

  // Evaluate the guard
  return evaluateGuard(procRule.getGuard());
}

void Cmt2Interpreter::executeProcRuleStep(ProcRuleOp procRule, ProcFSMState &fsm) {
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

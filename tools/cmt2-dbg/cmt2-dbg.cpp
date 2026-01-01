//===- cmt2-dbg.cpp - CMT2 GAA Debugger CLI ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the cmt2-dbg tool, a REPL-based debugger for CMT2
// circuits that implements GAA (Guarded Atomic Actions) semantics.
//
// Usage:
//   cmt2-dbg circuit.mlir                    # Interactive REPL
//   cmt2-dbg circuit.mlir < script.txt       # Read commands from file
//   cmt2-dbg circuit.mlir --script=script.txt  # Same, using --script option
//
// Script files support comments (lines starting with #).
//
// REPL Commands:
//   step [n]       - Execute n cycles (default 1)
//   run [n]        - Run until breakpoint or n cycles
//   reset          - Reset to initial state
//   state          - Print current state
//   rules          - Print rule status
//   regs           - List registers
//   reg <name>     - Read register value
//   set <name> <v> - Set register value
//   fire <rule>    - Manually fire a rule
//   break <rule>   - Set breakpoint on rule fire
//   bpc <cycle>    - Set breakpoint at cycle
//   bpw <reg>      - Set breakpoint on register write
//   delete <id>    - Delete breakpoint
//   list           - List breakpoints
//   trace on|off   - Enable/disable tracing
//   history [n]    - Show last n trace entries
//   help           - Show help
//   quit           - Exit
//
//===----------------------------------------------------------------------===//

#include "Interpreter.h"
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Support/Version.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

using namespace llvm;
using namespace mlir;
using namespace circt;
using namespace circt::cmt2;

//===----------------------------------------------------------------------===//
// Command Line Options
//===----------------------------------------------------------------------===//

static cl::OptionCategory mainCategory("cmt2-dbg options");

static cl::opt<std::string> inputFileName(cl::Positional,
                                          cl::desc("<input file>"),
                                          cl::init("-"), cl::cat(mainCategory));

static cl::opt<std::string>
    circuitName("circuit", cl::desc("Name of the circuit to simulate"),
                cl::value_desc("name"), cl::cat(mainCategory));

static cl::opt<bool>
    batchMode("batch", cl::desc("Run in batch mode (no interactive REPL)"),
              cl::init(false), cl::cat(mainCategory));

static cl::opt<std::string>
    scriptFile("script", cl::desc("Script file to execute"),
               cl::value_desc("file"), cl::cat(mainCategory));

static cl::opt<unsigned>
    maxCycles("max-cycles", cl::desc("Maximum cycles to run"),
              cl::init(1000000), cl::cat(mainCategory));

//===----------------------------------------------------------------------===//
// REPL Implementation
//===----------------------------------------------------------------------===//

/// Parse a command line into tokens
static std::vector<std::string> tokenize(StringRef line) {
  std::vector<std::string> tokens;
  std::string current;
  bool inQuote = false;

  for (char c : line) {
    if (c == '"') {
      inQuote = !inQuote;
    } else if (c == ' ' && !inQuote) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
    } else {
      current += c;
    }
  }

  if (!current.empty())
    tokens.push_back(current);

  return tokens;
}

/// Print help message
static void printHelp(raw_ostream &os) {
  os << R"(
CMT2 GAA Debugger Commands:
===========================

Execution Control:
  step [n]         Execute n cycles (default 1)
  run [n]          Run until breakpoint or n cycles (default max)
  reset            Reset to initial state
  fire <rule>      Manually fire a specific rule

State Inspection:
  state            Print all register values
  rules            Print rule status (enabled/disabled)
  regs             List all register names
  reg <name>       Read a register value
  set <name> <val> Set a register value
  value <name>     Read a value method output
  cycle            Print current cycle number

Breakpoints:
  break <rule>     Set breakpoint on rule fire
  bpc <cycle>      Set breakpoint at cycle number
  bpw <reg>        Set breakpoint on register write
  delete <id>      Delete breakpoint by ID
  enable <id>      Enable breakpoint
  disable <id>     Disable breakpoint
  list             List all breakpoints
  clear            Clear all breakpoints

Tracing:
  trace on|off     Enable/disable execution tracing
  history [n]      Show last n trace entries (default 10)

Other:
  help             Show this help
  quit             Exit the debugger

)";
}

/// Execute a single REPL command
static bool executeCommand(Cmt2Interpreter &interp,
                           const std::vector<std::string> &tokens,
                           raw_ostream &os) {
  if (tokens.empty())
    return true;

  const std::string &cmd = tokens[0];

  // step [n]
  if (cmd == "step" || cmd == "s") {
    unsigned n = 1;
    if (tokens.size() > 1)
      n = std::stoul(tokens[1]);

    for (unsigned i = 0; i < n; ++i) {
      auto results = interp.step();

      // Print cycle summary
      os << "Cycle " << interp.getCycle() - 1 << ": ";
      bool any = false;
      for (const auto &result : results) {
        if (result.fired) {
          if (any) os << ", ";
          os << result.ruleName;
          any = true;
        }
      }
      if (!any)
        os << "(no rules fired)";
      os << "\n";
    }
    return true;
  }

  // run [n]
  if (cmd == "run" || cmd == "r") {
    uint64_t n = maxCycles;
    if (tokens.size() > 1)
      n = std::stoull(tokens[1]);

    os << "Running for up to " << n << " cycles...\n";
    auto bp = interp.run(n);
    if (bp) {
      os << "Breakpoint hit at cycle " << interp.getCycle() << ": ";
      switch (bp->type) {
      case BreakpointType::RuleFire:
        os << "rule '" << bp->target << "' fired\n";
        break;
      case BreakpointType::RegisterWrite:
        os << "register '" << bp->target << "' written\n";
        break;
      case BreakpointType::Cycle:
        os << "cycle " << bp->cycleTarget << " reached\n";
        break;
      default:
        os << "unknown\n";
        break;
      }
    } else {
      os << "Ran " << n << " cycles, no breakpoint hit\n";
    }
    return true;
  }

  // reset
  if (cmd == "reset") {
    interp.reset();
    return true;
  }

  // state
  if (cmd == "state") {
    interp.printState();
    return true;
  }

  // rules
  if (cmd == "rules") {
    interp.printRuleStatus();
    return true;
  }

  // regs
  if (cmd == "regs") {
    os << "Registers:\n";
    for (const auto &name : interp.getRegisterNames()) {
      os << "  " << name << "\n";
    }
    return true;
  }

  // reg <name>
  if (cmd == "reg") {
    if (tokens.size() < 2) {
      os << "usage: reg <name>\n";
      return true;
    }
    auto value = interp.readRegister(tokens[1]);
    if (value) {
      os << tokens[1] << " = ";
      value->print(os, false); // Print as unsigned
      os << "\n";
    } else {
      os << "Register '" << tokens[1] << "' not found\n";
    }
    return true;
  }

  // set <name> <value>
  if (cmd == "set") {
    if (tokens.size() < 3) {
      os << "usage: set <name> <value>\n";
      return true;
    }
    uint64_t val = std::stoull(tokens[2]);
    APInt apval(32, val);  // Assume 32-bit for now
    if (succeeded(interp.writeRegister(tokens[1], apval))) {
      os << tokens[1] << " = ";
      apval.print(os, false); // Print as unsigned
      os << "\n";
    }
    return true;
  }

  // fire <rule>
  if (cmd == "fire") {
    if (tokens.size() < 2) {
      os << "usage: fire <rule>\n";
      return true;
    }
    if (succeeded(interp.fireRule(tokens[1]))) {
      os << "Fired rule '" << tokens[1] << "'\n";
    }
    return true;
  }

  // value <name>
  if (cmd == "value") {
    if (tokens.size() < 2) {
      os << "usage: value <name>\n";
      return true;
    }
    auto val = interp.readValue(tokens[1]);
    if (val) {
      os << tokens[1] << " = ";
      val->print(os, false); // Print as unsigned
      os << "\n";
    } else {
      os << "Value '" << tokens[1] << "' not available\n";
    }
    return true;
  }

  // cycle
  if (cmd == "cycle") {
    os << "Cycle: " << interp.getCycle() << "\n";
    return true;
  }

  // break <rule>
  if (cmd == "break" || cmd == "b") {
    if (tokens.size() < 2) {
      os << "usage: break <rule>\n";
      return true;
    }
    unsigned id = interp.addBreakpointOnRuleFire(tokens[1]);
    os << "Breakpoint " << id << ": on rule '" << tokens[1] << "' fire\n";
    return true;
  }

  // bpc <cycle>
  if (cmd == "bpc") {
    if (tokens.size() < 2) {
      os << "usage: bpc <cycle>\n";
      return true;
    }
    uint64_t cycle = std::stoull(tokens[1]);
    unsigned id = interp.addBreakpointAtCycle(cycle);
    os << "Breakpoint " << id << ": at cycle " << cycle << "\n";
    return true;
  }

  // bpw <reg>
  if (cmd == "bpw") {
    if (tokens.size() < 2) {
      os << "usage: bpw <register>\n";
      return true;
    }
    unsigned id = interp.addBreakpointOnRegisterWrite(tokens[1]);
    os << "Breakpoint " << id << ": on write to '" << tokens[1] << "'\n";
    return true;
  }

  // delete <id>
  if (cmd == "delete" || cmd == "d") {
    if (tokens.size() < 2) {
      os << "usage: delete <id>\n";
      return true;
    }
    unsigned id = std::stoul(tokens[1]);
    if (interp.removeBreakpoint(id)) {
      os << "Deleted breakpoint " << id << "\n";
    } else {
      os << "Breakpoint " << id << " not found\n";
    }
    return true;
  }

  // enable <id>
  if (cmd == "enable") {
    if (tokens.size() < 2) {
      os << "usage: enable <id>\n";
      return true;
    }
    unsigned id = std::stoul(tokens[1]);
    interp.setBreakpointEnabled(id, true);
    os << "Enabled breakpoint " << id << "\n";
    return true;
  }

  // disable
  if (cmd == "disable") {
    if (tokens.size() < 2) {
      os << "usage: disable <id>\n";
      return true;
    }
    unsigned id = std::stoul(tokens[1]);
    interp.setBreakpointEnabled(id, false);
    os << "Disabled breakpoint " << id << "\n";
    return true;
  }

  // list
  if (cmd == "list" || cmd == "l") {
    const auto &bps = interp.getBreakpoints();
    if (bps.empty()) {
      os << "No breakpoints set\n";
    } else {
      os << "Breakpoints:\n";
      for (const auto &bp : bps) {
        os << "  " << bp.id << ": ";
        switch (bp.type) {
        case BreakpointType::RuleFire:
          os << "rule '" << bp.target << "' fire";
          break;
        case BreakpointType::RegisterWrite:
          os << "write to '" << bp.target << "'";
          break;
        case BreakpointType::Cycle:
          os << "cycle " << bp.cycleTarget;
          break;
        default:
          os << "unknown";
          break;
        }
        os << (bp.enabled ? "" : " (disabled)") << "\n";
      }
    }
    return true;
  }

  // clear
  if (cmd == "clear") {
    interp.clearBreakpoints();
    os << "Cleared all breakpoints\n";
    return true;
  }

  // trace on|off
  if (cmd == "trace") {
    if (tokens.size() < 2) {
      os << "Tracing: " << (interp.isTracingEnabled() ? "on" : "off") << "\n";
      return true;
    }
    bool enabled = tokens[1] == "on" || tokens[1] == "1";
    interp.setTracing(enabled);
    os << "Tracing " << (enabled ? "enabled" : "disabled") << "\n";
    return true;
  }

  // history [n]
  if (cmd == "history") {
    unsigned n = 10;
    if (tokens.size() > 1)
      n = std::stoul(tokens[1]);

    const auto &traces = interp.getTraces();
    unsigned start = traces.size() > n ? traces.size() - n : 0;

    if (traces.empty()) {
      os << "No trace history (enable tracing with 'trace on')\n";
    } else {
      for (unsigned i = start; i < traces.size(); ++i) {
        interp.printTrace(traces[i]);
      }
    }
    return true;
  }

  // help
  if (cmd == "help" || cmd == "h" || cmd == "?") {
    printHelp(os);
    return true;
  }

  // quit
  if (cmd == "quit" || cmd == "q" || cmd == "exit") {
    return false;
  }

  os << "Unknown command: " << cmd << "\n";
  os << "Type 'help' for available commands\n";
  return true;
}

//===----------------------------------------------------------------------===//
// Main
//===----------------------------------------------------------------------===//

int main(int argc, char **argv) {
  InitLLVM y(argc, argv);

  // Set bug report message
  setBugReportMsg(circtBugReportMsg);

  cl::ParseCommandLineOptions(argc, argv,
                              "CMT2 GAA Debugger\n\n"
                              "An interactive debugger for CMT2 circuits\n"
                              "implementing Guarded Atomic Actions semantics.\n");

  // Load the input file
  auto fileOrErr = MemoryBuffer::getFileOrSTDIN(inputFileName.c_str());
  if (std::error_code error = fileOrErr.getError()) {
    errs() << argv[0] << ": could not open input file '" << inputFileName
           << "': " << error.message() << "\n";
    return 1;
  }

  // Set up MLIR context
  MLIRContext context;
  context.loadDialect<cmt2::Cmt2Dialect, comb::CombDialect, hw::HWDialect,
                      firrtl::FIRRTLDialect, arith::ArithDialect,
                      func::FuncDialect>();
  context.allowUnregisteredDialects();

  // Parse the input
  SourceMgr sourceMgr;
  sourceMgr.AddNewSourceBuffer(std::move(*fileOrErr), SMLoc());
  OwningOpRef<mlir::ModuleOp> module(parseSourceFile<mlir::ModuleOp>(sourceMgr, &context));
  if (!module) {
    errs() << "error: could not parse input file\n";
    return 1;
  }

  // Create interpreter
  Cmt2Interpreter interp(*module, outs());

  // Find circuit name if not specified
  std::string circuitArg = circuitName;
  // CMT2 CircuitOp doesn't have a name, just use empty string
  if (circuitArg.empty()) {
    circuitArg = "circuit";
  }

  // Initialize
  if (failed(interp.initialize(circuitArg))) {
    return 1;
  }

  interp.reset();

  // Run script file if specified
  if (!scriptFile.empty()) {
    auto scriptOrErr = MemoryBuffer::getFile(scriptFile);
    if (std::error_code error = scriptOrErr.getError()) {
      errs() << "error: could not open script file: " << error.message()
             << "\n";
      return 1;
    }

    StringRef script = (*scriptOrErr)->getBuffer();
    SmallVector<StringRef> lines;
    script.split(lines, '\n');

    for (StringRef line : lines) {
      line = line.trim();
      if (line.empty() || line.starts_with("#"))
        continue;

      outs() << "> " << line << "\n";
      auto tokens = tokenize(line);
      if (!executeCommand(interp, tokens, outs()))
        break;
    }

    if (batchMode)
      return 0;
  }

  // Batch mode: just exit
  if (batchMode) {
    return 0;
  }

  // Check if stdin is a terminal (interactive) or a pipe/file
  bool isInteractive = isatty(fileno(stdin));

  // Interactive REPL
  if (isInteractive) {
    outs() << "CMT2 GAA Debugger\n";
    outs() << "Type 'help' for available commands\n\n";
  }

  std::string lineBuffer;
  while (true) {
    if (isInteractive) {
      outs() << "(cmt2-dbg) ";
      outs().flush();
    }

    if (!std::getline(std::cin, lineBuffer))
      break;

    StringRef line(lineBuffer);
    line = line.trim();

    // Skip empty lines and comments
    if (line.empty() || line.starts_with("#"))
      continue;

    auto tokens = tokenize(line);
    if (!executeCommand(interp, tokens, outs()))
      break;
  }

  return 0;
}

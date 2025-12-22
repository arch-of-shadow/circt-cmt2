//===- STLLibrary.h - ECMT2 Standard Template Library -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the ECMT2 Standard Template Library (STL) for common
// hardware building blocks. The STL provides factory methods that create
// Module instances for common components like wires, registers, FIFOs, etc.
//
// Usage:
//   #include "circt/Dialect/Cmt2/ECMT2/STLLibrary.h"
//   using namespace circt::cmt2::ecmt2::stl;
//
//   // Create modules using factory methods
//   auto* regModule = STLLibrary::createRegModule("my_reg", 32, circuit);
//   auto* fifoModule = STLLibrary::createFIFOModule("my_fifo", 32, 16, circuit);
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_STLLIBRARY_H
#define CIRCT_DIALECT_CMT2_ECMT2_STLLIBRARY_H

#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace circt::cmt2::ecmt2::stl {

//===----------------------------------------------------------------------===//
// STLLibrary - Factory for creating standard hardware modules
//===----------------------------------------------------------------------===//

class STLLibrary {
public:  
  //===--------------------------------------------------------------------===//
  // Wire modules
  //===--------------------------------------------------------------------===//

  /// Create a wire module with specified width
  static Module* createWireModule(unsigned width, Circuit& circuit);

  /// Create a wire module with specified width and default init value
  static Module* createWireDefaultModule(unsigned width, unsigned init, Circuit& circuit);

  //===--------------------------------------------------------------------===//
  // Register modules
  //===--------------------------------------------------------------------===//

  /// Create a register module with specified width and init value
  static Module* createRegModule(unsigned width, unsigned init, Circuit& circuit);

  //===--------------------------------------------------------------------===//
  // FIFO modules
  //===--------------------------------------------------------------------===//

  /// Create a depth-1 FIFO module (actively push)
  static Module* createFIFO1PushModule(unsigned dataWidth, Circuit& circuit);

  /// Create a depth-1 FIFO module (actively pull)
  static Module* createFIFO1PullModule(unsigned dataWidth, Circuit& circuit);

  /// Create a depth-2 FIFO module (independent enq/deq, double buffered)
  static Module* createFIFO2IModule(unsigned dataWidth, Circuit& circuit);

  //===--------------------------------------------------------------------===//
  // Memory modules
  //===--------------------------------------------------------------------===//

  /// Create a 1-read 1-write memory module, read latency and write latency are both 1
  static Module* createMem1r1w1cModule(unsigned dataWidth, unsigned addrWidth,
    unsigned depth, Circuit& circuit);

  /// Create a 1-read 1-write memory module, write latency is 1, read latency is 0
  static Module* createMem1r1w0cModule(unsigned dataWidth, unsigned addrWidth,
    unsigned depth, Circuit& circuit);

  //===--------------------------------------------------------------------===//
  // Floating-point IP modules (external Verilog)
  //===--------------------------------------------------------------------===//

  /// Create a floating-point adder module (references external Verilog)
  static Module* createFloatAddModule(unsigned width, unsigned latency, Circuit& circuit);

  /// Create a floating-point subtractor module
  static Module* createFloatSubModule(unsigned width, unsigned latency, Circuit& circuit);

  /// Create a floating-point multiplier module
  static Module* createFloatMulModule(unsigned width, unsigned latency, Circuit& circuit);

  /// Create a floating-point divider module
  static Module* createFloatDivModule(unsigned width, unsigned latency, Circuit& circuit);

  /// Create a floating-point square root module
  static Module* createFloatSqrtModule(unsigned width, unsigned latency, Circuit& circuit);

  /// Create a floating-point comparator module
  /// @param predicate - 0=eq, 1=lt, 2=le, 3=gt, 4=ge, 5=ne, 6=ord, 7=uno
  static Module* createFloatCmpModule(unsigned width, unsigned predicate,
                                      unsigned latency, Circuit& circuit);
};

} // namespace circt::cmt2::ecmt2::stl

#endif // CIRCT_DIALECT_CMT2_ECMT2_STLLIBRARY_H
//===- STLLibrary.cpp - ECMT2 STL Module Factory Implementation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ECMT2 Standard Template Library factory methods.
// These methods create common hardware modules using proven patterns from APS.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/STLLibrary.h"
#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/Cmt2/ECMT2/Instance.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt::cmt2::ecmt2;
using namespace circt::cmt2::ecmt2::stl;


//===----------------------------------------------------------------------===//
// STLLibrary Implementation
//===----------------------------------------------------------------------===//
ExternalModule* STLLibrary::createWireModule(unsigned width, Circuit& circuit) {
  llvm::StringMap<int64_t> params;
  params["width"] = width;

  auto *wireMod = circuit.hasExternalModule("Wire", params);
  if (wireMod) {
    return wireMod;
  }
  wireMod = circuit.addExternalModule("Wire", params);

  wireMod->bindMethod("write", "write_enable", "write_ready", {"write_data"}, {});
  wireMod->bindValue("read", "read_ready", {}, {"read_data"});
  wireMod->addConflict("write", "write");
  wireMod->addSequenceBefore("write", "read");

  return wireMod;
}

Module* STLLibrary::createWireDefaultModule(unsigned width, unsigned init, Circuit& circuit) {
  auto *wireDefaultMod = circuit.addModule("WireDefault_w" + std::to_string(width) + "_i" + std::to_string(init));
  auto &builder = wireDefaultMod->getBuilder();
  auto loc = wireDefaultMod->getLoc();

  auto *inner = createWireModule(width, circuit);
  auto *innerinst = wireDefaultMod->addInstance("inner", inner, {});

  auto wireType = circt::firrtl::UIntType::get(builder.getContext(), width);

  // Add read value that delegates to inner wire
  auto *readVal = wireDefaultMod->addValue("read", {wireType});
  readVal->guard([&](mlir::OpBuilder &b) {
    Signal trueValue = UInt::constant(1, 1, builder, loc);
    b.create<circt::cmt2::ReturnOp>(loc, trueValue.getValue());
  });
  readVal->body([&, innerinst](mlir::OpBuilder &b) {
    auto vals = innerinst->callValue("read", b);
    b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{vals[0]});
  });
  readVal->finalize();

  // Add write method that delegates to inner wire
  auto *writeMethod = wireDefaultMod->addMethod("write", {{"in_", wireType}}, {});
  writeMethod->guard([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
    Signal trueValue = UInt::constant(1, 1, builder, loc);
    b.create<circt::cmt2::ReturnOp>(loc, trueValue.getValue());
  });
  writeMethod->body([&, innerinst](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
    auto inVal = args[0];
    innerinst->callMethod("write", {inVal}, b);
    b.create<circt::cmt2::ReturnOp>(loc);
  });
  writeMethod->finalize();

  // Add default rule that writes default value
  auto *defaultRule = wireDefaultMod->addRule("default");
  defaultRule->guard([&](mlir::OpBuilder &b) {
    Signal trueValue = UInt::constant(1, 1, builder, loc);
    b.create<circt::cmt2::ReturnOp>(loc, trueValue.getValue());
  });
  defaultRule->body([&, innerinst](mlir::OpBuilder &b) {
    Signal initValue = UInt::constant(init, width, builder, loc);
    innerinst->callMethod("write", {initValue.getValue()}, b);
    b.create<circt::cmt2::ReturnOp>(loc);
  });
  defaultRule->finalize();

  // Set scheduling: write, default, read
  wireDefaultMod->setPrecedence({{"write", "default"}, {"default", "read"}});

  return wireDefaultMod;
}

ExternalModule* STLLibrary::createRegModule(unsigned width, unsigned init, Circuit& circuit) {
  // Create external module directly - no wrapper needed
  llvm::StringMap<int64_t> params;
  params["width"] = width;
  params["init"] = init;

  auto *regMod = circuit.hasExternalModule("FIRRTLReg", params);

  if (regMod) {
    return regMod;
  }

  regMod = circuit.addExternalModule("FIRRTLReg", params);

  // Apply the same binding pattern used in APS
  regMod->bindClock("clk", "clock")
        .bindReset("rst", "reset")
        .bindValue("read", "read_ready", {}, {"read_data"})
        .bindMethod("write", "write_enable", "write_ready", {"write_data"}, {})
        ;
        // .addSequenceBefore("read", "write");

  return regMod;
}

Module* STLLibrary::createFIFO1PushModule(unsigned dataWidth, Circuit& circuit) {
  std::string moduleName = "FIFO1_PUSH_w" + std::to_string(dataWidth);
  auto *fifoMod = circuit.addModule(moduleName);
  fifoMod->setPrecedence({
      {"full", "deq"}, 
      {"deq", "enq"}, 
      {"enq", "deqed_default"},
      {"deqed_default", "enqed_default"},
      {"enqed_default", "next"}
    });
    
  Clock clk = fifoMod->addClockArgument("clk");
  Reset rst = fifoMod->addResetArgument("rst");

  auto &context = circuit.getContext();
  auto dataType = circt::firrtl::UIntType::get(&context, dataWidth);
  auto boolType = circt::firrtl::UIntType::get(&context, 1);

  // Instances
  auto *regDataT = createRegModule(dataWidth, 0, circuit);
  auto *regBooleanT = createRegModule(1, 0, circuit);
  auto *wireBooleanT = createWireModule(1, circuit);

  auto *reg = fifoMod->addInstance("reg_data", regDataT, {clk.getValue(), rst.getValue()});
  auto *fullReg = fifoMod->addInstance("full_reg", regBooleanT, {clk.getValue(), rst.getValue()});
  auto *deqed = fifoMod->addInstance("deqed", wireBooleanT, {clk.getValue(), rst.getValue()});
  auto *enqed = fifoMod->addInstance("enqed", wireBooleanT, {clk.getValue(), rst.getValue()});

  // Value: full() -> bool
  auto *fullVal = fifoMod->addValue("full", {boolType});
  fullVal->guard([&](mlir::OpBuilder &b) {
    auto trueVal = UInt::constant(1, 1, b, fifoMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{trueVal.getValue()});
  });
  fullVal->body([&](mlir::OpBuilder &b) {
    auto fullVals = fullReg->callValue("read", b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{fullVals[0]});
  });
  fullVal->finalize();

  // Value: deq() -> data
  // Guard: full_reg.read()
  auto *deqMethod = fifoMod->addMethod("deq", {}, {{dataType}});
  deqMethod->guard([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
    auto fullVals = fullReg->callValue("read", b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{fullVals[0]});
  });
  deqMethod->body([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
    auto c1 = UInt::constant(1, 1, b, fifoMod->getLoc());
    deqed->callMethod("write", {c1.getValue()}, b);
    auto dataVals = reg->callValue("read", b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{dataVals[0]});
  });
  deqMethod->finalize();

  // Method: enq(data)
  // Guard: !full_reg.read() | deqed.read()
  auto *enqMethod = fifoMod->addMethod("enq", {{"data", dataType}}, {});
  enqMethod->guard([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
    auto fullVals = fullReg->callValue("read", b);
    auto deqedVals = deqed->callValue("read", b);
    Signal fullSig(fullVals[0], &b, fifoMod->getLoc());
    Signal deqedSig(deqedVals[0], &b, fifoMod->getLoc());
    auto notFull = ~fullSig;
    auto canEnq = notFull | deqedSig;
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{canEnq.getValue()});
  });
  enqMethod->body([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
    auto c1 = UInt::constant(1, 1, b, fifoMod->getLoc());
    enqed->callMethod("write", {c1.getValue()}, b);
    reg->callMethod("write", {args[0]}, b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{});
  });
  enqMethod->finalize();

  // Rules for wire defaults and state update
  auto *deqedDefault = fifoMod->addRule("deqed_default");
  deqedDefault->guard([&](mlir::OpBuilder &b) {
    auto trueVal = UInt::constant(1, 1, b, fifoMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{trueVal.getValue()});
  });
  deqedDefault->body([&](mlir::OpBuilder &b) {
    auto c0 = UInt::constant(0, 1, b, fifoMod->getLoc());
    deqed->callMethod("write", {c0.getValue()}, b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{});
  });
  deqedDefault->finalize();

  auto *enqedDefault = fifoMod->addRule("enqed_default");
  enqedDefault->guard([&](mlir::OpBuilder &b) {
    auto trueVal = UInt::constant(1, 1, b, fifoMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{trueVal.getValue()});
  });
  enqedDefault->body([&](mlir::OpBuilder &b) {
    auto c0 = UInt::constant(0, 1, b, fifoMod->getLoc());
    enqed->callMethod("write", {c0.getValue()}, b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{});
  });
  enqedDefault->finalize();

  // Rule: next - update full_reg
  // full_reg.write(enqed.read() | full_reg.read() & !deqed.read())
  auto *nextRule = fifoMod->addRule("next");
  nextRule->guard([&](mlir::OpBuilder &b) {
    auto trueVal = UInt::constant(1, 1, b, fifoMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{trueVal.getValue()});
  });
  nextRule->body([&](mlir::OpBuilder &b) {
    auto enqedVals = enqed->callValue("read", b);
    auto deqedVals = deqed->callValue("read", b);
    auto fullVals = fullReg->callValue("read", b);
    Signal enqedSig(enqedVals[0], &b, fifoMod->getLoc());
    Signal deqedSig(deqedVals[0], &b, fifoMod->getLoc());
    Signal fullSig(fullVals[0], &b, fifoMod->getLoc());
    auto notDeqed = ~deqedSig;
    auto fullAndNotDeqed = fullSig & notDeqed;
    auto nextFull = enqedSig | fullAndNotDeqed;
    fullReg->callMethod("write", {nextFull.getValue()}, b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{});
  });
  nextRule->finalize();

  return fifoMod;
}

Module* STLLibrary::createFIFO1PullModule(unsigned dataWidth, Circuit& circuit) {
  std::string moduleName = "FIFO1_PULL_w" + std::to_string(dataWidth);
  auto *fifoMod = circuit.addModule(moduleName);

  Clock clk = fifoMod->addClockArgument("clk");
  Reset rst = fifoMod->addResetArgument("rst");

  auto &context = circuit.getContext();
  auto dataType = circt::firrtl::UIntType::get(&context, dataWidth);
  auto boolType = circt::firrtl::UIntType::get(&context, 1);

  // Instances
  auto *regDataT = createRegModule(dataWidth, 0, circuit);
  auto *regBooleanT = createRegModule(1, 0, circuit);
  auto *wireBooleanT = createWireModule(1, circuit);

  auto *reg = fifoMod->addInstance("reg_data", regDataT, {clk.getValue(), rst.getValue()});
  auto *fullReg = fifoMod->addInstance("full_reg", regBooleanT, {clk.getValue(), rst.getValue()});
  auto *deqed = fifoMod->addInstance("deqed", wireBooleanT, {clk.getValue(), rst.getValue()});
  auto *enqed = fifoMod->addInstance("enqed", wireBooleanT, {clk.getValue(), rst.getValue()});

  // Value: full() -> bool
  auto *fullVal = fifoMod->addValue("full", {boolType});
  fullVal->guard([&](mlir::OpBuilder &b) {
    auto trueVal = UInt::constant(1, 1, b, fifoMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{trueVal.getValue()});
  });
  fullVal->body([&](mlir::OpBuilder &b) {
    auto fullVals = fullReg->callValue("read", b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{fullVals[0]});
  });
  fullVal->finalize();

  // Method: enq(data)
  // Guard: none (always ready)
  auto *enqMethod = fifoMod->addMethod("enq", {{"data", dataType}}, {});
  enqMethod->guard([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
    auto trueVal = UInt::constant(1, 1, b, fifoMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{trueVal.getValue()});
  });
  enqMethod->body([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
    auto c1 = UInt::constant(1, 1, b, fifoMod->getLoc());
    enqed->callMethod("write", {c1.getValue()}, b);
    reg->callMethod("write", {args[0]}, b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{});
  });
  enqMethod->finalize();

  // Value: deq() -> data
  // Guard: full_reg.read() & enqed.read()
  auto *deqVal = fifoMod->addValue("deq", {dataType});
  deqVal->guard([&](mlir::OpBuilder &b) {
    auto fullVals = fullReg->callValue("read", b);
    auto enqedVals = enqed->callValue("read", b);
    Signal fullSig(fullVals[0], &b, fifoMod->getLoc());
    Signal enqedSig(enqedVals[0], &b, fifoMod->getLoc());
    auto canDeq = fullSig & enqedSig;
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{canDeq.getValue()});
  });
  deqVal->body([&](mlir::OpBuilder &b) {
    auto c1 = UInt::constant(1, 1, b, fifoMod->getLoc());
    deqed->callMethod("write", {c1.getValue()}, b);
    auto dataVals = reg->callValue("read", b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{dataVals[0]});
  });
  deqVal->finalize();

  // Rules (same as PUSH)
  auto *enqedDefault = fifoMod->addRule("enqed_default");
  enqedDefault->guard([&](mlir::OpBuilder &b) {
    auto trueVal = UInt::constant(1, 1, b, fifoMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{trueVal.getValue()});
  });
  enqedDefault->body([&](mlir::OpBuilder &b) {
    auto c0 = UInt::constant(0, 1, b, fifoMod->getLoc());
    enqed->callMethod("write", {c0.getValue()}, b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{});
  });
  enqedDefault->finalize();

  auto *deqedDefault = fifoMod->addRule("deqed_default");
  deqedDefault->guard([&](mlir::OpBuilder &b) {
    auto trueVal = UInt::constant(1, 1, b, fifoMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{trueVal.getValue()});
  });
  deqedDefault->body([&](mlir::OpBuilder &b) {
    auto c0 = UInt::constant(0, 1, b, fifoMod->getLoc());
    deqed->callMethod("write", {c0.getValue()}, b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{});
  });
  deqedDefault->finalize();

  auto *nextRule = fifoMod->addRule("next");
  nextRule->guard([&](mlir::OpBuilder &b) {
    auto trueVal = UInt::constant(1, 1, b, fifoMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{trueVal.getValue()});
  });
  nextRule->body([&](mlir::OpBuilder &b) {
    auto enqedVals = enqed->callValue("read", b);
    auto deqedVals = deqed->callValue("read", b);
    auto fullVals = fullReg->callValue("read", b);
    Signal enqedSig(enqedVals[0], &b, fifoMod->getLoc());
    Signal deqedSig(deqedVals[0], &b, fifoMod->getLoc());
    Signal fullSig(fullVals[0], &b, fifoMod->getLoc());
    auto notDeqed = ~deqedSig;
    auto fullAndNotDeqed = fullSig & notDeqed;
    auto nextFull = enqedSig | fullAndNotDeqed;
    fullReg->callMethod("write", {nextFull.getValue()}, b);
    b.create<circt::cmt2::ReturnOp>(fifoMod->getLoc(), mlir::ValueRange{});
  });
  nextRule->finalize();

  return fifoMod;
}

ExternalModule* STLLibrary::createMem1r1w1cModule( unsigned dataWidth, unsigned addrWidth, unsigned depth, unsigned readLatency, Circuit& circuit) {
  // Create external memory module with Mem1r1w binding
  llvm::StringMap<int64_t> params;
  params["data_width"] = dataWidth;
  params["addr_width"] = addrWidth;
  params["depth"] = depth;
  params["read_latency"] = readLatency;

  auto* memMod = circuit.addExternalModule("Mem1r1w1c", params);

  // Bind memory interface
  memMod->bindClock("clk", "clock")
        .bindReset("rst", "reset")
        .bindMethod("rd0", "en", "", {"raddr"}, {})
        .bindValue("rd1", "rd1_valid", {}, {"rdata"})
        .bindMethod("write", "wen", "", {"wdata", "waddr"}, {});

  return memMod;
}

ExternalModule* STLLibrary::createMem1r1w0cModule( unsigned dataWidth, unsigned addrWidth, unsigned depth, unsigned readLatency, Circuit& circuit) {
  // Create external memory module with Mem1r1w binding
  llvm::StringMap<int64_t> params;
  params["data_width"] = dataWidth;
  params["addr_width"] = addrWidth;
  params["depth"] = depth;
  params["read_latency"] = readLatency;

  auto* memMod = circuit.addExternalModule("Mem1r1w0c", params);

  // Bind memory interface
  memMod->bindClock("clk", "clock")
        .bindReset("rst", "reset")
        .bindMethod("read", "en", "", {"raddr"}, {"rdata"})
        .bindMethod("write", "wen", "", {"wdata", "waddr"}, {});

  return memMod;
}
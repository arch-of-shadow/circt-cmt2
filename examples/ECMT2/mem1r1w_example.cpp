//===- mem1r1w_example.cpp - Memory 1R1W Example --------------*- C++ -*-===//
//
// Example using 1R1W synchronous memory from ModuleLibrary
// Based on Rust CMT2: crates/cmt2/core/src/cmtrs/stl/mem.rs
//
// Demonstrates:
// - Loading memory from library with parameters
// - Using rd0/rd1 two-stage read protocol
// - Using write method
// - Conflict matrix (write C write, rd0 C rd0)
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/MLIRContext.h"

using namespace circt::cmt2::ecmt2;

int main() {
  // Initialize MLIR
  mlir::MLIRContext context;
  context.loadDialect<circt::cmt2::Cmt2Dialect>();
  context.loadDialect<circt::firrtl::FIRRTLDialect>();

  // Initialize module library
  auto &library = ModuleLibrary::getInstance();
  std::string manifestPath = "lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml";
  if (library.loadManifest(manifestPath).failed()) {
    llvm::errs() << "Warning: Failed to load module library manifest\n";
  }

  // Create circuit
  Circuit circuit("MemoryTest", context);

  // Create 1R1W memory from library
  // Parameters: 32-bit data, 10-bit address (1024 entries)
  llvm::StringMap<int64_t> memParams;
  memParams["data_width"] = 32;
  memParams["addr_width"] = 10;
  memParams["depth"] = 1024;

  // Name auto-generated: Mem1r1w_w32_a10_d1024
  auto *memory = circuit.addExternalModule("Mem1r1w", memParams);

  // Bind clock and reset
  memory->bindClock("clk", "clock")
        .bindReset("rst", "reset");

  // Bind methods according to manifest
  // rd0: enable-based, takes raddr input
  memory->bindMethod("rd0", "en", "", {"raddr"}, {});

  // rd1: ready-based (rd1_valid), returns rdata
  memory->bindValue("rd1", "rd1_valid", {"rdata"});

  // write: enable-based, takes wdata and waddr inputs
  memory->bindMethod("write", "wen", "", {"wdata", "waddr"}, {});

  // Add conflict relationships from manifest
  memory->addConflict("write", "write");   // write C write
  memory->addConflict("rd0", "rd0");       // rd0 C rd0

  // Create a test module that uses the memory
  auto *testMod = circuit.addModule("MemoryTestModule");

  Clock clk = testMod->addClockArgument("clk");
  Reset rst = testMod->addResetArgument("rst");

  // Create memory instance
  auto *mem = testMod->addInstance("mem", memory, {clk.getValue(), rst.getValue()});

  auto &ctx = circuit.getContext();
  auto addrType = circt::firrtl::UIntType::get(&ctx, 10);
  auto dataType = circt::firrtl::UIntType::get(&ctx, 32);

  // Rule: write_test - write some test data
  auto *writeTest = testMod->addRule("write_test");
  writeTest->guard([&](mlir::OpBuilder &b) {
    auto trueVal = UInt::constant(1, 1, b, testMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(testMod->getLoc(),
                                    mlir::ValueRange{trueVal.getValue()});
  });
  writeTest->body([&](mlir::OpBuilder &b) {
    // Write value 42 to address 0
    auto addr = UInt::constant(0, 10, b, testMod->getLoc());
    auto data = UInt::constant(42, 32, b, testMod->getLoc());
    mem->callMethod("write", {data.getValue(), addr.getValue()}, b);
    b.create<circt::cmt2::ReturnOp>(testMod->getLoc(), mlir::ValueRange{});
  });
  writeTest->finalize();

  // Rule: read_test - start read from address 0
  auto *readTest = testMod->addRule("read_test");
  readTest->guard([&](mlir::OpBuilder &b) {
    auto trueVal = UInt::constant(1, 1, b, testMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(testMod->getLoc(),
                                    mlir::ValueRange{trueVal.getValue()});
  });
  readTest->body([&](mlir::OpBuilder &b) {
    // Start read from address 0
    auto addr = UInt::constant(0, 10, b, testMod->getLoc());
    mem->callMethod("rd0", {addr.getValue()}, b);
    b.create<circt::cmt2::ReturnOp>(testMod->getLoc(), mlir::ValueRange{});
  });
  readTest->finalize();

  // Value: get_result - get read result (next cycle after rd0)
  auto *getResult = testMod->addValue("get_result", {dataType});
  getResult->guard([&](mlir::OpBuilder &b) {
    // Guard is rd1_valid from memory
    // This is automatically handled by bindValue
    auto trueVal = UInt::constant(1, 1, b, testMod->getLoc());
    b.create<circt::cmt2::ReturnOp>(testMod->getLoc(),
                                    mlir::ValueRange{trueVal.getValue()});
  });
  getResult->body([&](mlir::OpBuilder &b) {
    auto resultVals = mem->callValue("rd1", b);
    b.create<circt::cmt2::ReturnOp>(testMod->getLoc(),
                                    mlir::ValueRange{resultVals[0]});
  });
  getResult->finalize();

  llvm::errs() << "\n=== Memory 1R1W Example (CMT2 + ModuleLibrary) ===\n";
  llvm::errs() << "Memory configuration:\n";
  llvm::errs() << "  Data width: 32 bits\n";
  llvm::errs() << "  Address width: 10 bits\n";
  llvm::errs() << "  Depth: 1024 entries\n";
  llvm::errs() << "  Read latency: 1 cycle (rd0 -> rd1)\n";
  llvm::errs() << "  Write latency: 1 cycle\n\n";

  llvm::errs() << "Generated CMT2 MLIR:\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  return 0;
}

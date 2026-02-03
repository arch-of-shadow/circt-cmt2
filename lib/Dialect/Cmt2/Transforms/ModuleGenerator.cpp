//===- ModuleGenerator.cpp - Generate storage modules in passes -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ModuleGenerator utility class that uses the
// ModuleLibrary to generate storage modules during transformation passes.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Transforms/ModuleGenerator.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-module-generator"

using namespace circt;
using namespace cmt2;
using namespace mlir;

ModuleGenerator::ModuleGenerator(CircuitOp circuit) : circuit_(circuit) {
  // Pre-populate cache with existing modules
  for (auto &op : circuit.getBody().front()) {
    if (auto mod = dyn_cast<cmt2::ModuleOp>(op)) {
      moduleCache_[mod.getSymName()] = mod;
    } else if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(op)) {
      extModuleCache_[extMod.getSymName()] = extMod;
    }
  }
}

LogicalResult ModuleGenerator::initialize(StringRef manifestPath) {
  auto &library = ecmt2::ModuleLibrary::getInstance();
  if (failed(library.loadManifest(manifestPath))) {
    return failure();
  }
  initialized_ = true;
  return success();
}

cmt2::ModuleOp ModuleGenerator::findModule(StringRef name) {
  auto it = moduleCache_.find(name);
  if (it != moduleCache_.end())
    return it->second;

  // Check circuit in case module was added elsewhere
  for (auto &op : circuit_.getBody().front()) {
    if (auto mod = dyn_cast<cmt2::ModuleOp>(op)) {
      if (mod.getSymName() == name) {
        moduleCache_[name] = mod;
        return mod;
      }
    }
  }
  return nullptr;
}

ExtModuleFirrtlOp ModuleGenerator::findExtModule(StringRef name) {
  auto it = extModuleCache_.find(name);
  if (it != extModuleCache_.end())
    return it->second;

  // Check circuit in case module was added elsewhere
  for (auto &op : circuit_.getBody().front()) {
    if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(op)) {
      if (extMod.getSymName() == name) {
        extModuleCache_[name] = extMod;
        return extMod;
      }
    }
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Reg Module - Uses ModuleLibrary
//===----------------------------------------------------------------------===//

ExtModuleFirrtlOp ModuleGenerator::getOrCreateRegModule(unsigned dataWidth) {
  // Create a unique name for this Reg configuration
  std::string name = "Reg_w" + std::to_string(dataWidth);

  // Check if already exists in cache
  if (auto existing = findExtModule(name))
    return existing;

  return createRegExtModule(dataWidth);
}

ExtModuleFirrtlOp ModuleGenerator::createRegExtModule(unsigned dataWidth) {
  LLVM_DEBUG(llvm::dbgs() << "Creating Reg ExtModule with width=" << dataWidth << "\n");

  auto &library = ecmt2::ModuleLibrary::getInstance();

  // Build parameters for the FIRRTLReg module
  llvm::StringMap<int64_t> params;
  params["width"] = dataWidth;
  params["init"] = 0;

  // Get the actual FIRRTL module name that will be generated
  std::string actualModuleName;
  if (failed(library.getActualModuleName("FIRRTLReg", params, actualModuleName))) {
    LLVM_DEBUG(llvm::dbgs() << "Warning: Could not get actual module name for FIRRTLReg\n");
    actualModuleName = "Reg_width" + std::to_string(dataWidth) + "_init0";
  }

  OpBuilder builder(circuit_.getContext());
  Location loc = circuit_.getLoc();

  // Insert the FIRRTL module from the library
  // The builder should insert at the top-level module (outside cmt2.circuit)
  auto topModule = circuit_->getParentOfType<mlir::ModuleOp>();
  builder.setInsertionPointToEnd(topModule.getBody());

  std::string insertedModuleName;
  if (failed(library.insertModuleIntoCircuit("FIRRTLReg", params, builder, loc, insertedModuleName))) {
    LLVM_DEBUG(llvm::dbgs() << "Warning: Failed to insert FIRRTLReg from library\n");
  }

  // Create the CMT2 name for this module
  std::string cmt2ModuleName = "Reg_w" + std::to_string(dataWidth);

  // Now create the ExtModuleFirrtlOp binding in the CMT2 circuit
  builder.setInsertionPointToEnd(&circuit_.getBody().front());

  auto dataType = firrtl::UIntType::get(builder.getContext(), dataWidth);
  auto clockType = firrtl::ClockType::get(builder.getContext());
  auto resetType = firrtl::UIntType::get(builder.getContext(), 1);

  // Create ExtModuleFirrtlOp with the actual FIRRTL module name
  // Start with empty argNames - we'll add them as we add block arguments
  auto extMod = builder.create<ExtModuleFirrtlOp>(
      loc, builder.getStringAttr(cmt2ModuleName),
      FlatSymbolRefAttr::get(builder.getContext(),
          insertedModuleName.empty() ? actualModuleName : insertedModuleName),
      builder.getArrayAttr({}));

  // Create body block
  Block *body = new Block();
  extMod.getBody().push_back(body);

  // Add block arguments for clock and reset
  auto clkArg = body->addArgument(clockType, loc);
  auto rstArg = body->addArgument(resetType, loc);

  // Update argNames attribute
  extMod.setArgNamesAttr(builder.getArrayAttr({
      builder.getStringAttr("clk"),
      builder.getStringAttr("rst")
  }));

  OpBuilder bodyBuilder(body, body->begin());

  // Bind clock using block argument Value
  bodyBuilder.create<BindBareOp>(loc, clkArg,
      FlatSymbolRefAttr::get(builder.getContext(), "clk"));

  // Bind reset using block argument Value
  bodyBuilder.create<BindBareOp>(loc, rstArg,
      FlatSymbolRefAttr::get(builder.getContext(), "rst"));

  // Bind value method: read() -> data
  auto readType = bodyBuilder.getFunctionType({}, {dataType});
  auto emptyArrayAttr = bodyBuilder.getArrayAttr({});
  bodyBuilder.create<BindValueOp>(
      loc,
      bodyBuilder.getStringAttr("read"),
      TypeAttr::get(readType),
      bodyBuilder.getStringAttr("read_ready"),  // ready port
      emptyArrayAttr,                            // arg names (none for read)
      bodyBuilder.getArrayAttr({bodyBuilder.getStringAttr("read_data")}),  // body result names
      emptyArrayAttr,                            // arg_attrs
      emptyArrayAttr);                           // res_attrs

  // Bind method: write(data)
  auto writeType = bodyBuilder.getFunctionType({dataType}, {});
  bodyBuilder.create<BindMethodOp>(
      loc,
      bodyBuilder.getStringAttr("write"),
      TypeAttr::get(writeType),
      bodyBuilder.getStringAttr("write_enable"),  // enable port
      bodyBuilder.getStringAttr("write_ready"),   // ready port
      bodyBuilder.getArrayAttr({bodyBuilder.getStringAttr("write_data")}),  // arg names
      emptyArrayAttr,                              // body result names (none for write)
      emptyArrayAttr,                              // arg_attrs
      emptyArrayAttr);                             // res_attrs

  // Add scheduling constraint: read must sequence before write
  // Format: sequenceBefore = [[@read, @write]]
  extMod->setAttr("sequenceBefore", builder.getArrayAttr({
      builder.getArrayAttr({
          FlatSymbolRefAttr::get(builder.getContext(), "read"),
          FlatSymbolRefAttr::get(builder.getContext(), "write")
      })
  }));

  extModuleCache_[cmt2ModuleName] = extMod;
  return extMod;
}

//===----------------------------------------------------------------------===//
// ShiftReg Module - Compound module built from Reg instances
//===----------------------------------------------------------------------===//

cmt2::ModuleOp ModuleGenerator::getOrCreateShiftRegModule(unsigned dataWidth,
                                                           unsigned depth) {
  std::string name =
      "ShiftReg_w" + std::to_string(dataWidth) + "_d" + std::to_string(depth);
  if (auto existing = findModule(name))
    return existing;

  return createShiftRegModule(name, dataWidth, depth);
}

cmt2::ModuleOp ModuleGenerator::createShiftRegModule(StringRef name,
                                                      unsigned dataWidth,
                                                      unsigned depth) {
  LLVM_DEBUG(llvm::dbgs() << "Creating ShiftReg module: " << name
                          << " (width=" << dataWidth << ", depth=" << depth
                          << ")\n");

  OpBuilder builder(circuit_.getContext());
  builder.setInsertionPointToEnd(&circuit_.getBody().front());

  Location loc = circuit_.getLoc();
  auto dataType = firrtl::UIntType::get(builder.getContext(), dataWidth);
  auto boolType = firrtl::UIntType::get(builder.getContext(), 1);
  auto clockType = firrtl::ClockType::get(builder.getContext());

  // Create module with clock and reset arguments
  SmallVector<Attribute> argNameAttrs = {builder.getStringAttr("clk"),
                                          builder.getStringAttr("rst")};

  auto mod = builder.create<cmt2::ModuleOp>(
      loc, builder.getStringAttr(name), builder.getArrayAttr(argNameAttrs));

  // Create module body with arguments
  Block *body = new Block();
  body->addArgument(clockType, loc);
  body->addArgument(boolType, loc);
  mod.getBody().push_back(body);

  OpBuilder modBuilder(mod.getContext());
  modBuilder.setInsertionPointToStart(body);

  // Get or create Reg modules using ModuleLibrary
  auto regMod = getOrCreateRegModule(dataWidth);
  auto validRegMod = getOrCreateRegModule(1);

  Value clk = body->getArgument(0);
  Value rst = body->getArgument(1);

  // Create register instances for each stage
  SmallVector<InstanceOp> dataRegs;
  SmallVector<InstanceOp> validRegs;

  for (unsigned i = 0; i < depth; ++i) {
    std::string dataInstName = "data_" + std::to_string(i);
    std::string validInstName = "valid_" + std::to_string(i);

    auto dataInst = modBuilder.create<InstanceOp>(
        loc, modBuilder.getStringAttr(dataInstName), ValueRange{clk, rst},
        FlatSymbolRefAttr::get(modBuilder.getContext(), regMod.getSymName()),
        /*interface_binds=*/nullptr);

    auto validInst = modBuilder.create<InstanceOp>(
        loc, modBuilder.getStringAttr(validInstName), ValueRange{clk, rst},
        FlatSymbolRefAttr::get(modBuilder.getContext(), validRegMod.getSymName()),
        /*interface_binds=*/nullptr);

    dataRegs.push_back(dataInst);
    validRegs.push_back(validInst);
  }

  // Add value method: valid() -> bool
  auto validValType = modBuilder.getFunctionType({}, {boolType});
  auto validVal = modBuilder.create<ValueOp>(
      loc, modBuilder.getStringAttr("valid"), TypeAttr::get(validValType),
      modBuilder.getStrArrayAttr({}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);

  // Guard: always true
  {
    Region &guardRegion = validVal.getGuard();
    Block *guardBlock = new Block();
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    auto trueVal =
        guardBuilder.create<firrtl::ConstantOp>(loc, boolType, APInt(1, 1));
    guardBuilder.create<ReturnOp>(loc, ValueRange{trueVal});
  }

  // Body: read valid from last stage
  {
    Region &bodyRegion = validVal.getBody();
    Block *bodyBlock = new Block();
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

    // Call the last valid register's read method
    auto lastValidInst = validRegs[depth - 1];
    auto callOp = bodyBuilder.create<CallOp>(
        loc, TypeRange{boolType}, ValueRange{},
        FlatSymbolRefAttr::get(bodyBuilder.getContext(),
                               lastValidInst.getSymName()),
        FlatSymbolRefAttr::get(bodyBuilder.getContext(), "read"),
        /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr, /*arg_timing=*/nullptr,
        /*result_timing=*/nullptr);
    bodyBuilder.create<ReturnOp>(loc, callOp.getOutputs());
  }

  // Add value method: peek() -> data
  auto peekValType = modBuilder.getFunctionType({}, {dataType});
  auto peekVal = modBuilder.create<ValueOp>(
      loc, modBuilder.getStringAttr("peek"), TypeAttr::get(peekValType),
      modBuilder.getStrArrayAttr({}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);

  // Guard: always true
  {
    Region &guardRegion = peekVal.getGuard();
    Block *guardBlock = new Block();
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    auto trueVal =
        guardBuilder.create<firrtl::ConstantOp>(loc, boolType, APInt(1, 1));
    guardBuilder.create<ReturnOp>(loc, ValueRange{trueVal});
  }

  // Body: read data from last stage
  {
    Region &bodyRegion = peekVal.getBody();
    Block *bodyBlock = new Block();
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

    auto lastDataInst = dataRegs[depth - 1];
    auto callOp = bodyBuilder.create<CallOp>(
        loc, TypeRange{dataType}, ValueRange{},
        FlatSymbolRefAttr::get(bodyBuilder.getContext(),
                               lastDataInst.getSymName()),
        FlatSymbolRefAttr::get(bodyBuilder.getContext(), "read"),
        /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr, /*arg_timing=*/nullptr,
        /*result_timing=*/nullptr);
    bodyBuilder.create<ReturnOp>(loc, callOp.getOutputs());
  }

  // Add method: write(data)
  auto writeType = modBuilder.getFunctionType({dataType}, {});
  auto writeMethod = modBuilder.create<MethodOp>(
      loc, modBuilder.getStringAttr("write"), TypeAttr::get(writeType),
      modBuilder.getStrArrayAttr({"data"}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);

  // Guard: always true
  {
    Region &guardRegion = writeMethod.getGuard();
    Block *guardBlock = new Block();
    guardBlock->addArgument(dataType, loc);
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    auto trueVal =
        guardBuilder.create<firrtl::ConstantOp>(loc, boolType, APInt(1, 1));
    guardBuilder.create<ReturnOp>(loc, ValueRange{trueVal});
  }

  // Body: write to first stage, set valid
  {
    Region &bodyRegion = writeMethod.getBody();
    Block *bodyBlock = new Block();
    bodyBlock->addArgument(dataType, loc);
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

    Value data = bodyBlock->getArgument(0);

    // Write data to first stage
    auto firstDataInst = dataRegs[0];
    bodyBuilder.create<CallOp>(
        loc, TypeRange{}, ValueRange{data},
        FlatSymbolRefAttr::get(bodyBuilder.getContext(),
                               firstDataInst.getSymName()),
        FlatSymbolRefAttr::get(bodyBuilder.getContext(), "write"),
        /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr, /*arg_timing=*/nullptr,
        /*result_timing=*/nullptr);

    // Set first valid to true
    auto trueVal =
        bodyBuilder.create<firrtl::ConstantOp>(loc, boolType, APInt(1, 1));
    auto firstValidInst = validRegs[0];
    bodyBuilder.create<CallOp>(
        loc, TypeRange{}, ValueRange{trueVal},
        FlatSymbolRefAttr::get(bodyBuilder.getContext(),
                               firstValidInst.getSymName()),
        FlatSymbolRefAttr::get(bodyBuilder.getContext(), "write"),
        /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr, /*arg_timing=*/nullptr,
        /*result_timing=*/nullptr);

    bodyBuilder.create<ReturnOp>(loc, ValueRange{});
  }

  moduleCache_[name] = mod;
  return mod;
}

//===----------------------------------------------------------------------===//
// FIFO Module
//===----------------------------------------------------------------------===//

Cmt2ModuleLike ModuleGenerator::getOrCreateFIFOModule(unsigned dataWidth,
                                                       unsigned depth) {
  std::string name =
      "FIFO_w" + std::to_string(dataWidth) + "_d" + std::to_string(depth);
  if (auto existing = findModule(name))
    return existing;

  return createFIFOModule(name, dataWidth, depth);
}

Cmt2ModuleLike ModuleGenerator::createFIFOModule(StringRef name,
                                                  unsigned dataWidth,
                                                  unsigned depth) {
  LLVM_DEBUG(llvm::dbgs() << "Creating FIFO module: " << name
                          << " (width=" << dataWidth << ", depth=" << depth
                          << ")\n");

  // Create a simple shift-register FIFO of the requested depth.
  //
  // Semantics:
  // - notEmpty/valid: true when there is at least 1 element
  // - notFull/ready: true when there is at least 1 free slot
  // - write/enq: enqueue (guarded by notFull)
  // - deq: dequeue and return front (guarded by notEmpty)
  // - peek/first: read front (not guarded here; caller should check valid)
  //
  // Implementation uses depth Reg<data> and Reg<i1> instances, and keeps
  // entries packed at the front by shifting on deq and placing new entries
  // in the first empty slot on enq.
  if (depth == 0)
    depth = 1;

  OpBuilder builder(circuit_.getContext());
  builder.setInsertionPointToEnd(&circuit_.getBody().front());

  Location loc = circuit_.getLoc();
  auto dataType = firrtl::UIntType::get(builder.getContext(), dataWidth);
  auto boolType = firrtl::UIntType::get(builder.getContext(), 1);
  auto clockType = firrtl::ClockType::get(builder.getContext());

  // Create module with clock and reset arguments
  SmallVector<Attribute> argNameAttrs = {builder.getStringAttr("clk"),
                                          builder.getStringAttr("rst")};

  auto mod = builder.create<cmt2::ModuleOp>(
      loc, builder.getStringAttr(name), builder.getArrayAttr(argNameAttrs));

  // Create module body with arguments
  Block *body = new Block();
  body->addArgument(clockType, loc);
  body->addArgument(boolType, loc);
  mod.getBody().push_back(body);

  OpBuilder modBuilder(mod.getContext());
  modBuilder.setInsertionPointToStart(body);
  // Leave body result names empty; Cmt2FunctionLike provides a default (resN)
  // based on the function type when names are unspecified.

  // Get Reg modules from ModuleLibrary
  auto regMod = getOrCreateRegModule(dataWidth);
  auto validRegMod = getOrCreateRegModule(1);

  Value clk = body->getArgument(0);
  Value rst = body->getArgument(1);

  // Create data/valid registers for each FIFO slot.
  SmallVector<InstanceOp> dataRegs;
  SmallVector<InstanceOp> validRegs;
  dataRegs.reserve(depth);
  validRegs.reserve(depth);

  for (unsigned i = 0; i < depth; ++i) {
    std::string dataInstName = "data_" + std::to_string(i);
    std::string validInstName = "valid_" + std::to_string(i);

    auto dataInst = modBuilder.create<InstanceOp>(
        loc, modBuilder.getStringAttr(dataInstName), ValueRange{clk, rst},
        FlatSymbolRefAttr::get(modBuilder.getContext(), regMod.getSymName()),
        /*interface_binds=*/nullptr);

    auto validInst = modBuilder.create<InstanceOp>(
        loc, modBuilder.getStringAttr(validInstName), ValueRange{clk, rst},
        FlatSymbolRefAttr::get(modBuilder.getContext(), validRegMod.getSymName()),
        /*interface_binds=*/nullptr);

    dataRegs.push_back(dataInst);
    validRegs.push_back(validInst);
  }

  auto constTrue = [&](OpBuilder &b) -> Value {
    return b.create<firrtl::ConstantOp>(loc, boolType, APInt(1, 1));
  };
  auto constFalse = [&](OpBuilder &b) -> Value {
    return b.create<firrtl::ConstantOp>(loc, boolType, APInt(1, 0));
  };

  auto readReg = [&](OpBuilder &b, InstanceOp inst, Type resultTy) -> Value {
    auto callOp = b.create<CallOp>(
        loc, TypeRange{resultTy}, ValueRange{},
        FlatSymbolRefAttr::get(b.getContext(), inst.getSymName()),
        FlatSymbolRefAttr::get(b.getContext(), "read"),
        /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr, /*arg_timing=*/nullptr,
        /*result_timing=*/nullptr);
    return callOp.getOutputs()[0];
  };

  auto writeReg = [&](OpBuilder &b, InstanceOp inst, Value v) {
    b.create<CallOp>(
        loc, TypeRange{}, ValueRange{v},
        FlatSymbolRefAttr::get(b.getContext(), inst.getSymName()),
        FlatSymbolRefAttr::get(b.getContext(), "write"),
        /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr, /*arg_timing=*/nullptr,
        /*result_timing=*/nullptr);
  };

  // Helper: build a boolean (UInt<1>) for notEmpty and notFull.
  auto buildNotEmpty = [&](OpBuilder &b) -> Value {
    return readReg(b, validRegs[0], boolType);
  };
  auto buildNotFull = [&](OpBuilder &b) -> Value {
    Value lastValid = readReg(b, validRegs[depth - 1], boolType);
    return b.create<firrtl::NotPrimOp>(loc, lastValid);
  };

  // Value: notEmpty() -> bool
  auto notEmptyType = modBuilder.getFunctionType({}, {boolType});
  auto notEmptyVal = modBuilder.create<ValueOp>(
      loc, modBuilder.getStringAttr("notEmpty"), TypeAttr::get(notEmptyType),
      modBuilder.getStrArrayAttr({}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
  {
    Region &guardRegion = notEmptyVal.getGuard();
    Block *guardBlock = new Block();
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    guardBuilder.create<ReturnOp>(loc, ValueRange{constTrue(guardBuilder)});
  }
  {
    Region &bodyRegion = notEmptyVal.getBody();
    Block *bodyBlock = new Block();
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());
    bodyBuilder.create<ReturnOp>(loc, ValueRange{buildNotEmpty(bodyBuilder)});
  }

  // Value: notFull() -> bool
  auto notFullType = modBuilder.getFunctionType({}, {boolType});
  auto notFullVal = modBuilder.create<ValueOp>(
      loc, modBuilder.getStringAttr("notFull"), TypeAttr::get(notFullType),
      modBuilder.getStrArrayAttr({}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
  {
    Region &guardRegion = notFullVal.getGuard();
    Block *guardBlock = new Block();
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    guardBuilder.create<ReturnOp>(loc, ValueRange{constTrue(guardBuilder)});
  }
  {
    Region &bodyRegion = notFullVal.getBody();
    Block *bodyBlock = new Block();
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());
    bodyBuilder.create<ReturnOp>(loc, ValueRange{buildNotFull(bodyBuilder)});
  }

  // Add value: first() -> data
  auto firstType = modBuilder.getFunctionType({}, {dataType});
  auto firstVal = modBuilder.create<ValueOp>(
      loc, modBuilder.getStringAttr("first"), TypeAttr::get(firstType),
      modBuilder.getStrArrayAttr({}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);

  // Guard: always true
  {
    Region &guardRegion = firstVal.getGuard();
    Block *guardBlock = new Block();
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    auto trueVal =
        guardBuilder.create<firrtl::ConstantOp>(loc, boolType, APInt(1, 1));
    guardBuilder.create<ReturnOp>(loc, ValueRange{trueVal});
  }

  // Body: read data_reg
  {
    Region &bodyRegion = firstVal.getBody();
    Block *bodyBlock = new Block();
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());
    bodyBuilder.create<ReturnOp>(loc,
                                 ValueRange{readReg(bodyBuilder, dataRegs[0], dataType)});
  }

  // Add method: enq(data)
  auto enqType = modBuilder.getFunctionType({dataType}, {});
  auto enqMethod = modBuilder.create<MethodOp>(
      loc, modBuilder.getStringAttr("enq"), TypeAttr::get(enqType),
      modBuilder.getStrArrayAttr({"data"}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);

  // Guard: notFull
  {
    Region &guardRegion = enqMethod.getGuard();
    Block *guardBlock = new Block();
    guardBlock->addArgument(dataType, loc);
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    guardBuilder.create<ReturnOp>(loc, ValueRange{buildNotFull(guardBuilder)});
  }

  // Body: enqueue into first empty slot
  {
    Region &bodyRegion = enqMethod.getBody();
    Block *bodyBlock = new Block();
    bodyBlock->addArgument(dataType, loc);
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

    Value data = bodyBlock->getArgument(0);

    SmallVector<Value> validVals;
    SmallVector<Value> dataVals;
    validVals.reserve(depth);
    dataVals.reserve(depth);
    for (unsigned i = 0; i < depth; ++i) {
      validVals.push_back(readReg(bodyBuilder, validRegs[i], boolType));
      dataVals.push_back(readReg(bodyBuilder, dataRegs[i], dataType));
    }

    Value prefixFull = constTrue(bodyBuilder);
    Value one = constTrue(bodyBuilder);

    for (unsigned i = 0; i < depth; ++i) {
      Value isEmpty = bodyBuilder.create<firrtl::NotPrimOp>(loc, validVals[i]);
      Value select = bodyBuilder.create<firrtl::AndPrimOp>(loc, prefixFull, isEmpty);

      Value nextData =
          bodyBuilder.create<firrtl::MuxPrimOp>(loc, select, data, dataVals[i]);
      Value nextValid =
          bodyBuilder.create<firrtl::MuxPrimOp>(loc, select, one, validVals[i]);

      writeReg(bodyBuilder, dataRegs[i], nextData);
      writeReg(bodyBuilder, validRegs[i], nextValid);

      prefixFull = bodyBuilder.create<firrtl::AndPrimOp>(loc, prefixFull, validVals[i]);
    }

    bodyBuilder.create<ReturnOp>(loc, ValueRange{});
  }

  // Add method: deq() -> data
  auto deqType = modBuilder.getFunctionType({}, {dataType});
  auto deqMethod = modBuilder.create<MethodOp>(
      loc, modBuilder.getStringAttr("deq"), TypeAttr::get(deqType),
      modBuilder.getStrArrayAttr({}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);

  // Guard: notEmpty
  {
    Region &guardRegion = deqMethod.getGuard();
    Block *guardBlock = new Block();
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    guardBuilder.create<ReturnOp>(loc, ValueRange{buildNotEmpty(guardBuilder)});
  }

  // Body: read front, shift, clear last valid
  {
    Region &bodyRegion = deqMethod.getBody();
    Block *bodyBlock = new Block();
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

    SmallVector<Value> validVals;
    SmallVector<Value> dataVals;
    validVals.reserve(depth);
    dataVals.reserve(depth);
    for (unsigned i = 0; i < depth; ++i) {
      validVals.push_back(readReg(bodyBuilder, validRegs[i], boolType));
      dataVals.push_back(readReg(bodyBuilder, dataRegs[i], dataType));
    }

    Value out = dataVals[0];

    for (unsigned i = 0; i + 1 < depth; ++i) {
      writeReg(bodyBuilder, dataRegs[i], dataVals[i + 1]);
      writeReg(bodyBuilder, validRegs[i], validVals[i + 1]);
    }
    writeReg(bodyBuilder, validRegs[depth - 1], constFalse(bodyBuilder));

    bodyBuilder.create<ReturnOp>(loc, ValueRange{out});
  }

  // -----------------------------------------------------------------------
  // Compatibility aliases for TokenRTLGen storage interface:
  //  - valid()  == notEmpty()
  //  - ready()  == notFull()
  //  - peek()   == first()
  //  - write()  == enq()
  // -----------------------------------------------------------------------

  // Value: valid() -> bool
  auto validType = modBuilder.getFunctionType({}, {boolType});
  auto validVal = modBuilder.create<ValueOp>(
      loc, modBuilder.getStringAttr("valid"), TypeAttr::get(validType),
      modBuilder.getStrArrayAttr({}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
  {
    Region &guardRegion = validVal.getGuard();
    Block *guardBlock = new Block();
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    guardBuilder.create<ReturnOp>(loc, ValueRange{constTrue(guardBuilder)});
  }
  {
    Region &bodyRegion = validVal.getBody();
    Block *bodyBlock = new Block();
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());
    bodyBuilder.create<ReturnOp>(loc, ValueRange{buildNotEmpty(bodyBuilder)});
  }

  // Value: ready() -> bool
  auto readyType = modBuilder.getFunctionType({}, {boolType});
  auto readyVal = modBuilder.create<ValueOp>(
      loc, modBuilder.getStringAttr("ready"), TypeAttr::get(readyType),
      modBuilder.getStrArrayAttr({}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
  {
    Region &guardRegion = readyVal.getGuard();
    Block *guardBlock = new Block();
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    guardBuilder.create<ReturnOp>(loc, ValueRange{constTrue(guardBuilder)});
  }
  {
    Region &bodyRegion = readyVal.getBody();
    Block *bodyBlock = new Block();
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());
    bodyBuilder.create<ReturnOp>(loc, ValueRange{buildNotFull(bodyBuilder)});
  }

  // Value: peek() -> data
  auto peekType = modBuilder.getFunctionType({}, {dataType});
  auto peekVal = modBuilder.create<ValueOp>(
      loc, modBuilder.getStringAttr("peek"), TypeAttr::get(peekType),
      modBuilder.getStrArrayAttr({}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
  {
    Region &guardRegion = peekVal.getGuard();
    Block *guardBlock = new Block();
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    guardBuilder.create<ReturnOp>(loc, ValueRange{constTrue(guardBuilder)});
  }
  {
    Region &bodyRegion = peekVal.getBody();
    Block *bodyBlock = new Block();
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());
    bodyBuilder.create<ReturnOp>(loc,
                                 ValueRange{readReg(bodyBuilder, dataRegs[0], dataType)});
  }

  // Method: write(data)
  auto writeType = modBuilder.getFunctionType({dataType}, {});
  auto writeMethod = modBuilder.create<MethodOp>(
      loc, modBuilder.getStringAttr("write"), TypeAttr::get(writeType),
      modBuilder.getStrArrayAttr({"data"}), modBuilder.getArrayAttr({}),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
  {
    Region &guardRegion = writeMethod.getGuard();
    Block *guardBlock = new Block();
    guardBlock->addArgument(dataType, loc);
    guardRegion.push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());
    guardBuilder.create<ReturnOp>(loc, ValueRange{buildNotFull(guardBuilder)});
  }
  {
    Region &bodyRegion = writeMethod.getBody();
    Block *bodyBlock = new Block();
    bodyBlock->addArgument(dataType, loc);
    bodyRegion.push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

    Value data = bodyBlock->getArgument(0);

    SmallVector<Value> validVals;
    SmallVector<Value> dataVals;
    validVals.reserve(depth);
    dataVals.reserve(depth);
    for (unsigned i = 0; i < depth; ++i) {
      validVals.push_back(readReg(bodyBuilder, validRegs[i], boolType));
      dataVals.push_back(readReg(bodyBuilder, dataRegs[i], dataType));
    }

    Value prefixFull = constTrue(bodyBuilder);
    Value one = constTrue(bodyBuilder);

    for (unsigned i = 0; i < depth; ++i) {
      Value isEmpty = bodyBuilder.create<firrtl::NotPrimOp>(loc, validVals[i]);
      Value select =
          bodyBuilder.create<firrtl::AndPrimOp>(loc, prefixFull, isEmpty);

      Value nextData =
          bodyBuilder.create<firrtl::MuxPrimOp>(loc, select, data, dataVals[i]);
      Value nextValid =
          bodyBuilder.create<firrtl::MuxPrimOp>(loc, select, one, validVals[i]);

      writeReg(bodyBuilder, dataRegs[i], nextData);
      writeReg(bodyBuilder, validRegs[i], nextValid);

      prefixFull =
          bodyBuilder.create<firrtl::AndPrimOp>(loc, prefixFull, validVals[i]);
    }

    bodyBuilder.create<ReturnOp>(loc, ValueRange{});
  }

  moduleCache_[name] = mod;
  return mod;
}

//===----------------------------------------------------------------------===//
// Instance and Call Creation
//===----------------------------------------------------------------------===//

InstanceOp ModuleGenerator::createStorageInstance(Location loc,
                                                   StringRef instanceName,
                                                   Cmt2ModuleLike storageModule,
                                                   Value clk, Value rst,
                                                   OpBuilder &builder) {
  return builder.create<InstanceOp>(
      loc, builder.getStringAttr(instanceName), ValueRange{clk, rst},
      FlatSymbolRefAttr::get(builder.getContext(), storageModule.moduleNameAttr()),
      /*interface_binds=*/nullptr);
}

SmallVector<Value>
ModuleGenerator::createStorageCall(Location loc, InstanceOp instance,
                                    StringRef methodName, ValueRange args,
                                    OpBuilder &builder) {
  // Look up the storage module to get method return types
  auto modRef = instance.getModuleNameAttr();

  // Try finding as regular module first
  if (auto mod = findModule(modRef.getValue())) {
    // Find the method/value in the module
    SmallVector<Type> resultTypes;
    for (auto &op : mod.getBody().front()) {
      if (auto valOp = dyn_cast<ValueOp>(op)) {
        if (valOp.getSymName() == methodName) {
          auto funcType = cast<FunctionType>(valOp.getFunctionType());
          resultTypes.append(funcType.getResults().begin(),
                             funcType.getResults().end());
          break;
        }
      } else if (auto methodOp = dyn_cast<MethodOp>(op)) {
        if (methodOp.getSymName() == methodName) {
          auto funcType = cast<FunctionType>(methodOp.getFunctionType());
          resultTypes.append(funcType.getResults().begin(),
                             funcType.getResults().end());
          break;
        }
      }
    }

    auto callOp = builder.create<CallOp>(
        loc, TypeRange(resultTypes), args,
        FlatSymbolRefAttr::get(builder.getContext(), instance.getSymName()),
        FlatSymbolRefAttr::get(builder.getContext(), methodName),
        /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr, /*arg_timing=*/nullptr,
        /*result_timing=*/nullptr);

    return SmallVector<Value>(callOp.getOutputs());
  }

  // Try finding as external FIRRTL module
  if (auto extMod = findExtModule(modRef.getValue())) {
    // Find the binding in the external module
    SmallVector<Type> resultTypes;
    for (auto &op : extMod.getBody().front()) {
      if (auto bindVal = dyn_cast<BindValueOp>(op)) {
        if (bindVal.getName() == methodName) {
          auto funcType = bindVal.getFunctionType();
          resultTypes.append(funcType.getResults().begin(),
                             funcType.getResults().end());
          break;
        }
      } else if (auto bindMethod = dyn_cast<BindMethodOp>(op)) {
        if (bindMethod.getName() == methodName) {
          auto funcType = bindMethod.getFunctionType();
          resultTypes.append(funcType.getResults().begin(),
                             funcType.getResults().end());
          break;
        }
      }
    }

    auto callOp = builder.create<CallOp>(
        loc, TypeRange(resultTypes), args,
        FlatSymbolRefAttr::get(builder.getContext(), instance.getSymName()),
        FlatSymbolRefAttr::get(builder.getContext(), methodName),
        /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr, /*arg_timing=*/nullptr,
        /*result_timing=*/nullptr);

    return SmallVector<Value>(callOp.getOutputs());
  }

  LLVM_DEBUG(llvm::dbgs() << "Could not find module: " << modRef.getValue()
                          << "\n");
  return {};
}

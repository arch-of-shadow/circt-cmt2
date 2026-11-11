//===- OpHandlerRegistry.cpp - Operation Handler Registry ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the OpHandlerRegistry and built-in handlers for the
// CMT2 interpreter.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Interpreter/OpHandlerRegistry.h"
#include "circt/Dialect/Cmt2/Interpreter/ModuleInterpreterRegistry.h"
#include "circt/Dialect/Cmt2/Interpreter/StateManager.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/HW/HWOps.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-op-handler"

using namespace circt;
using namespace cmt2;
using namespace interp;

//===----------------------------------------------------------------------===//
// OpContext Implementation
//===----------------------------------------------------------------------===//

InterpValue OpContext::getValue(mlir::Value v) {
  // Check if we have a cached value
  auto it = valueMap.find(v);
  if (it != valueMap.end())
    return it->second;

  // For block arguments, return a default value
  if (auto arg = mlir::dyn_cast<mlir::BlockArgument>(v)) {
    unsigned width = 32;
    if (auto intType = mlir::dyn_cast<mlir::IntegerType>(v.getType()))
      width = intType.getWidth();
    return llvm::APInt(width, 0);
  }

  // Default width
  unsigned width = 32;
  if (auto intType = mlir::dyn_cast<mlir::IntegerType>(v.getType()))
    width = intType.getWidth();
  return llvm::APInt(width, 0);
}

void OpContext::setValue(mlir::Value v, const InterpValue &val) {
  valueMap[v] = val;
}

//===----------------------------------------------------------------------===//
// OpHandlerRegistry Implementation
//===----------------------------------------------------------------------===//

OpHandlerRegistry::OpHandlerRegistry() {
  // Register dialect registrars for lazy loading
  dialectRegistrars_["hw"] = registerHWHandlers;
  dialectRegistrars_["comb"] = registerCombHandlers;
  dialectRegistrars_["firrtl"] = registerFIRRTLHandlers;
  dialectRegistrars_["cmt2"] = registerCMT2Handlers;
}

void OpHandlerRegistry::registerHandlerByName(llvm::StringRef opName,
                                              OpHandler handler) {
  namedHandlers_[opName] = std::move(handler);
}

void OpHandlerRegistry::registerDialect(
    llvm::StringRef dialectName,
    std::function<void(OpHandlerRegistry &)> registrar) {
  dialectRegistrars_[dialectName] = std::move(registrar);
}

std::optional<InterpValue> OpHandlerRegistry::execute(mlir::Operation *op,
                                                       OpContext &ctx) {
  // Get operation's dialect and ensure it's registered
  llvm::StringRef dialectName = op->getDialect()->getNamespace();
  if (!registeredDialects_.count(dialectName)) {
    auto it = dialectRegistrars_.find(dialectName);
    if (it != dialectRegistrars_.end()) {
      it->second(*this);
      registeredDialects_.insert(dialectName);
    }
  }

  // Try TypeID-based handler first
  mlir::TypeID typeID = op->getName().getTypeID();
  auto it = handlers_.find(typeID);
  if (it != handlers_.end()) {
    LLVM_DEBUG(llvm::dbgs() << "OpHandlerRegistry: executing handler for "
                            << op->getName() << "\n");
    return it->second(op, ctx);
  }

  // Try name-based handler
  auto nameIt = namedHandlers_.find(op->getName().getStringRef());
  if (nameIt != namedHandlers_.end()) {
    LLVM_DEBUG(llvm::dbgs() << "OpHandlerRegistry: executing named handler for "
                            << op->getName() << "\n");
    return nameIt->second(op, ctx);
  }

  LLVM_DEBUG(llvm::dbgs() << "OpHandlerRegistry: no handler for "
                          << op->getName() << "\n");
  return std::nullopt;
}

bool OpHandlerRegistry::hasHandler(mlir::Operation *op) const {
  mlir::TypeID typeID = op->getName().getTypeID();
  if (handlers_.count(typeID))
    return true;
  if (namedHandlers_.count(op->getName().getStringRef()))
    return true;
  return false;
}

void OpHandlerRegistry::registerBuiltinHandlers() {
  registerHWHandlers(*this);
  registerCombHandlers(*this);
  registerFIRRTLHandlers(*this);
  registerCMT2Handlers(*this);

  registeredDialects_.insert("hw");
  registeredDialects_.insert("comb");
  registeredDialects_.insert("firrtl");
  registeredDialects_.insert("cmt2");
}

//===----------------------------------------------------------------------===//
// HW Dialect Handlers
//===----------------------------------------------------------------------===//

void circt::cmt2::interp::registerHWHandlers(OpHandlerRegistry &registry) {
  registry.registerHandler<hw::ConstantOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto constOp = mlir::cast<hw::ConstantOp>(op);
        InterpValue value = constOp.getValue();
        if (op->getNumResults() > 0)
          ctx.setValue(op->getResult(0), value);
        return value;
      });
}

//===----------------------------------------------------------------------===//
// Comb Dialect Handlers
//===----------------------------------------------------------------------===//

void circt::cmt2::interp::registerCombHandlers(OpHandlerRegistry &registry) {
  // AddOp
  registry.registerHandler<comb::AddOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto addOp = mlir::cast<comb::AddOp>(op);
        InterpValue lhs = ctx.getValue(addOp.getOperand(0));
        InterpValue rhs = ctx.getValue(addOp.getOperand(1));
        InterpValue result = lhs + rhs;
        ctx.setValue(addOp.getResult(), result);
        return result;
      });

  // SubOp
  registry.registerHandler<comb::SubOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto subOp = mlir::cast<comb::SubOp>(op);
        InterpValue lhs = ctx.getValue(subOp.getOperand(0));
        InterpValue rhs = ctx.getValue(subOp.getOperand(1));
        InterpValue result = lhs - rhs;
        ctx.setValue(subOp.getResult(), result);
        return result;
      });

  // AndOp
  registry.registerHandler<comb::AndOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto andOp = mlir::cast<comb::AndOp>(op);
        InterpValue result = ctx.getValue(andOp.getOperand(0));
        for (unsigned i = 1; i < andOp.getNumOperands(); ++i)
          result &= ctx.getValue(andOp.getOperand(i));
        ctx.setValue(andOp.getResult(), result);
        return result;
      });

  // OrOp
  registry.registerHandler<comb::OrOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto orOp = mlir::cast<comb::OrOp>(op);
        InterpValue result = ctx.getValue(orOp.getOperand(0));
        for (unsigned i = 1; i < orOp.getNumOperands(); ++i)
          result |= ctx.getValue(orOp.getOperand(i));
        ctx.setValue(orOp.getResult(), result);
        return result;
      });

  // XorOp
  registry.registerHandler<comb::XorOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto xorOp = mlir::cast<comb::XorOp>(op);
        InterpValue result = ctx.getValue(xorOp.getOperand(0));
        for (unsigned i = 1; i < xorOp.getNumOperands(); ++i)
          result ^= ctx.getValue(xorOp.getOperand(i));
        ctx.setValue(xorOp.getResult(), result);
        return result;
      });

  // ICmpOp
  registry.registerHandler<comb::ICmpOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto icmpOp = mlir::cast<comb::ICmpOp>(op);
        InterpValue lhs = ctx.getValue(icmpOp.getOperand(0));
        InterpValue rhs = ctx.getValue(icmpOp.getOperand(1));
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
        ctx.setValue(icmpOp.getResult(), resultVal);
        return resultVal;
      });

  // MuxOp
  registry.registerHandler<comb::MuxOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto muxOp = mlir::cast<comb::MuxOp>(op);
        InterpValue cond = ctx.getValue(muxOp.getCond());
        InterpValue result = cond != 0 ? ctx.getValue(muxOp.getTrueValue())
                                       : ctx.getValue(muxOp.getFalseValue());
        ctx.setValue(muxOp.getResult(), result);
        return result;
      });
}

//===----------------------------------------------------------------------===//
// FIRRTL Dialect Handlers
//===----------------------------------------------------------------------===//

void circt::cmt2::interp::registerFIRRTLHandlers(OpHandlerRegistry &registry) {
  // ConstantOp
  registry.registerHandler<firrtl::ConstantOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto constOp = mlir::cast<firrtl::ConstantOp>(op);
        InterpValue value = constOp.getValue();
        if (op->getNumResults() > 0)
          ctx.setValue(op->getResult(0), value);
        return value;
      });

  // AddPrimOp
  registry.registerHandler<firrtl::AddPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto addOp = mlir::cast<firrtl::AddPrimOp>(op);
        InterpValue lhs = ctx.getValue(addOp.getLhs());
        InterpValue rhs = ctx.getValue(addOp.getRhs());
        unsigned resultWidth = std::max(lhs.getBitWidth(), rhs.getBitWidth()) + 1;
        InterpValue lhsExt = lhs.zext(resultWidth);
        InterpValue rhsExt = rhs.zext(resultWidth);
        InterpValue result = lhsExt + rhsExt;
        ctx.setValue(addOp.getResult(), result);
        return result;
      });

  // SubPrimOp
  registry.registerHandler<firrtl::SubPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto subOp = mlir::cast<firrtl::SubPrimOp>(op);
        InterpValue lhs = ctx.getValue(subOp.getLhs());
        InterpValue rhs = ctx.getValue(subOp.getRhs());
        unsigned resultWidth = std::max(lhs.getBitWidth(), rhs.getBitWidth()) + 1;
        InterpValue lhsExt = lhs.zext(resultWidth);
        InterpValue rhsExt = rhs.zext(resultWidth);
        InterpValue result = lhsExt - rhsExt;
        ctx.setValue(subOp.getResult(), result);
        return result;
      });

  // BitsPrimOp
  registry.registerHandler<firrtl::BitsPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto bitsOp = mlir::cast<firrtl::BitsPrimOp>(op);
        InterpValue input = ctx.getValue(bitsOp.getInput());
        unsigned hi = bitsOp.getHi();
        unsigned lo = bitsOp.getLo();
        unsigned resultWidth = hi - lo + 1;
        InterpValue shifted = input.lshr(lo);
        InterpValue result = shifted.trunc(resultWidth);
        ctx.setValue(bitsOp.getResult(), result);
        return result;
      });

  // DShrPrimOp
  registry.registerHandler<firrtl::DShrPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto shrOp = mlir::cast<firrtl::DShrPrimOp>(op);
        InterpValue input = ctx.getValue(shrOp.getLhs());
        InterpValue amount = ctx.getValue(shrOp.getRhs());
        InterpValue result = input.lshr(amount);
        ctx.setValue(shrOp.getResult(), result);
        return result;
      });

  // ShrPrimOp
  registry.registerHandler<firrtl::ShrPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto shrOp = mlir::cast<firrtl::ShrPrimOp>(op);
        InterpValue input = ctx.getValue(shrOp.getInput());
        unsigned amount = shrOp.getAmount();
        unsigned inputWidth = input.getBitWidth();
        unsigned resultWidth = inputWidth > amount ? inputWidth - amount : 1;
        InterpValue shifted = input.lshr(amount);
        InterpValue result = shifted.trunc(resultWidth);
        ctx.setValue(shrOp.getResult(), result);
        return result;
      });

  // ShlPrimOp
  registry.registerHandler<firrtl::ShlPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto shlOp = mlir::cast<firrtl::ShlPrimOp>(op);
        InterpValue input = ctx.getValue(shlOp.getInput());
        unsigned amount = shlOp.getAmount();
        unsigned resultWidth = input.getBitWidth() + amount;
        InterpValue extended = input.zext(resultWidth);
        InterpValue result = extended.shl(amount);
        ctx.setValue(shlOp.getResult(), result);
        return result;
      });

  // PadPrimOp
  registry.registerHandler<firrtl::PadPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto padOp = mlir::cast<firrtl::PadPrimOp>(op);
        InterpValue input = ctx.getValue(padOp.getInput());
        unsigned resultWidth = padOp.getAmount();
        InterpValue result = input.zext(resultWidth);
        ctx.setValue(padOp.getResult(), result);
        return result;
      });

  // AndPrimOp
  registry.registerHandler<firrtl::AndPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto andOp = mlir::cast<firrtl::AndPrimOp>(op);
        InterpValue lhs = ctx.getValue(andOp.getLhs());
        InterpValue rhs = ctx.getValue(andOp.getRhs());
        unsigned resultWidth = std::max(lhs.getBitWidth(), rhs.getBitWidth());
        InterpValue lhsExt = lhs.zext(resultWidth);
        InterpValue rhsExt = rhs.zext(resultWidth);
        InterpValue result = lhsExt & rhsExt;
        ctx.setValue(andOp.getResult(), result);
        return result;
      });

  // OrPrimOp
  registry.registerHandler<firrtl::OrPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto orOp = mlir::cast<firrtl::OrPrimOp>(op);
        InterpValue lhs = ctx.getValue(orOp.getLhs());
        InterpValue rhs = ctx.getValue(orOp.getRhs());
        unsigned resultWidth = std::max(lhs.getBitWidth(), rhs.getBitWidth());
        InterpValue lhsExt = lhs.zext(resultWidth);
        InterpValue rhsExt = rhs.zext(resultWidth);
        InterpValue result = lhsExt | rhsExt;
        ctx.setValue(orOp.getResult(), result);
        return result;
      });

  // XorPrimOp
  registry.registerHandler<firrtl::XorPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto xorOp = mlir::cast<firrtl::XorPrimOp>(op);
        InterpValue lhs = ctx.getValue(xorOp.getLhs());
        InterpValue rhs = ctx.getValue(xorOp.getRhs());
        unsigned resultWidth = std::max(lhs.getBitWidth(), rhs.getBitWidth());
        InterpValue lhsExt = lhs.zext(resultWidth);
        InterpValue rhsExt = rhs.zext(resultWidth);
        InterpValue result = lhsExt ^ rhsExt;
        ctx.setValue(xorOp.getResult(), result);
        return result;
      });

  // EQPrimOp
  registry.registerHandler<firrtl::EQPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto eqOp = mlir::cast<firrtl::EQPrimOp>(op);
        InterpValue lhs = ctx.getValue(eqOp.getLhs());
        InterpValue rhs = ctx.getValue(eqOp.getRhs());
        InterpValue result(1, lhs == rhs ? 1 : 0);
        ctx.setValue(eqOp.getResult(), result);
        return result;
      });

  // NEQPrimOp
  registry.registerHandler<firrtl::NEQPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto neqOp = mlir::cast<firrtl::NEQPrimOp>(op);
        InterpValue lhs = ctx.getValue(neqOp.getLhs());
        InterpValue rhs = ctx.getValue(neqOp.getRhs());
        InterpValue result(1, lhs != rhs ? 1 : 0);
        ctx.setValue(neqOp.getResult(), result);
        return result;
      });

  // LTPrimOp
  registry.registerHandler<firrtl::LTPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto ltOp = mlir::cast<firrtl::LTPrimOp>(op);
        InterpValue lhs = ctx.getValue(ltOp.getLhs());
        InterpValue rhs = ctx.getValue(ltOp.getRhs());
        InterpValue result(1, lhs.ult(rhs) ? 1 : 0);
        ctx.setValue(ltOp.getResult(), result);
        return result;
      });

  // LEQPrimOp
  registry.registerHandler<firrtl::LEQPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto leqOp = mlir::cast<firrtl::LEQPrimOp>(op);
        InterpValue lhs = ctx.getValue(leqOp.getLhs());
        InterpValue rhs = ctx.getValue(leqOp.getRhs());
        InterpValue result(1, lhs.ule(rhs) ? 1 : 0);
        ctx.setValue(leqOp.getResult(), result);
        return result;
      });

  // GTPrimOp
  registry.registerHandler<firrtl::GTPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto gtOp = mlir::cast<firrtl::GTPrimOp>(op);
        InterpValue lhs = ctx.getValue(gtOp.getLhs());
        InterpValue rhs = ctx.getValue(gtOp.getRhs());
        InterpValue result(1, lhs.ugt(rhs) ? 1 : 0);
        ctx.setValue(gtOp.getResult(), result);
        return result;
      });

  // GEQPrimOp
  registry.registerHandler<firrtl::GEQPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto geqOp = mlir::cast<firrtl::GEQPrimOp>(op);
        InterpValue lhs = ctx.getValue(geqOp.getLhs());
        InterpValue rhs = ctx.getValue(geqOp.getRhs());
        InterpValue result(1, lhs.uge(rhs) ? 1 : 0);
        ctx.setValue(geqOp.getResult(), result);
        return result;
      });

  // MuxPrimOp
  registry.registerHandler<firrtl::MuxPrimOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto muxOp = mlir::cast<firrtl::MuxPrimOp>(op);
        InterpValue sel = ctx.getValue(muxOp.getSel());
        InterpValue result =
            sel != 0 ? ctx.getValue(muxOp.getHigh()) : ctx.getValue(muxOp.getLow());
        ctx.setValue(muxOp.getResult(), result);
        return result;
      });
}

//===----------------------------------------------------------------------===//
// CMT2 Dialect Handlers
//===----------------------------------------------------------------------===//

void circt::cmt2::interp::registerCMT2Handlers(OpHandlerRegistry &registry) {
  // CallOp - Delegate to module interpreter registry
  registry.registerHandler<cmt2::CallOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto callOp = mlir::cast<cmt2::CallOp>(op);

        llvm::StringRef instanceName =
            callOp.getCalleeAttr().getLeafReference().getValue();
        llvm::StringRef methodName =
            callOp.getMethodOrValueAttr().getLeafReference().getValue();

        // Gather arguments
        std::vector<InterpValue> args;
        for (mlir::Value input : callOp.getInputs())
          args.push_back(ctx.getValue(input));

        // Call through module registry if available
        if (ctx.moduleRegistry) {
          auto results = ctx.moduleRegistry->callMethod(instanceName, methodName, args);
          if (results && !results->empty() && callOp.getNumResults() > 0) {
            ctx.setValue(callOp.getResult(0), (*results)[0]);
            return (*results)[0];
          }
        }

        return std::nullopt;
      });

  // Token operations - these need to be lowered before interpretation
  // TokenValidOp - returns validity of token
  registry.registerHandler<cmt2::TokenValidOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        LLVM_DEBUG(llvm::dbgs() << "TokenValidOp: token ops should be lowered "
                                << "via --cmt2-token-lowering before interpretation\n");
        // Return true (1) as default - token is valid
        auto validOp = mlir::cast<cmt2::TokenValidOp>(op);
        auto result = llvm::APInt(1, 1);
        ctx.setValue(validOp.getResult(), result);
        return result;
      });

  // TokenDataOp - extracts data from token
  registry.registerHandler<cmt2::TokenDataOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        LLVM_DEBUG(llvm::dbgs() << "TokenDataOp: token ops should be lowered "
                                << "via --cmt2-token-lowering before interpretation\n");
        auto dataOp = mlir::cast<cmt2::TokenDataOp>(op);
        // Pass through the token's underlying data
        auto tokenValue = ctx.getValue(dataOp.getToken());
        ctx.setValue(dataOp.getResult(), tokenValue);
        return tokenValue;
      });

  // TokenCreateOp - creates a token from data
  registry.registerHandler<cmt2::TokenCreateOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        LLVM_DEBUG(llvm::dbgs() << "TokenCreateOp: token ops should be lowered "
                                << "via --cmt2-token-lowering before interpretation\n");
        auto createOp = mlir::cast<cmt2::TokenCreateOp>(op);
        // Pass through the data as the token value
        auto dataValue = ctx.getValue(createOp.getData());
        ctx.setValue(createOp.getResult(), dataValue);
        return dataValue;
      });

  // TokenJoinOp - joins multiple tokens
  registry.registerHandler<cmt2::TokenJoinOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        LLVM_DEBUG(llvm::dbgs() << "TokenJoinOp: token ops should be lowered "
                                << "via --cmt2-token-lowering before interpretation\n");
        auto joinOp = mlir::cast<cmt2::TokenJoinOp>(op);
        // For join, return the first input token's value
        if (joinOp.getNumOperands() > 0) {
          auto result = ctx.getValue(joinOp.getOperand(0));
          ctx.setValue(joinOp.getResult(), result);
          return result;
        }
        return std::nullopt;
      });

  // ReturnOp - return from method/value
  registry.registerHandler<cmt2::ReturnOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto retOp = mlir::cast<cmt2::ReturnOp>(op);
        if (retOp.getNumOperands() > 0) {
          return ctx.getValue(retOp.getOperand(0));
        }
        return std::nullopt;
      });

  // YieldOp - yield from control flow region
  registry.registerHandler<cmt2::YieldOp>(
      [](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto yieldOp = mlir::cast<cmt2::YieldOp>(op);
        if (yieldOp.getNumOperands() > 0) {
          return ctx.getValue(yieldOp.getOperand(0));
        }
        return std::nullopt;
      });

  // ProcSeqOp - sequential execution of body
  registry.registerHandler<cmt2::ProcSeqOp>(
      [&registry](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto seqOp = mlir::cast<cmt2::ProcSeqOp>(op);
        mlir::Region &body = seqOp.getBody();
        if (body.empty())
          return std::nullopt;

        LLVM_DEBUG(llvm::dbgs() << "ProcSeqOp: executing sequential body\n");

        // Execute operations in body sequentially
        for (mlir::Operation &innerOp : body.front()) {
          if (mlir::isa<cmt2::YieldOp>(innerOp))
            continue;
          registry.execute(&innerOp, ctx);
        }

        return std::nullopt;
      });

  // ProcParOp - parallel execution of body (all ops in one cycle)
  registry.registerHandler<cmt2::ProcParOp>(
      [&registry](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto parOp = mlir::cast<cmt2::ProcParOp>(op);
        mlir::Region &body = parOp.getBody();
        if (body.empty())
          return std::nullopt;

        LLVM_DEBUG(llvm::dbgs() << "ProcParOp: executing parallel body\n");

        // In simulation, we execute all ops "in parallel" (same cycle)
        // Order matters for determinism, but all should complete in one cycle
        for (mlir::Operation &innerOp : body.front()) {
          if (mlir::isa<cmt2::YieldOp>(innerOp))
            continue;
          registry.execute(&innerOp, ctx);
        }

        return std::nullopt;
      });

  // ProcIfOp - conditional execution
  registry.registerHandler<cmt2::ProcIfOp>(
      [&registry](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto ifOp = mlir::cast<cmt2::ProcIfOp>(op);
        InterpValue cond = ctx.getValue(ifOp.getCond());
        bool takeThen = cond != 0;

        LLVM_DEBUG(llvm::dbgs() << "ProcIfOp: taking " << (takeThen ? "then" : "else")
                                << " branch\n");

        mlir::Region &branch = takeThen ? ifOp.getThenRegion() : ifOp.getElseRegion();
        if (branch.empty())
          return std::nullopt;

        for (mlir::Operation &innerOp : branch.front()) {
          if (mlir::isa<cmt2::YieldOp>(innerOp))
            continue;
          registry.execute(&innerOp, ctx);
        }

        return std::nullopt;
      });

  // ProcCondIfOp - conditional with computed condition region
  registry.registerHandler<cmt2::ProcCondIfOp>(
      [&registry](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto condIfOp = mlir::cast<cmt2::ProcCondIfOp>(op);

        // First execute the condition region
        mlir::Region &condRegion = condIfOp.getCondRegion();
        if (!condRegion.empty()) {
          for (mlir::Operation &innerOp : condRegion.front()) {
            registry.execute(&innerOp, ctx);
          }
        }

        // Get condition value (yielded from condition region)
        InterpValue cond = ctx.getValue(condIfOp.getCond());
        bool takeThen = cond != 0;

        LLVM_DEBUG(llvm::dbgs() << "ProcCondIfOp: taking " << (takeThen ? "then" : "else")
                                << " branch\n");

        mlir::Region &branch = takeThen ? condIfOp.getThenRegion() : condIfOp.getElseRegion();
        if (branch.empty())
          return std::nullopt;

        for (mlir::Operation &innerOp : branch.front()) {
          if (mlir::isa<cmt2::YieldOp>(innerOp))
            continue;
          registry.execute(&innerOp, ctx);
        }

        return std::nullopt;
      });

  // ProcWhileOp - loop while condition is true
  // Note: This is a simplified interpreter that may not handle all cases
  registry.registerHandler<cmt2::ProcWhileOp>(
      [&registry](mlir::Operation *op, OpContext &ctx) -> std::optional<InterpValue> {
        auto whileOp = mlir::cast<cmt2::ProcWhileOp>(op);
        mlir::Region &condRegion = whileOp.getCondRegion();
        mlir::Region &body = whileOp.getBody();

        const unsigned maxIterations = 10000; // Safety limit
        unsigned iterations = 0;

        while (iterations < maxIterations) {
          // Execute condition region
          if (!condRegion.empty()) {
            for (mlir::Operation &innerOp : condRegion.front()) {
              registry.execute(&innerOp, ctx);
            }
          }

          // Check condition (from block argument or yield)
          // The condition is typically yielded from the condition region
          // For simplicity, we look for a ProcWhileCondOp or check a default
          bool continueLoop = false;

          // Look for while_cond in the condition region
          for (mlir::Operation &innerOp : condRegion.front()) {
            if (auto condOp = mlir::dyn_cast<cmt2::ProcWhileCondYieldOp>(innerOp)) {
              InterpValue condVal = ctx.getValue(condOp.getCond());
              continueLoop = condVal != 0;
              break;
            }
          }

          if (!continueLoop) {
            LLVM_DEBUG(llvm::dbgs() << "ProcWhileOp: exiting after " << iterations
                                    << " iterations\n");
            break;
          }

          // Execute body
          if (!body.empty()) {
            for (mlir::Operation &innerOp : body.front()) {
              if (mlir::isa<cmt2::YieldOp>(innerOp))
                continue;
              registry.execute(&innerOp, ctx);
            }
          }

          iterations++;
        }

        if (iterations >= maxIterations) {
          LLVM_DEBUG(llvm::dbgs() << "ProcWhileOp: WARNING - hit max iterations limit\n");
        }

        return std::nullopt;
      });
}

//===- TokenRTLGen.cpp - Generate RTL for token operations ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass generates RTL hardware for token operations (cmt2-token-materialize):
// - Instantiate storage modules for LS tokens (shift registers) and LI tokens (FIFOs)
// - Rewrite token.create, token.valid, token.data, token.join to storage operations
// - Remove token signatures from rules after rewriting
//
// After this pass, NO token operations should remain in the IR.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/Cmt2/Cmt2Types.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/Cmt2/Transforms/ModuleGenerator.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#define DEBUG_TYPE "cmt2-token-rtl-gen"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_TOKENRTLGEN
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

/// Information about a token's storage implementation
struct TokenStorageInfo {
  /// The storage instance (ShiftReg or FIFO)
  InstanceOp storageInstance;
  /// The instance name
  std::string instanceName;
  /// Whether this is LI (FIFO) or LS (shift register)
  bool isLatencyInsensitive;
  /// Data width in bits
  unsigned dataWidth;
  /// Storage depth (shift register stages or FIFO depth)
  unsigned depth;
};

struct TokenRTLGenPass
    : public circt::cmt2::impl::TokenRTLGenBase<TokenRTLGenPass> {
  using TokenRTLGenBase::TokenRTLGenBase;

  void runOnOperation() override;

private:
  /// Process a single CMT2 module
  void processModule(cmt2::ModuleOp module);

  /// Collect all token producers and consumers in a module
  void collectTokenInfo(cmt2::ModuleOp module);

  /// Create storage instances for all tokens at module level
  LogicalResult createStorageInstances(cmt2::ModuleOp module);

  /// Rewrite token operations in rules to storage calls
  LogicalResult rewriteTokenOps(cmt2::ModuleOp module);

  /// Get the data width from a token type
  unsigned getDataWidth(SyncTokenType tokenType);

  /// Get the storage depth from annotations
  unsigned getStorageDepth(Operation *producer);

  /// Check if a token is latency-insensitive
  bool isTokenLI(SyncTokenType tokenType);

  /// Map from token Value (from token.create result) to storage info
  llvm::DenseMap<Value, TokenStorageInfo> tokenStorage_;

  /// Map from token block argument to storage instance name
  /// Note: We use SmallDenseMap to allow std::string values
  llvm::SmallDenseMap<Value, std::string, 8> tokenArgToStorage_;

  /// Map from token type string to producer storage info (for type-based matching)
  llvm::StringMap<TokenStorageInfo*> tokenTypeToStorage_;

  /// Map from instance name to InstanceOp (for generating calls)
  llvm::StringMap<InstanceOp> instanceNameToOp_;

  /// Map from token.join result to the AND of valid signals
  llvm::DenseMap<Value, Value> joinResultToAndSignal_;

  /// Counter for generating unique instance names
  unsigned instanceCounter_ = 0;

  /// Build mapping from consumer token block args to producer storage
  void buildTokenArgMapping(cmt2::ModuleOp module);

  /// Get string key for a token type (for matching)
  std::string getTokenTypeKey(Type type);
};

//===----------------------------------------------------------------------===//
// Utility Functions
//===----------------------------------------------------------------------===//

unsigned TokenRTLGenPass::getDataWidth(SyncTokenType tokenType) {
  if (auto dataType = tokenType.getDataType()) {
    if (auto intType = dyn_cast<firrtl::IntType>(dataType))
      return intType.getWidthOrSentinel();
    // Default for unknown types
    return 32;
  }
  // Token without data - just valid signal
  return 0;
}

unsigned TokenRTLGenPass::getStorageDepth(Operation *producer) {
  // Check for annotation from TokenLowering pass
  if (auto depthAttr = producer->getAttrOfType<IntegerAttr>("token.shiftreg_depth"))
    return depthAttr.getInt();
  if (auto depthAttr = producer->getAttrOfType<IntegerAttr>("token.fifo_depth"))
    return depthAttr.getInt();

  // Try to infer from timing attribute
  if (auto timing = producer->getAttrOfType<TimingIntervalAttr>("timing")) {
    unsigned depth = static_cast<unsigned>(timing.getEnd() - timing.getStart());
    return depth > 0 ? depth : 1;
  }

  // Default depth
  return 1;
}

bool TokenRTLGenPass::isTokenLI(SyncTokenType tokenType) {
  return tokenType.getMode() == TokenMode::LI;
}

//===----------------------------------------------------------------------===//
// Token Type Utilities
//===----------------------------------------------------------------------===//

std::string TokenRTLGenPass::getTokenTypeKey(Type type) {
  std::string key;
  llvm::raw_string_ostream os(key);
  type.print(os);
  return key;
}

//===----------------------------------------------------------------------===//
// Token Collection
//===----------------------------------------------------------------------===//

void TokenRTLGenPass::collectTokenInfo(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Collecting token info for module @"
                          << module.getSymName() << "\n");

  // Find all token.create operations
  module.walk([&](TokenCreateOp createOp) {
    Value token = createOp.getToken();
    auto tokenType = cast<SyncTokenType>(token.getType());

    LLVM_DEBUG({
      llvm::dbgs() << "  Found token.create: ";
      createOp.print(llvm::dbgs());
      llvm::dbgs() << "\n";
      llvm::dbgs() << "    Data width: " << getDataWidth(tokenType) << "\n";
      llvm::dbgs() << "    Mode: " << (isTokenLI(tokenType) ? "LI" : "LS")
                   << "\n";
    });
  });

  // Build mapping from token block arguments to their producer values
  // This requires analyzing the dataflow connections
  module.walk([&](RuleOp rule) {
    // Check for token_in_types attribute
    if (auto tokenInTypes = rule->getAttrOfType<ArrayAttr>("token_in_types")) {
      LLVM_DEBUG(llvm::dbgs() << "  Rule @" << rule.getSymName()
                              << " has " << tokenInTypes.size()
                              << " token inputs\n");
    }
    if (auto tokenOutTypes = rule->getAttrOfType<ArrayAttr>("token_out_types")) {
      LLVM_DEBUG(llvm::dbgs() << "  Rule @" << rule.getSymName()
                              << " has " << tokenOutTypes.size()
                              << " token outputs\n");
    }
  });
}

void TokenRTLGenPass::buildTokenArgMapping(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Building token arg mapping for module @"
                          << module.getSymName() << "\n");

  // First, build a map from token type to producer storage
  // (for simple single-producer-per-type cases)
  tokenTypeToStorage_.clear();

  for (auto &entry : tokenStorage_) {
    Value tokenValue = entry.first;
    TokenStorageInfo &info = entry.second;

    std::string typeKey = getTokenTypeKey(tokenValue.getType());
    tokenTypeToStorage_[typeKey] = &info;

    LLVM_DEBUG(llvm::dbgs() << "  Token type " << typeKey
                            << " -> storage " << info.instanceName << "\n");
  }

  // Now find all consumer rules and map their block arguments
  module.walk([&](RuleOp rule) {
    if (!rule.hasTokenInputs())
      return;

    auto tokenInTypes = rule.getTokenInTypes();
    if (!tokenInTypes)
      return;

    // Get guard and body regions - token args are in both
    Block &guardBlock = rule.getGuard().front();
    Block &bodyBlock = rule.getBody().front();

    // Token input args come after regular function args
    unsigned numFuncArgs = rule.getFunctionType().getNumInputs();

    // Check for explicit storage index mapping from DataflowLowering
    SmallVector<unsigned> inputStorageIndices;
    if (auto indicesAttr = rule->getAttrOfType<ArrayAttr>(
            "dataflow.input_storage_indices")) {
      for (auto attr : indicesAttr) {
        inputStorageIndices.push_back(
            cast<IntegerAttr>(attr).getValue().getZExtValue());
      }
      LLVM_DEBUG(llvm::dbgs() << "  Rule @" << rule.getSymName()
                              << " has explicit storage indices: ");
      for (unsigned idx : inputStorageIndices) {
        LLVM_DEBUG(llvm::dbgs() << idx << " ");
      }
      LLVM_DEBUG(llvm::dbgs() << "\n");
    }

    // Process guard block token args
    for (size_t i = 0; i < tokenInTypes->size(); ++i) {
      if (numFuncArgs + i >= guardBlock.getNumArguments())
        continue;

      Value tokenArg = guardBlock.getArgument(numFuncArgs + i);
      std::string storageName;

      // Use explicit storage index if available
      if (i < inputStorageIndices.size()) {
        storageName = "__tok_" + std::to_string(inputStorageIndices[i]);
        LLVM_DEBUG(llvm::dbgs() << "  Guard arg " << i << " in rule @"
                                << rule.getSymName()
                                << " mapped to storage " << storageName
                                << " (from explicit index)\n");
      } else {
        // Fall back to type-based lookup
        Type tokenType = tokenArg.getType();
        std::string typeKey = getTokenTypeKey(tokenType);
        auto storageIt = tokenTypeToStorage_.find(typeKey);
        if (storageIt != tokenTypeToStorage_.end()) {
          storageName = storageIt->second->instanceName;
          LLVM_DEBUG(llvm::dbgs() << "  Guard arg " << i << " in rule @"
                                  << rule.getSymName()
                                  << " mapped to storage " << storageName
                                  << " (from type)\n");
        } else {
          LLVM_DEBUG(llvm::dbgs() << "  Guard arg " << i << " in rule @"
                                  << rule.getSymName()
                                  << " - no matching producer for type "
                                  << typeKey << "\n");
          continue;
        }
      }
      tokenArgToStorage_[tokenArg] = storageName;
    }

    // Process body block token args (same mapping)
    for (size_t i = 0; i < tokenInTypes->size(); ++i) {
      if (numFuncArgs + i >= bodyBlock.getNumArguments())
        continue;

      Value tokenArg = bodyBlock.getArgument(numFuncArgs + i);
      std::string storageName;

      // Use explicit storage index if available
      if (i < inputStorageIndices.size()) {
        storageName = "__tok_" + std::to_string(inputStorageIndices[i]);
        LLVM_DEBUG(llvm::dbgs() << "  Body arg " << i << " in rule @"
                                << rule.getSymName()
                                << " mapped to storage " << storageName
                                << " (from explicit index)\n");
      } else {
        // Fall back to type-based lookup
        Type tokenType = tokenArg.getType();
        std::string typeKey = getTokenTypeKey(tokenType);
        auto storageIt = tokenTypeToStorage_.find(typeKey);
        if (storageIt != tokenTypeToStorage_.end()) {
          storageName = storageIt->second->instanceName;
        } else {
          continue;
        }
      }
      tokenArgToStorage_[tokenArg] = storageName;
    }
  });

  // Also handle DataflowTaskOp with explicit SSA connections
  module.walk([&](ProcDataflowOp dataflow) {
    dataflow.walk([&](DataflowTaskOp task) {
      // DataflowTaskOp has token_inputs which are SSA values from upstream tasks
      auto tokenInputs = task.getTokenInputs();
      Block &taskBody = task.getBody().front();

      for (size_t i = 0; i < tokenInputs.size(); ++i) {
        Value inputToken = tokenInputs[i];

        // Check if this input token has storage
        auto storageIt = tokenStorage_.find(inputToken);
        if (storageIt != tokenStorage_.end()) {
          // The corresponding block arg in the task body
          if (i < taskBody.getNumArguments()) {
            Value blockArg = taskBody.getArgument(i);
            tokenArgToStorage_[blockArg] = storageIt->second.instanceName;
            LLVM_DEBUG(llvm::dbgs() << "  Task @" << task.getSymName()
                                    << " arg " << i << " mapped to storage "
                                    << storageIt->second.instanceName << "\n");
          }
        } else {
          // Try type-based matching
          std::string typeKey = getTokenTypeKey(inputToken.getType());
          auto typeStorageIt = tokenTypeToStorage_.find(typeKey);
          if (typeStorageIt != tokenTypeToStorage_.end()) {
            if (i < taskBody.getNumArguments()) {
              Value blockArg = taskBody.getArgument(i);
              tokenArgToStorage_[blockArg] = typeStorageIt->second->instanceName;
            }
          }
        }
      }
    });
  });
}

//===----------------------------------------------------------------------===//
// Storage Instance Creation
//===----------------------------------------------------------------------===//

LogicalResult TokenRTLGenPass::createStorageInstances(cmt2::ModuleOp module) {
  // Get clock and reset from module arguments
  Value clk = nullptr, rst = nullptr;
  for (auto arg : module.getBody().front().getArguments()) {
    auto argType = arg.getType();
    if (isa<firrtl::ClockType>(argType))
      clk = arg;
    else if (auto uintType = dyn_cast<firrtl::UIntType>(argType)) {
      if (uintType.getWidthOrSentinel() == 1 && !rst)
        rst = arg;
    }
  }

  if (!clk) {
    LLVM_DEBUG(llvm::dbgs() << "  No clock found in module @"
                            << module.getSymName() << ", skipping storage creation\n");
    return success();
  }

  auto circuit = module->getParentOfType<CircuitOp>();
  if (!circuit)
    return module.emitError("Module not inside a circuit");

  // Create module generator for this circuit
  ModuleGenerator gen(circuit);

  OpBuilder builder(module.getContext());

  // Find insertion point at beginning of module body (after arguments)
  builder.setInsertionPointToStart(&module.getBody().front());

  // Collect all token.create ops and create storage for each
  SmallVector<TokenCreateOp> createOps;
  module.walk([&](TokenCreateOp op) { createOps.push_back(op); });

  for (auto createOp : createOps) {
    Value token = createOp.getToken();
    auto tokenType = cast<SyncTokenType>(token.getType());
    unsigned dataWidth = getDataWidth(tokenType);
    unsigned depth = getStorageDepth(createOp);
    bool isLI = isTokenLI(tokenType);

    std::string instName = "__tok_" + std::to_string(instanceCounter_++);

    LLVM_DEBUG(llvm::dbgs() << "  Creating storage instance: " << instName
                            << " (width=" << dataWidth << ", depth=" << depth
                            << ", mode=" << (isLI ? "LI" : "LS") << ")\n");

    // Get or create the appropriate storage module
    Cmt2ModuleLike storageModule;
    if (isLI) {
      storageModule = gen.getOrCreateFIFOModule(dataWidth, depth);
    } else {
      storageModule = gen.getOrCreateShiftRegModule(dataWidth, depth);
    }

    if (!storageModule) {
      return createOp.emitError("Failed to create storage module");
    }

    LLVM_DEBUG(llvm::dbgs() << "    Using storage module: @"
                            << storageModule.moduleNameAttr().getValue() << "\n");

    // Create storage instance at module level
    auto instance = gen.createStorageInstance(
        createOp.getLoc(), instName, storageModule, clk, rst, builder);

    LLVM_DEBUG(llvm::dbgs() << "    Created instance: @" << instName << "\n");

    // Add annotation about the storage for debugging
    createOp->setAttr("token.storage_instance",
                      builder.getStringAttr(instName));
    createOp->setAttr("token.storage_module",
                      storageModule.moduleNameAttr());

    // Store info for later rewriting
    TokenStorageInfo info;
    info.storageInstance = instance;
    info.instanceName = instName;
    info.isLatencyInsensitive = isLI;
    info.dataWidth = dataWidth;
    info.depth = depth;
    tokenStorage_[token] = info;

    // Also store instance by name for lookup during consumer rewriting
    instanceNameToOp_[instName] = instance;
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Token Operation Rewriting
//===----------------------------------------------------------------------===//

LogicalResult TokenRTLGenPass::rewriteTokenOps(cmt2::ModuleOp module) {
  auto circuit = module->getParentOfType<CircuitOp>();
  if (!circuit)
    return module.emitError("Module not inside a circuit");

  ModuleGenerator gen(circuit);

  LLVM_DEBUG(llvm::dbgs() << "Rewriting token ops in module @"
                          << module.getSymName() << "\n");

  // Rewrite token.create ops to storage.write calls
  SmallVector<TokenCreateOp> createOpsToRewrite;
  module.walk([&](TokenCreateOp op) {
    // Only rewrite if we have storage info for this token
    if (tokenStorage_.count(op.getToken()))
      createOpsToRewrite.push_back(op);
  });

  for (auto createOp : createOpsToRewrite) {
    auto it = tokenStorage_.find(createOp.getToken());
    if (it == tokenStorage_.end())
      continue;

    auto &storageInfo = it->second;
    if (!storageInfo.storageInstance) {
      LLVM_DEBUG(llvm::dbgs() << "  Skipping token.create - no storage instance\n");
      continue;
    }

    OpBuilder builder(createOp);
    Location loc = createOp.getLoc();

    // Generate call to storage.write(data)
    if (Value data = createOp.getData()) {
      LLVM_DEBUG(llvm::dbgs() << "  Rewriting token.create to storage.write\n");

      auto results = gen.createStorageCall(loc, storageInfo.storageInstance,
                                            "write", {data}, builder);

      // Mark the token.create as rewritten (but don't delete yet -
      // we need to handle consumers first)
      createOp->setAttr("token.rewritten", builder.getUnitAttr());
      createOp->setAttr("token.storage_instance",
                        builder.getStringAttr(storageInfo.instanceName));
    } else {
      // Token without data - just mark valid
      LLVM_DEBUG(llvm::dbgs() << "  Token without data - marking valid only\n");
      createOp->setAttr("token.rewritten", builder.getUnitAttr());
    }
  }

  // Process token.join BEFORE token.valid so join results can be used
  SmallVector<TokenJoinOp> joinOpsToRewrite;
  module.walk([&](TokenJoinOp op) {
    joinOpsToRewrite.push_back(op);
  });

  // Rewrite token.join ops - AND all valid signals together
  for (auto op : joinOpsToRewrite) {
    SmallVector<Value> validSignals;
    bool allMapped = true;

    // Get valid signal for each input token
    for (Value inputToken : op.getTokens()) {
      std::string instanceName;

      // Check if token comes from a token.create
      if (auto createOp = inputToken.getDefiningOp<TokenCreateOp>()) {
        if (auto instAttr = createOp->getAttrOfType<StringAttr>("token.storage_instance")) {
          instanceName = instAttr.getValue().str();
        }
      } else {
        // Token is a block argument - look up in our mapping
        auto it = tokenArgToStorage_.find(inputToken);
        if (it != tokenArgToStorage_.end()) {
          instanceName = it->second;
        }
      }

      if (!instanceName.empty()) {
        auto instIt = instanceNameToOp_.find(instanceName);
        if (instIt != instanceNameToOp_.end()) {
          InstanceOp storageInstance = instIt->second;
          OpBuilder builder(op);
          auto results = gen.createStorageCall(op.getLoc(), storageInstance,
                                                "valid", {}, builder);
          if (!results.empty()) {
            validSignals.push_back(results[0]);
          } else {
            allMapped = false;
          }
        } else {
          allMapped = false;
        }
      } else {
        allMapped = false;
      }
    }

    if (allMapped && !validSignals.empty()) {
      OpBuilder builder(op);
      Location loc = op.getLoc();

      // AND all valid signals together
      Value result = validSignals[0];
      for (size_t i = 1; i < validSignals.size(); ++i) {
        result = builder.create<firrtl::AndPrimOp>(loc, result, validSignals[i]);
      }

      // Store the AND result for later use when processing token.valid on this join
      joinResultToAndSignal_[op.getResult()] = result;

      // Mark as rewritten
      op->setAttr("token.rewritten", builder.getUnitAttr());

      LLVM_DEBUG(llvm::dbgs() << "  Rewrote token.join with " << validSignals.size()
                              << " inputs to AND chain\n");
    } else {
      op->setAttr("token.rewrite_to_and", UnitAttr::get(op.getContext()));
      LLVM_DEBUG(llvm::dbgs() << "  Marked token.join for downstream AND rewrite\n");
    }
  }

  // Collect token.valid ops for rewriting (can't modify while walking)
  SmallVector<TokenValidOp> validOpsToRewrite;
  module.walk([&](TokenValidOp op) {
    validOpsToRewrite.push_back(op);
  });

  for (auto op : validOpsToRewrite) {
    Value token = op.getToken();
    std::string instanceName;

    // Check if token comes from a token.join we've rewritten
    auto joinIt = joinResultToAndSignal_.find(token);
    if (joinIt != joinResultToAndSignal_.end()) {
      // Replace uses of token.valid on join result with the AND signal
      op.getResult().replaceAllUsesWith(joinIt->second);
      op->setAttr("token.rewritten", UnitAttr::get(op.getContext()));
      LLVM_DEBUG(llvm::dbgs() << "  Rewrote token.valid on join to AND signal\n");
      continue;
    }

    // Check if token comes from a token.create we've rewritten
    if (auto createOp = token.getDefiningOp<TokenCreateOp>()) {
      if (auto instAttr = createOp->getAttrOfType<StringAttr>("token.storage_instance")) {
        instanceName = instAttr.getValue().str();
      }
    } else {
      // Token is a block argument - look up in our mapping
      auto it = tokenArgToStorage_.find(token);
      if (it != tokenArgToStorage_.end()) {
        instanceName = it->second;
        LLVM_DEBUG(llvm::dbgs() << "  token.valid block arg mapped to storage: "
                                << instanceName << "\n");
      }
    }

    if (!instanceName.empty()) {
      // Find the storage instance
      auto instIt = instanceNameToOp_.find(instanceName);
      if (instIt != instanceNameToOp_.end()) {
        InstanceOp storageInstance = instIt->second;

        // Generate call to storage.valid()
        OpBuilder builder(op);
        auto results = gen.createStorageCall(op.getLoc(), storageInstance,
                                              "valid", {}, builder);
        if (!results.empty()) {
          // Replace uses of token.valid result with call result
          op.getResult().replaceAllUsesWith(results[0]);
          op->setAttr("token.rewritten", builder.getUnitAttr());
          LLVM_DEBUG(llvm::dbgs() << "  Rewrote token.valid to storage.valid call\n");
        }
      } else {
        // Just mark for downstream handling
        op->setAttr("token.storage_instance",
                    StringAttr::get(op.getContext(), instanceName));
        op->setAttr("token.storage_method",
                    StringAttr::get(op.getContext(), "valid"));
        LLVM_DEBUG(llvm::dbgs() << "  Marked token.valid with storage: "
                                << instanceName << "\n");
      }
    } else {
      // No mapping found - mark for downstream handling
      op->setAttr("token.from_block_arg", UnitAttr::get(op.getContext()));
      LLVM_DEBUG(llvm::dbgs() << "  token.valid uses block arg - no mapping found\n");
    }
  }

  // Collect token.data ops for rewriting (can't modify while walking)
  SmallVector<TokenDataOp> dataOpsToRewrite;
  module.walk([&](TokenDataOp op) {
    dataOpsToRewrite.push_back(op);
  });

  for (auto op : dataOpsToRewrite) {
    Value token = op.getToken();
    std::string instanceName;

    // Check if token comes from a token.create we've rewritten
    if (auto createOp = token.getDefiningOp<TokenCreateOp>()) {
      if (auto instAttr = createOp->getAttrOfType<StringAttr>("token.storage_instance")) {
        instanceName = instAttr.getValue().str();
      }
    } else {
      // Token is a block argument - look up in our mapping
      auto it = tokenArgToStorage_.find(token);
      if (it != tokenArgToStorage_.end()) {
        instanceName = it->second;
        LLVM_DEBUG(llvm::dbgs() << "  token.data block arg mapped to storage: "
                                << instanceName << "\n");
      }
    }

    if (!instanceName.empty()) {
      // Find the storage instance
      auto instIt = instanceNameToOp_.find(instanceName);
      if (instIt != instanceNameToOp_.end()) {
        InstanceOp storageInstance = instIt->second;

        // Generate call to storage.peek()
        OpBuilder builder(op);
        auto results = gen.createStorageCall(op.getLoc(), storageInstance,
                                              "peek", {}, builder);
        if (!results.empty()) {
          // Replace uses of token.data result with call result
          op.getResult().replaceAllUsesWith(results[0]);
          op->setAttr("token.rewritten", builder.getUnitAttr());
          LLVM_DEBUG(llvm::dbgs() << "  Rewrote token.data to storage.peek call\n");
        }
      } else {
        // Just mark for downstream handling
        op->setAttr("token.storage_instance",
                    StringAttr::get(op.getContext(), instanceName));
        op->setAttr("token.storage_method",
                    StringAttr::get(op.getContext(), "peek"));
        LLVM_DEBUG(llvm::dbgs() << "  Marked token.data with storage: "
                                << instanceName << "\n");
      }
    } else {
      // No mapping found - mark for downstream handling
      op->setAttr("token.from_block_arg", UnitAttr::get(op.getContext()));
      LLVM_DEBUG(llvm::dbgs() << "  token.data uses block arg - no mapping found\n");
    }
  }

  // Clean up: Erase token ops that have been fully rewritten
  // We need to erase in dependency order: consumers first, then producers
  unsigned erasedCount = 0;

  // Phase 1: Erase token.valid and token.data (consumers of tokens)
  for (auto validOp : validOpsToRewrite) {
    if (validOp->hasAttr("token.rewritten") && validOp.getResult().use_empty()) {
      LLVM_DEBUG(llvm::dbgs() << "  Erasing: "; validOp->print(llvm::dbgs());
                 llvm::dbgs() << "\n");
      validOp->erase();
      erasedCount++;
    }
  }

  for (auto dataOp : dataOpsToRewrite) {
    if (dataOp->hasAttr("token.rewritten") && dataOp.getResult().use_empty()) {
      LLVM_DEBUG(llvm::dbgs() << "  Erasing: "; dataOp->print(llvm::dbgs());
                 llvm::dbgs() << "\n");
      dataOp->erase();
      erasedCount++;
    }
  }

  // Phase 2: Erase token.join (now that token.valid users are gone)
  for (auto joinOp : joinOpsToRewrite) {
    if (joinOp->hasAttr("token.rewritten") && joinOp.getResult().use_empty()) {
      LLVM_DEBUG(llvm::dbgs() << "  Erasing: "; joinOp->print(llvm::dbgs());
                 llvm::dbgs() << "\n");
      joinOp->erase();
      erasedCount++;
    }
  }

  // Phase 3: Erase token.create (producers)
  for (auto createOp : createOpsToRewrite) {
    if (createOp->hasAttr("token.rewritten") && createOp.getToken().use_empty()) {
      LLVM_DEBUG(llvm::dbgs() << "  Erasing: "; createOp->print(llvm::dbgs());
                 llvm::dbgs() << "\n");
      createOp->erase();
      erasedCount++;
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "  Erased " << erasedCount << " rewritten token ops\n");

  // Remove token signatures from rules
  // (The tokens have been materialized to storage, so signatures are no longer needed)
  module.walk([&](RuleOp rule) {
    bool modified = false;

    // Remove token input types and names
    if (rule.getTokenInTypes()) {
      rule.removeTokenInTypesAttr();
      modified = true;
    }
    if (rule.getTokenInNames()) {
      rule.removeTokenInNamesAttr();
      modified = true;
    }

    // Remove token output types
    if (rule.getTokenOutTypes()) {
      rule.removeTokenOutTypesAttr();
      modified = true;
    }

    if (modified) {
      LLVM_DEBUG(llvm::dbgs() << "  Removed token signature from rule @"
                              << rule.getSymName() << "\n");
    }
  });

  return success();
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void TokenRTLGenPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // Clear per-module state
  tokenStorage_.clear();
  tokenArgToStorage_.clear();
  tokenTypeToStorage_.clear();
  instanceNameToOp_.clear();
  joinResultToAndSignal_.clear();

  // Step 1: Collect information about all tokens
  collectTokenInfo(module);

  // Step 2: Create storage instances
  if (failed(createStorageInstances(module))) {
    signalPassFailure();
    return;
  }

  // Step 3: Build mapping from consumer token args to producer storage
  buildTokenArgMapping(module);

  // Step 4: Rewrite token operations
  if (failed(rewriteTokenOps(module))) {
    signalPassFailure();
    return;
  }
}

//===----------------------------------------------------------------------===//
// Pass Entry Point
//===----------------------------------------------------------------------===//

void TokenRTLGenPass::runOnOperation() {
  auto circuit = getOperation();

  LLVM_DEBUG(llvm::dbgs() << "=== TokenRTLGen Pass ===\n");
  LLVM_DEBUG(llvm::dbgs() << "Processing circuit with "
                          << std::distance(circuit.getBody().front().begin(),
                                           circuit.getBody().front().end())
                          << " operations\n");

  // Initialize ModuleLibrary singleton if not already initialized
  auto &library = ecmt2::ModuleLibrary::getInstance();
  if (!library.hasModule("FIRRTLReg")) {
    // Try to find the module library manifest
    llvm::SmallString<256> manifestPath;
    llvm::sys::path::append(manifestPath, "lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml");

    if (!llvm::sys::fs::exists(manifestPath)) {
      // Try relative to executable path
      auto exeOrError = llvm::sys::fs::getMainExecutable(nullptr, nullptr);
      if (!exeOrError.empty()) {
        manifestPath = llvm::sys::path::parent_path(exeOrError);
        llvm::sys::path::append(manifestPath, "../lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml");
      }
    }

    if (llvm::sys::fs::exists(manifestPath)) {
      llvm::SmallString<256> libraryPath = llvm::sys::path::parent_path(manifestPath);
      library.setLibraryPath(libraryPath);
      if (mlir::failed(library.loadManifest(manifestPath))) {
        LLVM_DEBUG(llvm::dbgs() << "Warning: Failed to load module library manifest\n");
      }
    } else {
      LLVM_DEBUG(llvm::dbgs() << "Warning: Module library manifest not found\n");
    }
  }

  // Reset instance counter (per-module state is cleared in processModule)
  instanceCounter_ = 0;

  // Process each module
  for (auto &op : circuit.getBody().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "=== TokenRTLGen Pass Complete ===\n");
}

} // end anonymous namespace

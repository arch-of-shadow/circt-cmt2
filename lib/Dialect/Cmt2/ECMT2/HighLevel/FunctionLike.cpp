//===- FunctionLike.cpp - High-Level Function Implementation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Attributes.h"

using namespace circt;
using namespace cmt2::ecmt2::highlevel;

//===----------------------------------------------------------------------===//
// BundleTypeDescriptor Implementation
//===----------------------------------------------------------------------===//

mlir::Type BundleTypeDescriptor::toMLIRType(mlir::MLIRContext *ctx) const {
  llvm::SmallVector<firrtl::BundleType::BundleElement> elements;

  for (const auto &field : fields_) {
    auto nameAttr = mlir::StringAttr::get(ctx, field.name);
    firrtl::FIRRTLBaseType fieldType;

    if (field.kind == "uint") {
      fieldType = firrtl::UIntType::get(ctx, field.width);
    } else if (field.kind == "sint") {
      fieldType = firrtl::SIntType::get(ctx, field.width);
    } else if (field.kind == "vector") {
      // Create element type for vector
      firrtl::FIRRTLBaseType elemType;
      if (field.elemKind == "uint") {
        elemType = firrtl::UIntType::get(ctx, field.width);
      } else if (field.elemKind == "sint") {
        elemType = firrtl::SIntType::get(ctx, field.width);
      } else {
        // Default to uint if unknown
        elemType = firrtl::UIntType::get(ctx, field.width);
      }
      fieldType = firrtl::FVectorType::get(elemType, field.numElems);
    } else {
      // Default to uint if unknown kind
      fieldType = firrtl::UIntType::get(ctx, field.width);
    }

    elements.push_back(firrtl::BundleType::BundleElement(nameAttr, field.isFlip, fieldType));
  }

  return firrtl::BundleType::get(ctx, elements);
}

// Note: getElements() needs to be implemented differently since it needs context
// For now, users should use toMLIRType() and cast to BundleType to get elements

//===----------------------------------------------------------------------===//
// VectorTypeDescriptor Implementation
//===----------------------------------------------------------------------===//

mlir::Type VectorTypeDescriptor::toMLIRType(mlir::MLIRContext *ctx) const {
  firrtl::FIRRTLBaseType elemType;

  if (elemKind_ == "uint") {
    elemType = firrtl::UIntType::get(ctx, elemWidth_);
  } else if (elemKind_ == "sint") {
    elemType = firrtl::SIntType::get(ctx, elemWidth_);
  } else {
    // Default to uint
    elemType = firrtl::UIntType::get(ctx, elemWidth_);
  }

  return firrtl::FVectorType::get(elemType, numElems_);
}

// Rule implementation
void Rule::init(Cmt2Module *parent, llvm::StringRef name) {
  name_ = name.str();

  // Create low-level rule
  auto *lowLevelRule = parent->lowLevelModule()->addRule(name);

  // Set guard if defined
  if (guardFn_) {
    lowLevelRule->guard([this, parent](mlir::OpBuilder &builder) {
      // Set up build context for implicit helper functions
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      guardFn_(builder);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  // Set body if defined
  if (bodyFn_) {
    lowLevelRule->body([this, parent](mlir::OpBuilder &builder) {
      // Set up build context for implicit helper functions
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      bodyFn_(builder);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  lowLevelRule->finalize();
}

//===----------------------------------------------------------------------===//
// CustomValue Implementation
//===----------------------------------------------------------------------===//

void CustomValue::init(Cmt2Module *parent, llvm::StringRef name) {
  name_ = name.str();

  auto *ctx = parent->lowLevelModule()->getBuilder().getContext();

  // Determine result types from descriptors
  llvm::SmallVector<mlir::Type, 1> resultTypes;
  if (hasCustomReturnType_) {
    if (returnTypeKind_ == TypeKind::Bundle) {
      resultTypes.push_back(returnTypeDesc_.toMLIRType(ctx));
    } else {
      resultTypes.push_back(vectorReturnTypeDesc_.toMLIRType(ctx));
    }
  }

  // Create low-level value
  auto *lowLevelValue = parent->lowLevelModule()->addValue(name, resultTypes);

  // Set guard if defined
  if (guardFn_) {
    lowLevelValue->guard([this, parent](mlir::OpBuilder &builder) {
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      guardFn_(builder);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  // Set body if defined
  if (bodyFn_) {
    lowLevelValue->body([this, parent](mlir::OpBuilder &builder) {
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      bodyFn_(builder);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  lowLevelValue->finalize();
}

//===----------------------------------------------------------------------===//
// CustomMethod Implementation
//===----------------------------------------------------------------------===//

void CustomMethod::init(Cmt2Module *parent, llvm::StringRef name) {
  name_ = name.str();

  auto *ctx = parent->lowLevelModule()->getBuilder().getContext();

  // Build argument types from descriptors
  llvm::SmallVector<std::pair<std::string, mlir::Type>, 4> argTypes;
  size_t bundleIdx = 0;
  size_t vectorIdx = 0;

  for (size_t i = 0; i < argTypeKinds_.size(); ++i) {
    std::string argName = i < argNames_.size() ? argNames_[i] : ("arg" + std::to_string(i));

    if (argTypeKinds_[i] == TypeKind::Bundle) {
      argTypes.push_back({argName, argTypeDescs_[bundleIdx++].toMLIRType(ctx)});
    } else {
      argTypes.push_back({argName, vectorArgTypeDescs_[vectorIdx++].toMLIRType(ctx)});
    }
  }

  // Build result types from descriptors
  llvm::SmallVector<mlir::Type, 1> resultTypes;
  if (!returnTypeDescs_.empty()) {
    resultTypes.push_back(returnTypeDescs_[0].toMLIRType(ctx));
  } else if (!vectorReturnTypeDescs_.empty()) {
    resultTypes.push_back(vectorReturnTypeDescs_[0].toMLIRType(ctx));
  }

  // Create low-level method
  auto *lowLevelMethod = parent->lowLevelModule()->addMethod(name, argTypes, resultTypes);

  // Set guard if defined
  if (guardFn_) {
    lowLevelMethod->guard([this, parent](mlir::OpBuilder &builder,
                                          llvm::ArrayRef<mlir::BlockArgument> args) {
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      guardFn_(builder, args);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  // Set body if defined
  if (bodyFn_) {
    lowLevelMethod->body([this, parent](mlir::OpBuilder &builder,
                                         llvm::ArrayRef<mlir::BlockArgument> args) {
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      bodyFn_(builder, args);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  lowLevelMethod->finalize();
}

// Template instantiations would go here for common types
// But since these use lambdas extensively, we keep them header-only

//===- Registry.h - Member Registration System -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Macro-based member registration for automatic initialization
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_REGISTRY_H
#define CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_REGISTRY_H

#include "llvm/ADT/StringRef.h"
#include <functional>
#include <string>
#include <vector>

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

class Cmt2Module;

/// Type-erased member initializer
struct MemberInitializer {
  std::string name;
  std::function<void(Cmt2Module *)> initFn;

  MemberInitializer(llvm::StringRef n, std::function<void(Cmt2Module *)> fn)
      : name(n.str()), initFn(std::move(fn)) {}
};

/// Registry for tracking module members
class MemberRegistry {
public:
  void registerMember(llvm::StringRef name,
                      std::function<void(Cmt2Module *)> initFn) {
    members_.emplace_back(name, std::move(initFn));
  }

  void initializeAll(Cmt2Module *module) {
    for (auto &member : members_) {
      member.initFn(module);
    }
  }

  void clear() { members_.clear(); }

private:
  std::vector<MemberInitializer> members_;
};

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

/// Registration macros

// Helper macro to register a member in constructor
#define CMT2_REGISTER(member)                                                  \
  getRegistry().registerMember(#member, [this](auto *mod) { member.init(mod, #member); })

/// Convenience macros for declaring members at class scope
/// Use these to declare Rule/Value/Method members, then register in constructor with CMT2_REGISTER

// Declare a Rule member
// Usage at class scope: CMT2_DECL_RULE(ruleName);
#define CMT2_DECL_RULE(name)                                                   \
  ::circt::cmt2::ecmt2::highlevel::Rule name

// Declare a Value member
// Usage at class scope: CMT2_DECL_VALUE(RetType, valueName);
#define CMT2_DECL_VALUE(RetType, name)                                         \
  ::circt::cmt2::ecmt2::highlevel::Value<RetType> name

// Declare a Method member
// Usage at class scope: CMT2_DECL_METHOD(RetType, methodName, Args...);
#define CMT2_DECL_METHOD(RetType, name, ...)                                   \
  ::circt::cmt2::ecmt2::highlevel::Method<RetType, __VA_ARGS__> name

/// Alternative: Combined declaration and initialization helper
/// This is a less boilerplate-heavy alternative, though less explicit

// Wrapper that returns a reference for fluent API
// This allows: auto& rule = INIT_RULE(myRule).guard(...).body(...);
#define INIT_RULE(member) (CMT2_REGISTER(member), member)
#define INIT_VALUE(member) (CMT2_REGISTER(member), member)
#define INIT_METHOD(member) (CMT2_REGISTER(member), member)

// Macros for custom types (bundles/vectors)
// These use the same registration mechanism but work with CustomMethod/CustomValue
#define INIT_CUSTOM_VALUE(member) (CMT2_REGISTER(member), member)
#define INIT_CUSTOM_METHOD(member) (CMT2_REGISTER(member), member)

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_REGISTRY_H

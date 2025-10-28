//===- Input.h - High-Level Input Templates --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// High-level input templates for module parameters
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INPUT_H
#define CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INPUT_H

#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Registry.h"
#include "llvm/ADT/StringRef.h"
#include <optional>
#include <string>

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

class Cmt2Module;

/// Input port wrapper template with auto-registration support
template <typename T>
class Input {
public:
  Input() = default;
  explicit Input(llvm::StringRef name) : name_(name.str()) {}

  /// Get the underlying value
  T get() const {
    assert(value_.has_value() && "Input not initialized");
    return *value_;
  }
  operator T() const { return get(); }

  /// Set the value (used during module initialization)
  void set(const T &value) {
    value_ = value;
  }

  llvm::StringRef name() const { return name_; }

  /// Initialize the argument (called by registry)
  void init(Cmt2Module *parent, llvm::StringRef name);

private:
  std::string name_;
  std::optional<T> value_;
};

/// Specialization for Clock type
template <>
inline void Input<Clock>::init(Cmt2Module *parent, llvm::StringRef name) {
  name_ = name.str();
  value_ = parent->lowLevelModule()->addClockArgument(name);
}

/// Specialization for Reset type
template <>
inline void Input<Reset>::init(Cmt2Module *parent, llvm::StringRef name) {
  name_ = name.str();
  value_ = parent->lowLevelModule()->addResetArgument(name);
}

/// Convenience type aliases for common inputs
using ClockInput = Input<Clock>;
using ResetInput = Input<Reset>;

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

/// Macros for declaring and auto-registering arguments

// Declare a Clock argument
// Usage in constructor: CMT2_ARG_CLOCK(clk);
#define CMT2_ARG_CLOCK(name)                                                   \
  getRegistry().registerMember(#name, [this](auto *mod) { name.init(mod, #name); })

// Declare a Reset argument
// Usage in constructor: CMT2_ARG_RESET(rst);
#define CMT2_ARG_RESET(name)                                                   \
  getRegistry().registerMember(#name, [this](auto *mod) { name.init(mod, #name); })

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INPUT_H

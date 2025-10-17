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
#include "llvm/ADT/StringRef.h"
#include <string>

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

/// Input port wrapper template
template <typename T>
class Input {
public:
  Input() = default;
  explicit Input(llvm::StringRef name) : name_(name.str()) {}

  /// Get the underlying value
  T get() const { return value_; }
  operator T() const { return value_; }

  /// Set the value (used during module initialization)
  void set(const T &value) { value_ = value; }

  llvm::StringRef name() const { return name_; }

private:
  std::string name_;
  T value_;
};

/// Convenience type aliases for common inputs
using ClockInput = Input<Clock>;
using ResetInput = Input<Reset>;

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INPUT_H

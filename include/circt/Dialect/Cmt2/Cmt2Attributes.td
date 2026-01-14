//===- Cmt2Attributes.td - Cmt2 Dialect Attributes -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Cmt2 dialect custom attributes, including timing
// attributes for cycle-precise control inspired by Calyx.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_Cmt2_Cmt2ATTRIBUTES_TD
#define CIRCT_DIALECT_Cmt2_Cmt2ATTRIBUTES_TD

// NOTE: Cmt2Dialect.td must be included before this file in the parent .td file
// We don't include it here to avoid duplicate def errors in TableGen.
include "mlir/IR/EnumAttr.td"
include "mlir/IR/BuiltinAttributeInterfaces.td"

//===----------------------------------------------------------------------===//
// Timing Attributes for Cycle-Precise Control
//===----------------------------------------------------------------------===//
//
// These attributes support Calyx-style cycle-precise timing on method
// signatures and call sites. They enable:
//
// 1. Method signature timing: Declare when inputs are required and outputs
//    are available (e.g., `{latency = 4}` means output at cycle 4)
//
// 2. Call-site timing guards: Specify when arguments are driven and results
//    captured (e.g., `{timing = [0, 1]}` means cycles 0 through 0)
//
// 3. Timing validation: Verify call timing matches method contracts
//
// See docs/Dialects/Cmt2/Cmt2ProcVsCalyx.md for design rationale.
//===----------------------------------------------------------------------===//

/// TimingIntervalAttr represents a half-open cycle interval [start, end).
/// Used on call arguments and results to specify when values are driven/captured.
///
/// Examples:
///   #cmt2.timing<[0, 1]>   - cycle 0 only
///   #cmt2.timing<[2, 5]>   - cycles 2, 3, 4
///   #cmt2.timing<5>        - shorthand for [5, 6]
def TimingIntervalAttr : AttrDef<Cmt2Dialect, "TimingInterval"> {
  let summary = "Timing interval attribute for cycle-precise scheduling";
  let description = [{
    Represents a half-open cycle interval [start, end) relative to the
    enclosing static step's activation. Used on call arguments and results
    to specify when values are driven or captured.

    The interval specifies the clock cycles during which:
    - For arguments: the value is driven to the callee
    - For results: the value is captured from the callee

    Examples:
    ```mlir
    // Drive argument during cycle 0
    %r = cmt2.call @inst @method(%a {timing = #cmt2.timing<[0, 1]>})

    // Capture result during cycle 4
    %r = cmt2.call @inst @method() -> (!firrtl.uint<32> {timing = #cmt2.timing<[4, 5]>})
    ```
  }];

  let mnemonic = "timing";

  let parameters = (ins
    "int64_t":$start,
    "int64_t":$end
  );

  let assemblyFormat = [{
    `<` `[` $start `,` $end `]` `>`
  }];

  let extraClassDeclaration = [{
    /// Get the duration of this interval in cycles
    int64_t getDuration() const { return getEnd() - getStart(); }

    /// Check if this interval is valid (start < end, both non-negative)
    bool isValid() const { return getStart() >= 0 && getStart() < getEnd(); }

    /// Check if a given cycle is within this interval
    bool containsCycle(int64_t cycle) const {
      return cycle >= getStart() && cycle < getEnd();
    }

    /// Check if this interval overlaps with another
    bool overlaps(TimingIntervalAttr other) const {
      return getStart() < other.getEnd() && other.getStart() < getEnd();
    }

    /// Create a single-cycle interval [cycle, cycle+1)
    static TimingIntervalAttr getSingleCycle(mlir::MLIRContext *ctx, int64_t cycle) {
      return get(ctx, cycle, cycle + 1);
    }
  }];

  let genVerifyDecl = 1;
}

/// LatencyAttr specifies when a port's value becomes available/required.
/// Used on method signature ports.
///
/// Examples:
///   #cmt2.latency<0>   - available/required at cycle 0 (combinational)
///   #cmt2.latency<4>   - available/required at cycle 4
def LatencyAttr : AttrDef<Cmt2Dialect, "Latency"> {
  let summary = "Port latency attribute";
  let description = [{
    Specifies the cycle at which a port's value is available (for outputs)
    or required (for inputs). Used on method signature ports.

    For input ports: the cycle by which the input must be stable
    For output ports: the cycle at which the output becomes valid

    Examples:
    ```mlir
    // Method with 4-cycle latency: inputs at 0, output at 4
    cmt2.bind.method @multiply : (
      !firrtl.uint<32> {cmt2.latency = #cmt2.latency<0>},
      !firrtl.uint<32> {cmt2.latency = #cmt2.latency<0>}
    ) -> (!firrtl.uint<32> {cmt2.latency = #cmt2.latency<4>})
    ```
  }];

  let mnemonic = "latency";

  let parameters = (ins "int64_t":$cycles);

  let assemblyFormat = [{ `<` $cycles `>` }];

  let extraClassDeclaration = [{
    /// Check if this is zero latency (combinational)
    bool isCombinational() const { return getCycles() == 0; }
  }];

  let genVerifyDecl = 1;
}

/// IntervalAttr specifies the initiation interval (II) for pipelined methods.
/// This is the minimum number of cycles between consecutive invocations.
///
/// Example:
///   #cmt2.interval<3>   - can start new invocation every 3 cycles
def IntervalAttr : AttrDef<Cmt2Dialect, "Interval"> {
  let summary = "Initiation interval attribute for pipelined methods";
  let description = [{
    Specifies the initiation interval (II) for pipelined methods. This is
    the minimum number of cycles that must elapse between consecutive
    invocations of the method.

    For fully pipelined designs, II = 1 means a new input can be accepted
    every cycle. For methods with resource constraints, II may be larger.

    Example:
    ```mlir
    // Pipelined multiplier: 4-cycle latency, II=3
    cmt2.bind.method @multiply {cmt2.interval = #cmt2.interval<3>} : ...
    ```
  }];

  let mnemonic = "interval";

  let parameters = (ins "int64_t":$cycles);

  let assemblyFormat = [{ `<` $cycles `>` }];

  let extraClassDeclaration = [{
    /// Check if fully pipelined (II = 1)
    bool isFullyPipelined() const { return getCycles() == 1; }
  }];

  let genVerifyDecl = 1;
}

//===----------------------------------------------------------------------===//
// Token Mode Enum (for SyncToken)
//===----------------------------------------------------------------------===//

/// TokenMode enum for synchronization token timing mode
def Cmt2TokenMode : I32EnumAttr<"TokenMode", "Token timing mode", [
  I32EnumAttrCase<"LS", 0, "ls">,   // Latency-sensitive: fixed timing, shift register
  I32EnumAttrCase<"LI", 1, "li">,   // Latency-insensitive: variable timing, FIFO
]> {
  let cppNamespace = "::circt::cmt2";
}

//===----------------------------------------------------------------------===//
// Port Kind Enum and Attributes
//===----------------------------------------------------------------------===//

/// PortKind enum for port timing attributes (similar to Calyx's @go, @done, etc.)
def Cmt2PortKind : I32EnumAttr<"PortKind", "Port timing kind", [
  I32EnumAttrCase<"Data", 0, "data">,      // Pure data port (no timing implications)
  I32EnumAttrCase<"Go", 1, "go">,          // Go/enable signal
  I32EnumAttrCase<"Done", 2, "done">,      // Done/completion signal
  I32EnumAttrCase<"Stable", 3, "stable">,  // Output is latched/registered
]> {
  let cppNamespace = "::circt::cmt2";
}

/// PortTimingAttr combines port kind with optional latency.
/// Provides Calyx-style port annotations.
///
/// Examples:
///   #cmt2.port<go>           - go signal
///   #cmt2.port<done, 4>      - done signal at cycle 4
///   #cmt2.port<stable>       - stable/latched output
///   #cmt2.port<data>         - pure data port
def PortTimingAttr : AttrDef<Cmt2Dialect, "PortTiming"> {
  let summary = "Port timing attribute combining kind and optional latency";
  let description = [{
    Combines a port kind (go, done, stable, data) with an optional latency
    value. This provides Calyx-style port timing annotations.

    Port kinds:
    - `data`: Pure data port with no special timing semantics
    - `go`: Go/enable signal that triggers method execution
    - `done`: Done signal indicating completion (with optional cycle)
    - `stable`: Output is latched/registered (not combinationally dependent on inputs)

    Examples:
    ```mlir
    cmt2.bind.method @multiply : (
      !firrtl.uint<1>  {cmt2.port = #cmt2.port<go>},           // go signal
      !firrtl.uint<32> {cmt2.port = #cmt2.port<data>},         // data input
      !firrtl.uint<32> {cmt2.port = #cmt2.port<data>}          // data input
    ) -> (
      !firrtl.uint<1>  {cmt2.port = #cmt2.port<done, 4>},      // done at cycle 4
      !firrtl.uint<32> {cmt2.port = #cmt2.port<stable>}        // stable output
    )
    ```
  }];

  let mnemonic = "port";

  let parameters = (ins
    "PortKind":$kind,
    OptionalParameter<"std::optional<int64_t>">:$latency
  );

  let assemblyFormat = [{
    `<` $kind (`,` $latency^)? `>`
  }];

  let builders = [
    // Convenience builder for port kind without latency
    AttrBuilder<(ins "PortKind":$kind), [{
      return $_get($_ctxt, kind, std::nullopt);
    }]>,
    // Convenience builder for port kind with latency
    AttrBuilder<(ins "PortKind":$kind, "int64_t":$latency), [{
      return $_get($_ctxt, kind, std::optional<int64_t>(latency));
    }]>
  ];

  let extraClassDeclaration = [{
    /// Check if this is a go signal
    bool isGo() const { return getKind() == PortKind::Go; }

    /// Check if this is a done signal
    bool isDone() const { return getKind() == PortKind::Done; }

    /// Check if this is a stable output
    bool isStable() const { return getKind() == PortKind::Stable; }

    /// Check if this is a data port
    bool isData() const { return getKind() == PortKind::Data; }

    /// Check if this port has an associated latency
    bool hasLatency() const { return getLatency().has_value(); }

    /// Get the latency value (returns 0 if not set)
    int64_t getLatencyOrZero() const {
      return getLatency().value_or(0);
    }
  }];
}

/// TimingArrayAttr is an array of TimingIntervalAttr, one per argument/result.
/// Used on CallOp to specify per-operand timing.
def TimingArrayAttr : TypedArrayAttrBase<TimingIntervalAttr,
  "array of timing interval attributes">;

/// PortTimingArrayAttr is an array of PortTimingAttr, one per port.
/// Used on method signatures.
def PortTimingArrayAttr : TypedArrayAttrBase<PortTimingAttr,
  "array of port timing attributes">;

/// LatencyArrayAttr is an array of LatencyAttr, one per port.
def LatencyArrayAttr : TypedArrayAttrBase<LatencyAttr,
  "array of latency attributes">;

#endif // CIRCT_DIALECT_Cmt2_Cmt2ATTRIBUTES_TD

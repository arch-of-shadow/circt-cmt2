//===- Cmt2Types.td - Cmt2 Dialect Types -------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Cmt2 dialect custom types.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_Cmt2_Cmt2TYPES_TD
#define CIRCT_DIALECT_Cmt2_Cmt2TYPES_TD

include "mlir/IR/AttrTypeBase.td"

// NOTE: Cmt2Dialect.td and Cmt2Attributes.td must be included before this file
// in the parent .td file. We don't include them here to avoid duplicate def errors.

/// Base class for Cmt2 dialect types
class Cmt2TypeDef<string name> : TypeDef<Cmt2Dialect, name> {}

//===----------------------------------------------------------------------===//
// SyncToken Type
//===----------------------------------------------------------------------===//

def SyncTokenType : Cmt2TypeDef<"SyncToken"> {
  let mnemonic = "sync_token";

  let summary = "Synchronization token type for pipelined dataflow";

  let description = [{
    A first-class SSA value representing a synchronization point in a
    pipelined dataflow design. SyncTokens enable token-based synchronization
    between rules, supporting both latency-sensitive (LS) and latency-insensitive
    (LI) communication patterns.

    A SyncToken optionally carries:
    1. **Data payload**: Values passed between producer and consumer rules
    2. **Timing mode**: LS (fixed timing, shift register) or LI (variable, FIFO)

    Tokens can have multiple consumers (fork pattern), which implies
    synchronization between all consumers on the same producer.

    Examples:
    ```mlir
    // Token without data
    !cmt2.sync_token

    // Token carrying 32-bit data (default LS mode)
    !cmt2.sync_token<data=!firrtl.uint<32>>

    // Latency-sensitive token with data
    !cmt2.sync_token<data=!firrtl.uint<32>, mode=ls>

    // Latency-insensitive token with data
    !cmt2.sync_token<data=!firrtl.uint<32>, mode=li>
    ```

    Hardware realization:
    - **LS tokens**: Lowered to shift registers with fixed delay
    - **LI tokens**: Lowered to FIFOs with depth inferred by analysis

    Multi-consumer handling:
    - **LS**: Fan-out valid/data signals; all consumers fire same cycle
    - **LI**: Broadcast FIFO with per-consumer handshake
  }];

  let parameters = (ins
    OptionalParameter<"Type">:$dataType,
    DefaultValuedParameter<"TokenMode", "TokenMode::LS">:$mode
  );

  let assemblyFormat = [{
    (`<` `data` `=` $dataType^ (`,` `mode` `=` $mode^)? `>`)?
  }];

  let extraClassDeclaration = [{
    /// Check if this token carries data
    bool hasData() const { return getDataType() != nullptr; }

    /// Check if this is a latency-sensitive token
    bool isLatencySensitive() const { return getMode() == TokenMode::LS; }

    /// Check if this is a latency-insensitive token
    bool isLatencyInsensitive() const { return getMode() == TokenMode::LI; }
  }];

  let builders = [
    // Token without data (pure synchronization)
    TypeBuilder<(ins), [{
      return $_get($_ctxt, nullptr, TokenMode::LS);
    }]>,
    // Token with data and default LS mode
    TypeBuilder<(ins "Type":$dataType), [{
      return $_get($_ctxt, dataType, TokenMode::LS);
    }]>
  ];

  let genVerifyDecl = 1;
}

/// Type constraint for any SyncToken
def AnySyncTokenType : Type<
  CPred<"llvm::isa<cmt2::SyncTokenType>($_self)">,
  "must be a !cmt2.sync_token type",
  "cmt2::SyncTokenType"
>;

#endif // CIRCT_DIALECT_Cmt2_Cmt2TYPES_TD

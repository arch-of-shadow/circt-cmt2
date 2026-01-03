//===- Cmt2Dialect.td - Cmt2 dialect definition -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the Cmt2 Dialect.
//
//===----------------------------------------------------------------------===//

def Cmt2Dialect : Dialect {
  let name = "cmt2";
  let cppNamespace = "::circt::cmt2";
  let summary = "Operations for cmt2 dialect";
  let description = [{
    This dialect defines the `cmt2` dialect.
    // TODO: add more information
  }];

  // Enable automatic attribute printer/parser generation
  let useDefaultAttributePrinterParser = 1;

  let extraClassDeclaration = [{
    /// Register all Cmt2 attributes.
    void registerAttributes();
  }];
}

// Base class for the operation in this dialect.
class Cmt2Op<
  string mnemonic,
  list<Trait> traits = []
  > : Op<Cmt2Dialect, mnemonic, traits>;
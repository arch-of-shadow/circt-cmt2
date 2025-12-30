//===- Cmt2Module.cpp - Cmt2 API nanobind module ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRCTModules.h"

#include "circt-c/Dialect/Cmt2.h"

#include "mlir/Bindings/Python/NanobindAdaptors.h"

#include <nanobind/nanobind.h>
namespace nb = nanobind;

using namespace circt;
using namespace mlir::python::nanobind_adaptors;

/// Populate the cmt2 python module.
void circt::python::populateDialectCmt2Submodule(nb::module_ &m) {
  m.doc() = "CMT2 dialect Python native extension";

  m.def(
      "register_dialect",
      [](MlirContext ctx, bool load) {
        MlirDialectHandle handle = mlirGetDialectHandle__cmt2__();
        mlirDialectHandleRegisterDialect(handle, ctx);
        if (load)
          mlirDialectHandleLoadDialect(handle, ctx);
      },
      nb::arg("context"), nb::arg("load") = true);
}

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

  //===--------------------------------------------------------------------===//
  // IntervalAttr
  //===--------------------------------------------------------------------===//
  mlir_attribute_subclass(m, "IntervalAttr", circtCmt2IntervalAttrIsA)
      .def_classmethod(
          "get",
          [](nb::object cls, MlirContext ctx, int64_t cycles) {
            return cls(circtCmt2IntervalAttrGet(ctx, cycles));
          },
          nb::arg("cls"), nb::arg("context"), nb::arg("cycles"),
          "Create an IntervalAttr with the given number of cycles.")
      .def_property_readonly(
          "cycles",
          [](MlirAttribute self) {
            return circtCmt2IntervalAttrGetCycles(self);
          },
          "Get the number of cycles.");

  //===--------------------------------------------------------------------===//
  // LatencyAttr
  //===--------------------------------------------------------------------===//
  mlir_attribute_subclass(m, "LatencyAttr", circtCmt2LatencyAttrIsA)
      .def_classmethod(
          "get",
          [](nb::object cls, MlirContext ctx, int64_t cycles) {
            return cls(circtCmt2LatencyAttrGet(ctx, cycles));
          },
          nb::arg("cls"), nb::arg("context"), nb::arg("cycles"),
          "Create a LatencyAttr with the given number of cycles.")
      .def_property_readonly(
          "cycles",
          [](MlirAttribute self) {
            return circtCmt2LatencyAttrGetCycles(self);
          },
          "Get the number of cycles.");

  //===--------------------------------------------------------------------===//
  // TimingIntervalAttr
  //===--------------------------------------------------------------------===//
  mlir_attribute_subclass(m, "TimingIntervalAttr",
                          circtCmt2TimingIntervalAttrIsA)
      .def_classmethod(
          "get",
          [](nb::object cls, MlirContext ctx, int64_t start, int64_t end) {
            return cls(circtCmt2TimingIntervalAttrGet(ctx, start, end));
          },
          nb::arg("cls"), nb::arg("context"), nb::arg("start"), nb::arg("end"),
          "Create a TimingIntervalAttr with the given start and end cycles.")
      .def_property_readonly(
          "start",
          [](MlirAttribute self) {
            return circtCmt2TimingIntervalAttrGetStart(self);
          },
          "Get the start cycle.")
      .def_property_readonly(
          "end",
          [](MlirAttribute self) {
            return circtCmt2TimingIntervalAttrGetEnd(self);
          },
          "Get the end cycle.");
}

//===- FIRRTLModule.cpp - FIRRTL API nanobind module ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRCTModules.h"

#include "circt-c/Dialect/FIRRTL.h"

#include "mlir-c/BuiltinAttributes.h"
#include "mlir/Bindings/Python/NanobindAdaptors.h"

#include "NanobindUtils.h"
#include "mlir-c/Support.h"
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

namespace nb = nanobind;

using namespace circt;
using namespace mlir::python::nanobind_adaptors;

/// Populate the firrtl python module.
void circt::python::populateDialectFIRRTLSubmodule(nb::module_ &m) {
  m.doc() = "FIRRTL dialect Python native extension";

  //===--------------------------------------------------------------------===//
  // Type API
  //===--------------------------------------------------------------------===//

  mlir_type_subclass(m, "UIntType", firrtlTypeIsAUInt)
      .def_classmethod(
          "get",
          [](nb::object cls, MlirContext ctx, int32_t width) {
            return cls(firrtlTypeGetUInt(ctx, width));
          },
          nb::arg("cls"), nb::arg("context"), nb::arg("width"));

  mlir_type_subclass(m, "SIntType", firrtlTypeIsASInt)
      .def_classmethod(
          "get",
          [](nb::object cls, MlirContext ctx, int32_t width) {
            return cls(firrtlTypeGetSInt(ctx, width));
          },
          nb::arg("cls"), nb::arg("context"), nb::arg("width"));

  mlir_type_subclass(m, "ClockType", firrtlTypeIsAClock)
      .def_classmethod("get",
                       [](nb::object cls, MlirContext ctx) {
                         return cls(firrtlTypeGetClock(ctx));
                       },
                       nb::arg("cls"), nb::arg("context"));

  mlir_type_subclass(m, "ResetType", firrtlTypeIsAReset)
      .def_classmethod("get",
                       [](nb::object cls, MlirContext ctx) {
                         return cls(firrtlTypeGetReset(ctx));
                       },
                       nb::arg("cls"), nb::arg("context"));

  mlir_type_subclass(m, "AsyncResetType", firrtlTypeIsAAsyncReset)
      .def_classmethod("get",
                       [](nb::object cls, MlirContext ctx) {
                         return cls(firrtlTypeGetAsyncReset(ctx));
                       },
                       nb::arg("cls"), nb::arg("context"));

  mlir_type_subclass(m, "AnalogType", firrtlTypeIsAAnalog)
      .def_classmethod(
          "get",
          [](nb::object cls, MlirContext ctx, int32_t width) {
            return cls(firrtlTypeGetAnalog(ctx, width));
          },
          nb::arg("cls"), nb::arg("context"), nb::arg("width"));

  mlir_type_subclass(m, "VectorType", firrtlTypeIsAVector)
      .def_classmethod(
          "get",
          [](nb::object cls, MlirType elementType, size_t count) {
            MlirContext ctx = mlirTypeGetContext(elementType);
            return cls(firrtlTypeGetVector(ctx, elementType, count));
          },
          nb::arg("cls"), nb::arg("element_type"), nb::arg("count"))
      .def_property_readonly(
          "element_type",
          [](MlirType self) { return firrtlTypeGetVectorElement(self); })
      .def_property_readonly("num_elements", [](MlirType self) {
        return firrtlTypeGetVectorNumElements(self);
      });

  mlir_type_subclass(m, "BundleType", firrtlTypeIsABundle)
      .def_classmethod(
          "get",
          [](nb::object cls, MlirContext ctx,
             nb::list fieldInfos) {
            llvm::SmallVector<FIRRTLBundleField> fields;
            for (size_t i = 0, e = fieldInfos.size(); i < e; ++i) {
              auto tuple = nb::cast<nb::tuple>(fieldInfos[i]);
              std::string name = nb::cast<std::string>(tuple[0]);
              MlirType type = nb::cast<MlirType>(tuple[1]);
              bool isFlip = nb::cast<bool>(tuple[2]);
              MlirIdentifier nameId = mlirIdentifierGet(
                  ctx, mlirStringRefCreateFromCString(name.c_str()));
              fields.push_back({nameId, isFlip, type});
            }
            return cls(firrtlTypeGetBundle(ctx, fields.size(), fields.data()));
          },
          nb::arg("cls"), nb::arg("context"), nb::arg("fields"))
      .def_property_readonly("num_fields", [](MlirType self) {
        return firrtlTypeGetBundleNumFields(self);
      });
}

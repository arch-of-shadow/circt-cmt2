#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""CMT2 dialect Python bindings."""

from __future__ import annotations

from .._mlir_libs._circt._cmt2 import *
from ..dialects._ods_common import _cext as _ods_cext
from ..ir import *
from ._cmt2_ops_gen import *
from ._cmt2_ops_gen import _Dialect


@_ods_cext.register_operation(_Dialect, replace=True)
class CircuitOp(CircuitOp):
    """CMT2 Circuit operation wrapper."""

    def __init__(self, *, loc=None, ip=None):
        super().__init__(loc=loc, ip=ip)

    @property
    def body(self):
        return self.regions[0].blocks[0]


@_ods_cext.register_operation(_Dialect, replace=True)
class ModuleOp(ModuleOp):
    """CMT2 Module operation wrapper."""

    @property
    def body(self):
        return self.regions[0].blocks[0]

    @property
    def entry_block(self):
        return self.regions[0].blocks[0]


@_ods_cext.register_operation(_Dialect, replace=True)
class RuleOp(RuleOp):
    """CMT2 Rule operation wrapper."""

    @property
    def guard_block(self):
        return self.regions[0].blocks[0]

    @property
    def body_block(self):
        return self.regions[1].blocks[0]


@_ods_cext.register_operation(_Dialect, replace=True)
class MethodOp(MethodOp):
    """CMT2 Method operation wrapper."""

    @property
    def guard_block(self):
        return self.regions[0].blocks[0]

    @property
    def body_block(self):
        return self.regions[1].blocks[0]


@_ods_cext.register_operation(_Dialect, replace=True)
class ValueOp(ValueOp):
    """CMT2 Value operation wrapper."""

    @property
    def guard_block(self):
        return self.regions[0].blocks[0]

    @property
    def body_block(self):
        return self.regions[1].blocks[0]


@_ods_cext.register_operation(_Dialect, replace=True)
class ProcRuleOp(ProcRuleOp):
    """CMT2 Procedural Rule operation wrapper."""

    @property
    def guard_block(self):
        return self.regions[0].blocks[0]

    @property
    def control_block(self):
        return self.regions[1].blocks[0]


@_ods_cext.register_operation(_Dialect, replace=True)
class ProcMethodOp(ProcMethodOp):
    """CMT2 Procedural Method operation wrapper."""

    @property
    def guard_block(self):
        return self.regions[0].blocks[0]

    @property
    def control_block(self):
        return self.regions[1].blocks[0]


@_ods_cext.register_operation(_Dialect, replace=True)
class ProcGroupOp(ProcGroupOp):
    """CMT2 Procedural Group operation wrapper."""

    @property
    def body_block(self):
        return self.regions[0].blocks[0]


@_ods_cext.register_operation(_Dialect, replace=True)
class ProcStaticGroupOp(ProcStaticGroupOp):
    """CMT2 Procedural Static Group operation wrapper."""

    @property
    def body_block(self):
        return self.regions[0].blocks[0]

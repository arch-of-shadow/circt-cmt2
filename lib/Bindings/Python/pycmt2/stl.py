#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Standard Library (STL) module wrappers for PyCMT2.

This module provides Python wrappers for common hardware components
that can be instantiated in CMT2 designs. These match the C++ STLLibrary
bindings in lib/Dialect/Cmt2/ECMT2/STLLibrary.cpp.

Key differences from old implementation:
- Reg and Wire are external modules (bindings to ModuleLibrary)
- FIFOs are CMT2 modules built from Reg and Wire primitives
- Memory modules are external modules (bindings to ModuleLibrary)
- No inline RTL generation - RTL comes from ModuleLibrary

Example:
    from pycmt2 import Circuit
    from pycmt2.stl import Reg, FIFO1Push

    circuit = Circuit("Counter")

    with circuit.module("Counter") as m:
        clk = m.clock()
        rst = m.reset()

        # Create a 32-bit register using STL
        reg_mod = Reg.create(circuit, 32)
        count = m.instance(reg_mod, "count", clk=clk, rst=rst)

        with m.rule("increment") as r:
            with r.guard() as g:
                g.always()
            with r.body() as body:
                val = body.call(count, "read")
                new_val = body.add(val, body.const(1, 32))
                body.call(count, "write", body.truncate(new_val, 32))
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from .types import UInt

if TYPE_CHECKING:
    from .circuit import Circuit
    from .external_module import ExternalModuleBuilder
    from .module import ModuleBuilder


# =============================================================================
# External Module Components (Reg, Wire, Memory)
#
# These are external modules that bind to FIRRTL modules from ModuleLibrary.
# No inline RTL generation - the RTL comes from the ModuleLibrary.
# =============================================================================

class Reg:
    """Factory for register external modules.

    Creates an external module matching C++ STLLibrary::createRegModule:
    - clock port: "clk"
    - reset port: "rst"
    - value method: "read" -> returns data
    - action method: "write" <- takes data
    - sequence_before("read", "write")

    Example:
        reg_mod = Reg.create(circuit, 32)
        count = m.instance(reg_mod, "count", clk=clk, rst=rst)

        # In a rule body:
        val = body.call(count, "read")
        body.call(count, "write", val + body.const(1, 32))
    """

    @staticmethod
    def create(circuit: Circuit, width: int, init: int = 0) -> ExternalModuleBuilder:
        """Create a register external module.

        Args:
            circuit: The circuit to add the module to.
            width: Bit width of the register.
            init: Initial value (default 0).

        Returns:
            The ExternalModuleBuilder for the register.
        """
        # Use parameterized name to allow reuse (include init to avoid cache collision)
        name = f"FIRRTLReg_{width}_i{init}"

        # Check if already exists
        if name in circuit._external_modules:
            return circuit._external_modules[name]

        # Include the FIRRTL module from ModuleLibrary
        # This builds from Chisel and inserts the firrtl.module into the circuit
        actual_name = circuit.include_library_module(
            "FIRRTLReg", {"width": width, "init": init}
        )
        if actual_name:
            # Use the actual name from the library (e.g., "Reg_width32_init0")
            firrtl_module_name = actual_name
        else:
            # Fallback if library not available
            firrtl_module_name = f"Reg_width{width}_init{init}"

        # Create the external module matching ECMT2 STLLibrary::createRegModule
        with circuit.external_module(name) as reg:
            # Set the FIRRTL module name to match the generated module
            reg.set_firrtl_module_name(firrtl_module_name)
            reg.clock("clk")
            reg.reset("rst")
            reg.value("read",
                      ready_name="read_ready",
                      returns=[("read_data", UInt(width))])
            reg.method("write",
                       enable_name="write_enable",
                       ready_name="write_ready",
                       args=[("write_data", UInt(width))])
            reg.sequence_before("read", "write")

        return circuit._external_modules[name]


class Wire:
    """Factory for wire external modules.

    Creates an external module matching C++ STLLibrary::createWireModule:
    - value method: "read" -> returns data
    - action method: "write" <- takes data
    - sequence_before("write", "read")
    - conflict("write", "write")

    Note: Wire is a combinational module and does NOT have clock/reset ports.

    Example:
        wire_mod = Wire.create(circuit, 32)
        temp = m.instance(wire_mod, "temp")
    """

    @staticmethod
    def create(circuit: Circuit, width: int) -> ExternalModuleBuilder:
        """Create a wire external module.

        Args:
            circuit: The circuit to add the module to.
            width: Bit width of the wire.

        Returns:
            The ExternalModuleBuilder for the wire.
        """
        name = f"Wire_{width}"

        if name in circuit._external_modules:
            return circuit._external_modules[name]

        # Include the FIRRTL module from ModuleLibrary
        actual_name = circuit.include_library_module("Wire", {"width": width})
        if actual_name:
            firrtl_module_name = actual_name
        else:
            # Fallback if library not available
            firrtl_module_name = f"Wire_w{width}"

        # Create the external module matching ECMT2 STLLibrary::createWireModule
        # Note: Wire is combinational and does NOT have clock/reset ports
        with circuit.external_module(name) as wire:
            wire.set_firrtl_module_name(firrtl_module_name)
            wire.value("read",
                       ready_name="read_ready",
                       returns=[("read_data", UInt(width))])
            wire.method("write",
                        enable_name="write_enable",
                        ready_name="write_ready",
                        args=[("write_data", UInt(width))])
            wire.conflict("write", "write")
            wire.sequence_before("write", "read")

        return circuit._external_modules[name]


# =============================================================================
# CMT2 Module Components (WireDefault, FIFOs)
#
# These are CMT2 modules built from primitives (Reg, Wire).
# They match the C++ STLLibrary implementations.
# =============================================================================

class WireDefault:
    """Factory for wire with default value as CMT2 module.

    Matches ECMT2 STLLibrary::createWireDefaultModule.
    Wraps a Wire with a default value rule that always writes the init value.

    Scheduling: write < default < read

    Example:
        wire_default_mod = WireDefault.create(circuit, 1, init=0)
        flag = m.instance(wire_default_mod, "flag")
    """

    @staticmethod
    def create(circuit: Circuit, width: int, init: int = 0) -> ModuleBuilder:
        """Create a wire with default value as CMT2 module.

        Args:
            circuit: The circuit to add the module to.
            width: Bit width of the wire.
            init: Default value (default 0).

        Returns:
            The ModuleBuilder for the wire with default.
        """
        name = f"WireDefault_w{width}_i{init}"

        if name in circuit._modules:
            return circuit._modules[name]

        # Get the wire primitive
        wire_mod = Wire.create(circuit, width)

        with circuit.module(name) as wire_default:
            # Internal wire instance (Wire is combinational, no clk/rst)
            inner = wire_default.instance(wire_mod, "inner")

            # Value: read() -> data (delegates to inner)
            with wire_default.value("read", returns=[UInt(width)]) as read_val:
                with read_val.guard() as g:
                    g.always()
                with read_val.body() as body:
                    result = body.call(inner, "read")
                    body.returns(result)

            # Method: write(data) (delegates to inner)
            with wire_default.method("write", args=[("in_", UInt(width))]) as write_meth:
                with write_meth.guard() as g:
                    g.always()
                with write_meth.body() as body:
                    body.call(inner, "write", body.arg("in_"))

            # Rule: default (writes init value)
            with wire_default.rule("default") as rule:
                with rule.guard() as g:
                    g.always()
                with rule.body() as body:
                    body.call(inner, "write", body.const(init, width))

            # Precedence: write < default < read
            wire_default.precedence(
                write_meth.ref(),
                rule.ref(),
                read_val.ref()
            )

        return circuit._modules[name]


class FIFO1Push:
    """Factory for depth-1 FIFO with active push semantics as CMT2 module.

    Matches ECMT2 STLLibrary::createFIFO1PushModule.

    Built from: Reg (data), Reg (full), Wire (deqed), Wire (enqed)

    Interface:
    - value "full" -> bool: Returns true when FIFO is full
    - method "deq" -> data: Dequeue data (guard: full)
    - method "enq" (data): Enqueue data (guard: !full | deqed)

    The "push" semantics means:
    - Producer (enq) is guarded by FIFO state
    - Consumer (deq) can always dequeue when data is available

    Example:
        fifo_mod = FIFO1Push.create(circuit, 32)
        fifo = m.instance(fifo_mod, "input_fifo")
    """

    @staticmethod
    def create(circuit: Circuit, width: int) -> ModuleBuilder:
        """Create a depth-1 push-style FIFO as CMT2 module.

        Args:
            circuit: The circuit to add the module to.
            width: Bit width of the data.

        Returns:
            The ModuleBuilder for the FIFO.
        """
        name = f"FIFO1_PUSH_w{width}"

        if name in circuit._modules:
            return circuit._modules[name]

        # Get primitives
        reg_data_mod = Reg.create(circuit, width)
        reg_bool_mod = Reg.create(circuit, 1)
        wire_bool_mod = Wire.create(circuit, 1)

        with circuit.module(name) as fifo:
            clk = fifo.clock("clk")
            rst = fifo.reset("rst")

            # Instances
            # Reg modules need clk/rst, Wire modules don't (combinational)
            reg_data = fifo.instance(reg_data_mod, "reg_data", clk=clk, rst=rst)
            full_reg = fifo.instance(reg_bool_mod, "full_reg", clk=clk, rst=rst)
            deqed = fifo.instance(wire_bool_mod, "deqed")
            enqed = fifo.instance(wire_bool_mod, "enqed")

            # Value: full() -> bool
            with fifo.value("full", returns=[UInt(1)]) as full_val:
                with full_val.guard() as g:
                    g.always()
                with full_val.body() as body:
                    result = body.call(full_reg, "read")
                    body.returns(result)

            # Method: deq() -> data
            # Guard: full_reg.read()
            with fifo.method("deq", returns=[UInt(width)]) as deq_meth:
                with deq_meth.guard() as g:
                    is_full = g.call(full_reg, "read")
                    g.returns(is_full)
                with deq_meth.body() as body:
                    body.call(deqed, "write", body.const(1, 1))
                    data = body.call(reg_data, "read")
                    body.returns(data)

            # Method: enq(data)
            # Guard: !full_reg.read() | deqed.read()
            with fifo.method("enq", args=[("data", UInt(width))]) as enq_meth:
                with enq_meth.guard() as g:
                    is_full = g.call(full_reg, "read")
                    is_deqed = g.call(deqed, "read")
                    not_full = g.not_(is_full)
                    can_enq = g.or_(not_full, is_deqed)
                    g.returns(can_enq)
                with enq_meth.body() as body:
                    body.call(enqed, "write", body.const(1, 1))
                    body.call(reg_data, "write", body.arg("data"))

            # Rule: deqed_default (write 0 to deqed wire)
            with fifo.rule("deqed_default") as deqed_default:
                with deqed_default.guard() as g:
                    g.always()
                with deqed_default.body() as body:
                    body.call(deqed, "write", body.const(0, 1))

            # Rule: enqed_default (write 0 to enqed wire)
            with fifo.rule("enqed_default") as enqed_default:
                with enqed_default.guard() as g:
                    g.always()
                with enqed_default.body() as body:
                    body.call(enqed, "write", body.const(0, 1))

            # Rule: next (update full_reg)
            # full_reg.write(enqed.read() | (full_reg.read() & !deqed.read()))
            with fifo.rule("next") as next_rule:
                with next_rule.guard() as g:
                    g.always()
                with next_rule.body() as body:
                    is_enqed = body.call(enqed, "read")
                    is_deqed = body.call(deqed, "read")
                    is_full = body.call(full_reg, "read")
                    not_deqed = body.not_(is_deqed)
                    full_and_not_deqed = body.and_(is_full, not_deqed)
                    next_full = body.or_(is_enqed, full_and_not_deqed)
                    body.call(full_reg, "write", next_full)

            # Set scheduling precedence (matching ECMT2)
            # full < deq < enq < deqed_default < enqed_default < next
            fifo.precedence(
                full_val.ref(),
                deq_meth.ref(),
                enq_meth.ref(),
                deqed_default.ref(),
                enqed_default.ref(),
                next_rule.ref()
            )

        return circuit._modules[name]


class FIFO1Pull:
    """Factory for depth-1 FIFO with active pull semantics as CMT2 module.

    Matches ECMT2 STLLibrary::createFIFO1PullModule.

    Built from: Reg (data), Reg (full), Wire (deqed), Wire (enqed)

    Interface:
    - value "full" -> bool: Returns true when FIFO is full
    - method "enq" (data): Enqueue data (always ready)
    - value "deq" -> data: Dequeue data (guard: full & enqed)

    The "pull" semantics means:
    - Producer (enq) can always enqueue
    - Consumer (deq) is guarded by data availability

    Example:
        fifo_mod = FIFO1Pull.create(circuit, 32)
        fifo = m.instance(fifo_mod, "output_fifo")
    """

    @staticmethod
    def create(circuit: Circuit, width: int) -> ModuleBuilder:
        """Create a depth-1 pull-style FIFO as CMT2 module.

        Args:
            circuit: The circuit to add the module to.
            width: Bit width of the data.

        Returns:
            The ModuleBuilder for the FIFO.
        """
        name = f"FIFO1_PULL_w{width}"

        if name in circuit._modules:
            return circuit._modules[name]

        # Get primitives
        reg_data_mod = Reg.create(circuit, width)
        reg_bool_mod = Reg.create(circuit, 1)
        wire_bool_mod = Wire.create(circuit, 1)

        with circuit.module(name) as fifo:
            clk = fifo.clock("clk")
            rst = fifo.reset("rst")

            # Instances
            # Reg modules need clk/rst, Wire modules don't (combinational)
            reg_data = fifo.instance(reg_data_mod, "reg_data", clk=clk, rst=rst)
            full_reg = fifo.instance(reg_bool_mod, "full_reg", clk=clk, rst=rst)
            deqed = fifo.instance(wire_bool_mod, "deqed")
            enqed = fifo.instance(wire_bool_mod, "enqed")

            # Value: full() -> bool
            with fifo.value("full", returns=[UInt(1)]) as full_val:
                with full_val.guard() as g:
                    g.always()
                with full_val.body() as body:
                    result = body.call(full_reg, "read")
                    body.returns(result)

            # Method: enq(data) - always ready
            with fifo.method("enq", args=[("data", UInt(width))]) as enq_meth:
                with enq_meth.guard() as g:
                    g.always()
                with enq_meth.body() as body:
                    body.call(enqed, "write", body.const(1, 1))
                    body.call(reg_data, "write", body.arg("data"))

            # Value: deq() -> data
            # Guard: full_reg.read() & enqed.read()
            with fifo.value("deq", returns=[UInt(width)]) as deq_val:
                with deq_val.guard() as g:
                    is_full = g.call(full_reg, "read")
                    is_enqed = g.call(enqed, "read")
                    can_deq = g.and_(is_full, is_enqed)
                    g.returns(can_deq)
                with deq_val.body() as body:
                    body.call(deqed, "write", body.const(1, 1))
                    data = body.call(reg_data, "read")
                    body.returns(data)

            # Rule: enqed_default
            with fifo.rule("enqed_default") as enqed_default:
                with enqed_default.guard() as g:
                    g.always()
                with enqed_default.body() as body:
                    body.call(enqed, "write", body.const(0, 1))

            # Rule: deqed_default
            with fifo.rule("deqed_default") as deqed_default:
                with deqed_default.guard() as g:
                    g.always()
                with deqed_default.body() as body:
                    body.call(deqed, "write", body.const(0, 1))

            # Rule: next
            with fifo.rule("next") as next_rule:
                with next_rule.guard() as g:
                    g.always()
                with next_rule.body() as body:
                    is_enqed = body.call(enqed, "read")
                    is_deqed = body.call(deqed, "read")
                    is_full = body.call(full_reg, "read")
                    not_deqed = body.not_(is_deqed)
                    full_and_not_deqed = body.and_(is_full, not_deqed)
                    next_full = body.or_(is_enqed, full_and_not_deqed)
                    body.call(full_reg, "write", next_full)

            # Precedence: full < enq < deq < enqed_default < deqed_default < next
            fifo.precedence(
                full_val.ref(),
                enq_meth.ref(),
                deq_val.ref(),
                enqed_default.ref(),
                deqed_default.ref(),
                next_rule.ref()
            )

        return circuit._modules[name]


class FIFO2I:
    """Factory for depth-2 FIFO with independent enq/deq as CMT2 module.

    Matches ECMT2 STLLibrary::createFIFO2IModule.

    Built from: Reg (reg0), Reg (reg1), Reg (state), WireDefault (deqed), WireDefault (enqed), Wire (enq_value)

    Interface:
    - value "full" -> bool: Returns true when state == 2 (FIFO at capacity)
    - method "deq" -> data: Dequeue data (guard: state != 0)
    - method "enq" (data): Enqueue data (guard: state != 2)

    This FIFO allows independent enq and deq in the same cycle.

    Example:
        fifo_mod = FIFO2I.create(circuit, 32)
        fifo = m.instance(fifo_mod, "buffer")
    """

    @staticmethod
    def create(circuit: Circuit, width: int) -> ModuleBuilder:
        """Create a depth-2 independent FIFO as CMT2 module.

        Args:
            circuit: The circuit to add the module to.
            width: Bit width of the data.

        Returns:
            The ModuleBuilder for the FIFO.
        """
        name = f"FIFO2_I_w{width}"

        if name in circuit._modules:
            return circuit._modules[name]

        # Get primitives
        reg_data_mod = Reg.create(circuit, width)
        reg_state_mod = Reg.create(circuit, 2)  # 2-bit state: 0=empty, 1=one, 2=full
        wire_default_bool_mod = WireDefault.create(circuit, 1, init=0)
        wire_data_mod = Wire.create(circuit, width)

        with circuit.module(name) as fifo:
            clk = fifo.clock("clk")
            rst = fifo.reset("rst")

            # Instances
            # Reg modules need clk/rst, Wire/WireDefault modules don't (combinational)
            reg0 = fifo.instance(reg_data_mod, "reg0", clk=clk, rst=rst)
            reg1 = fifo.instance(reg_data_mod, "reg1", clk=clk, rst=rst)
            state = fifo.instance(reg_state_mod, "state", clk=clk, rst=rst)
            deqed = fifo.instance(wire_default_bool_mod, "deqed")
            enqed = fifo.instance(wire_default_bool_mod, "enqed")
            enq_value = fifo.instance(wire_data_mod, "enq_value")

            # Value: full() -> bool (state == 2)
            with fifo.value("full", returns=[UInt(1)]) as full_val:
                with full_val.guard() as g:
                    g.always()
                with full_val.body() as body:
                    state_val = body.call(state, "read")
                    c2 = body.const(2, 2)
                    is_full = body.eq(state_val, c2)
                    body.returns(is_full)

            # Value: empty() -> bool (state == 0)
            with fifo.value("empty", returns=[UInt(1)]) as empty_val:
                with empty_val.guard() as g:
                    g.always()
                with empty_val.body() as body:
                    state_val = body.call(state, "read")
                    c0 = body.const(0, 2)
                    is_empty = body.eq(state_val, c0)
                    body.returns(is_empty)

            # Method: deq() -> data
            # Guard: state != 0
            with fifo.method("deq", returns=[UInt(width)]) as deq_meth:
                with deq_meth.guard() as g:
                    state_val = g.call(state, "read")
                    c0 = g.const(0, 2)
                    can_deq = g.neq(state_val, c0)
                    g.returns(can_deq)
                with deq_meth.body() as body:
                    body.call(deqed, "write", body.const(1, 1))
                    data = body.call(reg0, "read")
                    body.returns(data)

            # Method: enq(data)
            # Guard: state != 2
            with fifo.method("enq", args=[("data", UInt(width))]) as enq_meth:
                with enq_meth.guard() as g:
                    state_val = g.call(state, "read")
                    c2 = g.const(2, 2)
                    can_enq = g.neq(state_val, c2)
                    g.returns(can_enq)
                with enq_meth.body() as body:
                    body.call(enqed, "write", body.const(1, 1))
                    body.call(enq_value, "write", body.arg("data"))

            # State update rule: state0 (when state == 0)
            # if enqed: reg0 = enq_value, state = 1
            with fifo.rule("state0_update") as state0_rule:
                with state0_rule.guard() as g:
                    state_val = g.call(state, "read")
                    c0 = g.const(0, 2)
                    is_state0 = g.eq(state_val, c0)
                    is_enqed = g.call(enqed, "read")
                    guard = g.and_(is_state0, is_enqed)
                    g.returns(guard)
                with state0_rule.body() as body:
                    val = body.call(enq_value, "read")
                    body.call(reg0, "write", val)
                    body.call(state, "write", body.const(1, 2))

            # State update rule: state1 (when state == 1)
            with fifo.rule("state1_update") as state1_rule:
                with state1_rule.guard() as g:
                    state_val = g.call(state, "read")
                    c1 = g.const(1, 2)
                    is_state1 = g.eq(state_val, c1)
                    g.returns(is_state1)
                with state1_rule.body() as body:
                    is_enqed = body.call(enqed, "read")
                    is_deqed = body.call(deqed, "read")

                    # State transitions:
                    # enq && deq -> stay 1, reg0 = enq_value
                    # enq && !deq -> go to 2, reg1 = reg0, reg0 = enq_value
                    # !enq && deq -> go to 0
                    # !enq && !deq -> stay 1

                    val = body.call(enq_value, "read")
                    reg0_val = body.call(reg0, "read")
                    not_deqed = body.not_(is_deqed)

                    # Compute next state using mux
                    c0 = body.const(0, 2)
                    c1 = body.const(1, 2)
                    c2 = body.const(2, 2)
                    inner_if_enq = body.mux(is_deqed, c1, c2)  # enq: deq?1:2
                    inner_if_not_enq = body.mux(is_deqed, c0, c1)  # !enq: deq?0:1
                    next_state = body.mux(is_enqed, inner_if_enq, inner_if_not_enq)
                    body.call(state, "write", next_state)

                    # Conditionally write reg0 (if enqed)
                    # Use enable signal to gate the write
                    enq_and_not_deq = body.and_(is_enqed, not_deqed)

                    # Write reg0 when enqed (use if for single level)
                    with body.if_(is_enqed) as if_enq:
                        with if_enq.then_() as then_b:
                            then_b.call(reg0, "write", val)

                    # Write reg1 when enqed && !deqed (separate if, not nested)
                    with body.if_(enq_and_not_deq) as if_reg1:
                        with if_reg1.then_() as then_b:
                            then_b.call(reg1, "write", reg0_val)

            # State update rule: state2 (when state == 2)
            # if deqed: reg0 = reg1, state = 1
            with fifo.rule("state2_update") as state2_rule:
                with state2_rule.guard() as g:
                    state_val = g.call(state, "read")
                    c2 = g.const(2, 2)
                    is_state2 = g.eq(state_val, c2)
                    is_deqed = g.call(deqed, "read")
                    guard = g.and_(is_state2, is_deqed)
                    g.returns(guard)
                with state2_rule.body() as body:
                    reg1_val = body.call(reg1, "read")
                    body.call(reg0, "write", reg1_val)
                    body.call(state, "write", body.const(1, 2))

            # Precedence: full < empty < deq < enq < state0_update < state1_update < state2_update
            fifo.precedence(
                full_val.ref(),
                empty_val.ref(),
                deq_meth.ref(),
                enq_meth.ref(),
                state0_rule.ref(),
                state1_rule.ref(),
                state2_rule.ref()
            )

        return circuit._modules[name]


class FIFO:
    """Factory for FIFO modules.

    For specific FIFO variants, use:
    - FIFO1Push: Depth-1 with active push semantics
    - FIFO1Pull: Depth-1 with active pull semantics
    - FIFO2I: Depth-2 with independent enq/deq

    Example:
        fifo_mod = FIFO.create(circuit, 32)
        fifo = m.instance(fifo_mod, "data_fifo")
    """

    @staticmethod
    def create(circuit: Circuit, width: int, depth: int = 2) -> ModuleBuilder:
        """Create a FIFO module.

        Args:
            circuit: The circuit to add the module to.
            width: Bit width of the data.
            depth: FIFO depth (1 or 2, default 2).

        Returns:
            The ModuleBuilder for the FIFO.
        """
        if depth == 1:
            return FIFO1Push.create(circuit, width)
        else:
            return FIFO2I.create(circuit, width)


# =============================================================================
# Memory External Modules
# =============================================================================

class Memory:
    """Factory for Memory external modules.

    Provides bindings for memory modules in ModuleLibrary:
    - Mem1r1w1c: 1R1W synchronous memory (1-cycle read latency)
    - Mem1r1w0c: 1R1W asynchronous memory (0-cycle read latency)
    """

    @staticmethod
    def create_1r1w_sync(
        circuit: Circuit,
        data_width: int,
        addr_width: int,
        depth: int,
    ) -> ExternalModuleBuilder:
        """Create a 1R1W synchronous memory (1-cycle read latency).

        Matches ECMT2 STLLibrary::createMem1r1w1cModule.

        Interface:
        - method "rd0" (raddr): Initiate read at address
        - value "rd1" -> rdata: Read result (available next cycle)
        - method "write" (wdata, waddr): Write data to address

        Args:
            circuit: The circuit to add the module to.
            data_width: Bit width of data.
            addr_width: Bit width of address.
            depth: Memory depth.

        Returns:
            The ExternalModuleBuilder for the memory.
        """
        name = f"Mem1r1w1c_w{data_width}_a{addr_width}_d{depth}"

        if name in circuit._external_modules:
            return circuit._external_modules[name]

        # Include the FIRRTL module from ModuleLibrary
        actual_name = circuit.include_library_module(
            "Mem1r1w1c",
            {"data_width": data_width, "addr_width": addr_width, "depth": depth}
        )
        if actual_name:
            firrtl_module_name = actual_name
        else:
            # Fallback if library not available
            firrtl_module_name = name

        with circuit.external_module(name) as mem:
            mem.set_firrtl_module_name(firrtl_module_name)
            mem.clock("clk")
            mem.reset("rst")
            # rd0: initiate read (no output this cycle)
            mem.method("rd0",
                       enable_name="en",
                       args=[("raddr", UInt(addr_width))])
            # rd1: read result available (value, not method)
            mem.value("rd1",
                      ready_name="rd1_valid",
                      returns=[("rdata", UInt(data_width))])
            # write: write data
            mem.method("write",
                       enable_name="wen",
                       args=[("wdata", UInt(data_width)),
                             ("waddr", UInt(addr_width))])

        return circuit._external_modules[name]

    @staticmethod
    def create_1r1w_async(
        circuit: Circuit,
        data_width: int,
        addr_width: int,
        depth: int,
    ) -> ExternalModuleBuilder:
        """Create a 1R1W asynchronous memory (0-cycle read latency).

        Matches ECMT2 STLLibrary::createMem1r1w0cModule.

        Interface:
        - method "read" (raddr) -> rdata: Combinational read
        - method "write" (wdata, waddr): Write data to address

        Args:
            circuit: The circuit to add the module to.
            data_width: Bit width of data.
            addr_width: Bit width of address.
            depth: Memory depth.

        Returns:
            The ExternalModuleBuilder for the memory.
        """
        name = f"Mem1r1w0c_w{data_width}_a{addr_width}_d{depth}"

        if name in circuit._external_modules:
            return circuit._external_modules[name]

        # Include the FIRRTL module from ModuleLibrary
        actual_name = circuit.include_library_module(
            "Mem1r1w0c",
            {"data_width": data_width, "addr_width": addr_width, "depth": depth}
        )
        if actual_name:
            firrtl_module_name = actual_name
        else:
            # Fallback if library not available
            firrtl_module_name = name

        with circuit.external_module(name) as mem:
            mem.set_firrtl_module_name(firrtl_module_name)
            mem.clock("clk")
            mem.reset("rst")
            # read: combinational read
            mem.method("read",
                       enable_name="en",
                       args=[("raddr", UInt(addr_width))],
                       returns=[("rdata", UInt(data_width))])
            # write: write data
            mem.method("write",
                       enable_name="wen",
                       args=[("wdata", UInt(data_width)),
                             ("waddr", UInt(addr_width))])

        return circuit._external_modules[name]

    @staticmethod
    def create(
        circuit: Circuit,
        data_width: int,
        addr_width: int,
        depth: int,
        sync: bool = True,
    ) -> ExternalModuleBuilder:
        """Create a memory external module.

        Args:
            circuit: The circuit to add the module to.
            data_width: Bit width of data.
            addr_width: Bit width of address.
            depth: Memory depth.
            sync: If True, creates synchronous memory (1-cycle read latency).
                  If False, creates asynchronous memory (0-cycle read latency).

        Returns:
            The ExternalModuleBuilder for the memory.
        """
        if sync:
            return Memory.create_1r1w_sync(circuit, data_width, addr_width, depth)
        else:
            return Memory.create_1r1w_async(circuit, data_width, addr_width, depth)


# =============================================================================
# ShiftReg Module
# =============================================================================

class ShiftReg:
    """Factory for shift register as CMT2 module.

    A shift register is a chain of registers that delays data by a fixed
    number of cycles. Data enters at one end (enq) and exits at the other (deq).

    Built from: Reg instances for each stage

    Interface:
    - method "enq" (data): Write data to input stage
    - value "deq" -> data: Read data from output stage
    - value "valid" -> bool: Output is valid (always true after delay cycles)

    The shift register delays data by `depth` cycles.

    Example:
        shiftreg_mod = ShiftReg.create(circuit, 32, depth=4)
        sr = m.instance(shiftreg_mod, "delay_line", clk=clk, rst=rst)
    """

    @staticmethod
    def create(circuit: Circuit, width: int, depth: int = 2) -> ModuleBuilder:
        """Create a shift register as CMT2 module.

        Args:
            circuit: The circuit to add the module to.
            width: Bit width of the data.
            depth: Number of register stages (default 2).

        Returns:
            The ModuleBuilder for the shift register.
        """
        name = f"ShiftReg_w{width}_d{depth}"

        if name in circuit._modules:
            return circuit._modules[name]

        if depth < 1:
            depth = 1

        # Get register primitive
        reg_mod = Reg.create(circuit, width)

        with circuit.module(name) as shiftreg:
            clk = shiftreg.clock("clk")
            rst = shiftreg.reset("rst")

            # Create chain of registers
            stages = []
            for i in range(depth):
                stage = shiftreg.instance(reg_mod, f"stage{i}", clk=clk, rst=rst)
                stages.append(stage)

            # Value: deq() -> data (read from last stage)
            with shiftreg.value("deq", returns=[UInt(width)]) as deq_val:
                with deq_val.guard() as g:
                    g.always()
                with deq_val.body() as body:
                    result = body.call(stages[-1], "read")
                    body.returns(result)

            # Value: valid() -> bool (always true for simple shift reg)
            with shiftreg.value("valid", returns=[UInt(1)]) as valid_val:
                with valid_val.guard() as g:
                    g.always()
                with valid_val.body() as body:
                    body.returns(body.const(1, 1))

            # Method: enq(data) - write to first stage and shift through
            with shiftreg.method("enq", args=[("data", UInt(width))]) as enq_meth:
                with enq_meth.guard() as g:
                    g.always()
                with enq_meth.body() as body:
                    # Write new data to first stage
                    body.call(stages[0], "write", body.arg("data"))
                    # Shift data through remaining stages
                    for i in range(1, depth):
                        prev_data = body.call(stages[i - 1], "read")
                        body.call(stages[i], "write", prev_data)

            # Precedence: deq < valid < enq
            shiftreg.precedence(
                deq_val.ref(),
                valid_val.ref(),
                enq_meth.ref()
            )

        return circuit._modules[name]


# =============================================================================
# Legacy compatibility functions (deprecated)
# =============================================================================

def get_stl_rtl_files() -> dict[str, str]:
    """Get all registered STL RTL implementations.

    DEPRECATED: STL modules now use ModuleLibrary for RTL.
    This function returns an empty dict for backward compatibility.

    Returns:
        Empty dictionary (RTL comes from ModuleLibrary).
    """
    return {}


def add_stl_rtl_to_workspace(workspace) -> None:
    """Add all registered STL RTL files to a simulation workspace.

    DEPRECATED: STL modules now use ModuleLibrary for RTL.
    This function is a no-op for backward compatibility.

    Args:
        workspace: SimulationWorkspace instance (ignored).
    """
    pass


def clear_stl_registry() -> None:
    """Clear the STL RTL registry.

    DEPRECATED: STL modules now use ModuleLibrary for RTL.
    This function is a no-op for backward compatibility.
    """
    pass

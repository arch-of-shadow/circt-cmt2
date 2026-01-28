# CMT2 JIT Proposal Review: Computer Architecture Expert

**Reviewer:** Computer Architecture Expert  
**Date:** 2026-01-28  
**Document:** CMT2-JIT-Proposal.md  

---

## 1. Overall Assessment

The CMT2 JIT Proposal presents a promising approach to hardware design that bridges the gap between Python's ergonomic flexibility and hardware implementation. The staged compilation pipeline (Python → CMT2 IR → MLIR → Hardware) is architecturally sound and follows proven patterns from JAX and Triton. The tracing mechanism and static/dynamic argument separation demonstrate good understanding of hardware parameterization needs.

However, from a computer architecture perspective, the proposal has significant gaps in hardware-specific concerns. While the software infrastructure is well-conceived, critical hardware concepts—timing analysis, clock domains, resource estimation, and physical design constraints—are either missing or underdeveloped. The proposal treats hardware generation somewhat like software compilation, missing the unique challenges of physical implementation. For this to be viable for real hardware design (not just educational/academic use), these gaps must be addressed before implementation proceeds.

---

## 2. Detailed Feedback by Section

### 2.1 Hardware Abstractions

**[CRITICAL]** **Missing Clock Domain Abstractions**
- The `Reg` abstraction lacks clock/reset domain specification
- Real designs have multiple clocks (e.g., `clk_core`, `clk_io`, `clk_ddr`)
- No mechanism for clock domain crossing (CDC) primitives
- **Recommendation:** Add `Reg(clock_domain="clk_core", reset_domain="rst_n")` or similar

**[CRITICAL]** **Insufficient Reset Semantics**
- The proposal mentions `init=0` but doesn't specify reset type (async, sync, active-high/low)
- FPGA vs ASIC have different reset best practices
- **Recommendation:** Add `reset_type` parameter: `Reg(..., reset_type="async_low")`

**[WARNING]** **FIFO Abstraction Too Simplistic**
- Real FIFOs need: almost_full/almost_empty flags, programmable thresholds, overflow/underflow detection
- No mention of clock domain crossing FIFOs (async FIFOs)
- **Recommendation:** Support `FIFO(..., clock_domains=("clk_a", "clk_b"))` for CDC FIFOs

**[WARNING]** **No Memory Abstractions**
- Only `Reg` is shown—no SRAM/DRAM memory primitives
- Hardware designs extensively use: single-port RAM, dual-port RAM, ROM, CAM
- **Recommendation:** Add `Memory`, `SRAM`, `ROM` abstractions with proper port specifications

**[SUGGESTION]** **Bundle/Struct Types Missing**
- Hardware uses packed structs extensively (AXI4, custom protocols)
- Proposal shows only `UInt` and `Bits`
- **Recommendation:** Add `Bundle`, `Struct`, and `Union` types with field access

### 2.2 Synthesis Flow Integration

**[CRITICAL]** **No Timing Constraint Generation**
- Synthesis tools (Vivado, Quartus, DC) require SDC/XDC constraint files
- The proposal has `target_freq="100MHz"` but no mechanism to generate constraints
- **Recommendation:** Generate `.sdc`/`.xdc` files with clock definitions, false paths, multicycle paths

**[CRITICAL]** **Missing Physical Design Considerations**
- No floorplanning hints (hierarchy preservation, module placement)
- No I/O pad specification (voltage standards, drive strength)
- **Recommendation:** Add physical design hints API:
  ```python
  circuit.set_floorplan(area=(1000, 1000), utilization=0.7)
  circuit.add_io_constraint("data_in", location="NORTH", io_standard="LVCMOS33")
  ```

**[WARNING]** **Incomplete Backend Targets**
- Only lists: Simulation, Verilog, FPGA
- Missing: ASIC synthesis, formal verification, power analysis
- **Recommendation:** Expand targets:
  - `target="asic-synthesis"` → Genus/DC flow
  - `target="formal"` → JasperGold/VC Formal
  - `target="power"` → Power analysis with switching activity

**[WARNING]** **No Resource Estimation**
- Hardware designers need early area/timing estimates
- Proposal has no resource reporting mechanism
- **Recommendation:** Add resource estimation pass:
  ```python
  estimate = circuit.estimate_resources()
  # Returns: {lut: 1200, ff: 800, bram: 4, dsp: 2}
  ```

**[SUGGESTION]** **Vendor-Specific Primitive Support**
- FPGAs have vendor primitives (Xilinx DSP48, Intel M9K)
- Proposal doesn't show how to target these
- **Recommendation:** Add vendor primitive library:
  ```python
  from cmt2.vendor.xilinx import DSP48E2
  dsp = DSP48E2(...)  # Direct primitive instantiation
  ```

### 2.3 Hardware-Specific Optimizations

**[CRITICAL]** **No Pipelining Primitives**
- The JIT should support automatic/manual pipelining
- Critical for high-frequency designs
- **Recommendation:** Add pipeline pragma:
  ```python
  @circuit.rule("compute", pipeline=True, stages=5)
  def compute():
      # Automatically inserts pipeline registers
      result = stage1() >> stage2() >> stage3()
  ```

**[CRITICAL]** **Missing Retiming Support**
- Modern synthesis tools support automatic retiming
- Proposal doesn't indicate how to enable/guide this
- **Recommendation:** Add retiming directives:
  ```python
  @circuit.enable_retiming(boundary_registers=True)
  def critical_path_module():
      pass
  ```

**[WARNING]** **No Resource Sharing Mechanism**
- Multipliers/adders are expensive—should be shared across rules
- No indication of how resource conflicts are resolved
- **Recommendation:** Add explicit sharing annotation:
  ```python
  @circuit.share_resource("multiplier_pool", count=2)
  def math_operations():
      pass
  ```

**[WARNING]** **Clock Domain Crossing Not Addressed**
- CDC is a major source of hardware bugs
- Needs proper synchronizer insertion (2-flop, handshake, FIFO)
- **Recommendation:** CDC primitives with automatic synchronizer insertion:
  ```python
  signal = cdc_cross(from_clk="clk_fast", to_clk="clk_slow", 
                     method="2flop", domain="gray")
  ```

**[SUGGESTION]** **Clock Gating Support**
- Power-efficient designs need clock gating
- Proposal doesn't mention power optimization
- **Recommendation:** Add clock gating inference:
  ```python
  reg = Reg(..., clock_gating=True, enable=enable_signal)
  ```

### 2.4 Parameterization

**[WARNING]** **Type-Level Parameterization Missing**
- Current approach: `fifo_module(depth: int, width: int)`
- Hardware often needs type-level params (e.g., `FIFO[DataType, Depth]`)
- **Recommendation:** Support generic/parametric types:
  ```python
  @cmt2.jit
  def generic_fifo[T, Depth: int]() -> Circuit:
      fifo = FIFO(T, depth=Depth)
  ```

**[WARNING]** **No Dependent Types Support**
- Hardware often has dependent types (e.g., address width depends on memory depth)
- `width = log2(depth)` is common pattern
- **Recommendation:** Built-in dependent type constructors:
  ```python
  depth = Param(1024)
  addr_width = Log2Ceil(depth)  # Type-level computation
  addr = Signal(UInt(addr_width))
  ```

**[SUGGESTION]** **Template Metaprogramming Limited**
- Proposal mentions "template-like metaprogramming" but no details
- Hardware generators often need compile-time loops/unrolling
- **Recommendation:** Support staged computation:
  ```python
  @cmt2.jit
  def unrolled_adder(n: int):
      result = 0
      for i in cmt2.static_range(n):  # Unrolled at compile time
          result += input[i]
  ```

### 2.5 Verification and Testing

**[CRITICAL]** **No Testbench Generation Strategy**
- Hardware verification requires testbenches
- Proposal only mentions simulation execution
- **Recommendation:** Add testbench generation:
  ```python
  @cmt2.testbench
  def test_counter():
      dut = counter_module(max_count=100)
      dut.reset()
      yield dut.clk
      assert dut.count == 0
      for _ in range(100):
          yield dut.clk
      assert dut.count == 100
  ```

**[CRITICAL]** **Missing Assertion Support**
- Hardware assertions (SVA) critical for verification
- No mention of assertions in proposal
- **Recommendation:** Python-friendly assertion API:
  ```python
  @circuit.assertion("valid_when_ready", severity="error")
  def check_protocol():
      return ~(valid & ~ready)  # Assertion: valid implies ready eventually
  ```

**[WARNING]** **No Formal Verification Integration**
- Formal methods increasingly important for hardware
- Proposal doesn't address this
- **Recommendation:** Add formal target:
  ```python
  result = circuit.compile(target="formal")
  result.verify(properties=["no_deadlock", "liveness"])
  ```

**[WARNING]** **Coverage Not Addressed**
- Code/functional coverage essential for verification sign-off
- No coverage generation mechanism
- **Recommendation:** Automatic coverage instrumentation:
  ```python
  @cmt2.jit(coverage=True)
  def design_with_coverage():
      pass  # Automatically inserts coverage points
  ```

**[SUGGESTION]** **UVM-Style Verification Support**
- Industry uses UVM for complex verification
- Python could generate UVM testbenches
- **Recommendation:** Consider cocotb integration for Python-based verification

---

## 3. Specific Recommendations

### 3.1 Add Hardware Constraint System

**Rationale:** Hardware design is constraint-driven. Without timing, area, and power constraints, the JIT produces designs that may not meet requirements.

**Implementation:**
```python
@cmt2.jit(constraints={
    "clock": {"period": "10ns", "uncertainty": "0.5ns"},
    "area": {"max_lut": 10000, "max_ff": 5000},
    "power": {"budget": "100mW"}
})
def constrained_design():
    pass
```

### 3.2 Implement Clock Domain Management

**Rationale:** Multi-clock designs are ubiquitous. Proper CDC handling prevents metastability issues.

**Implementation:**
```python
@cmt2.jit
def multi_clock_design():
    circuit = Circuit("CDC_Example")
    
    # Define clocks
    clk_fast = circuit.add_clock("clk_fast", freq="200MHz")
    clk_slow = circuit.add_clock("clk_slow", freq="100MHz")
    
    # Clock domain crossing
    data_in_fast = Signal(UInt(32), clock_domain=clk_fast)
    data_out_slow = cdc_sync(data_in_fast, to_clk=clk_slow, method="fifo")
```

### 3.3 Add Resource Estimation and Reporting

**Rationale:** Hardware designers need early feedback on resource utilization to guide architectural decisions.

**Implementation:**
```python
traced = design.trace(width=32, depth=16)
estimate = traced.estimate_resources(target="xilinx-ultrascale+")
print(estimate.report())
# Output:
# LUT6: 1,240 (3.2%)
# FF:    820 (2.1%)
# BRAM36:  4 (8.0%)
# DSP48:   2 (4.0%)
# Estimated Fmax: 350 MHz
```

### 3.4 Support Vendor-Specific Flows

**Rationale:** Real deployment requires vendor toolchain integration.

**Implementation:**
```python
xilinx_flow = cmt2.vendor.XilinxFlow(
    part="xcvu9p-flga2104-2-e",
    strategy="Performance_Explore",
    constraints="design.xdc"
)
result = compiled.synthesize(flow=xilinx_flow)
print(result.timing_summary())  # WNS, TNS, WHS
print(result.resource_utilization())
```

### 3.5 Add Verification Infrastructure

**Rationale:** Verification typically consumes 70%+ of hardware design effort. A hardware JIT without verification support is incomplete.

**Implementation:**
```python
# Generate cocotb testbench
cocotb_tb = compiled.generate_cocotb_testbench(
    test_vectors="tests/vectors.json",
    coverage=True
)
cocotb_tb.run(simulator="verilator")

# Generate formal properties
formal_props = compiled.generate_formal_properties(
    properties=["no_deadlock", "data_integrity"]
)
```

### 3.6 Implement Pipeline Primitives

**Rationale:** Pipelining is essential for high-frequency designs. Explicit pipeline control enables predictable performance.

**Implementation:**
```python
@cmt2.pipeline(stages=5, style="forward")
def pipelined_computation(a, b, c):
    # Automatic pipeline register insertion
    x = a * b      # Stage 0
    y = x + c      # Stage 1
    z = y << 2     # Stage 2
    return z
```

### 3.7 Add Power Optimization Support

**Rationale:** Power is a primary constraint for mobile/edge devices.

**Implementation:**
```python
@cmt2.jit(power_optimization={
    "clock_gating": True,
    "operand_isolation": True,
    "retention_registers": True
})
def power_optimized_design():
    pass
```

---

## 4. Questions for Other Experts

### For PL/Type Systems Expert:
1. **How do we express hardware-specific dependent types?**
   - Example: Address width must be `ceil(log2(depth))`
   - Can we encode this in Python's type system or need custom types?

2. **What's the best way to handle staged metaprogramming?**
   - Should we support Python's `ast` module for code transformation?
   - Or rely entirely on tracing like JAX?

3. **How do we handle mutable state in the tracing model?**
   - Registers have `.next` assignments—how does this interact with pure functional tracing?

### For Compiler Expert:
1. **How do we integrate timing analysis into the compilation pipeline?**
   - Should timing be a first-class IR concern or post-processing?

2. **What's the best approach for incremental synthesis?**
   - Can we cache synthesis results at module boundaries?

3. **How do we handle multi-clock IR?**
   - Does MLIR have abstractions for clock domains we can leverage?

### For Verification Expert:
1. **Should we generate SVA assertions or Python-based checkers?**
   - SVA is industry standard but harder to generate
   - Python assertions are easier but require simulation

2. **How do we handle coverage closure?**
   - Can we automatically generate coverage points from the IR?

---

## 5. Approval Status

**STATUS: NEEDS_REVISION**

### Summary

The CMT2 JIT Proposal has a solid foundation for a Python-to-hardware flow, but it lacks critical hardware-specific features necessary for production use. The abstraction layer is too software-centric and doesn't adequately address:

1. **Timing and Clocking:** No clock domain management, CDC, or constraint generation
2. **Physical Design:** No resource estimation, floorplanning, or vendor toolchain integration
3. **Verification:** No testbench generation, assertions, or formal verification support
4. **Power:** No power optimization or analysis features

### Required Changes Before Approval

The following must be added to the proposal:

| Priority | Item | Section |
|----------|------|---------|
| P0 | Clock domain management and CDC primitives | Hardware Abstractions |
| P0 | Timing constraint generation (SDC/XDC) | Synthesis Flow |
| P0 | Resource estimation API | Synthesis Flow |
| P0 | Assertion and testbench generation | Verification |
| P1 | Power optimization directives | Optimizations |
| P1 | Vendor-specific toolchain integration | Backend Targets |
| P1 | Pipeline and retiming primitives | Optimizations |
| P2 | Coverage instrumentation | Verification |
| P2 | Physical design hints | Synthesis Flow |

### Recommended Path Forward

1. **Immediate:** Add clock domain and constraint generation sections
2. **Short-term:** Define verification strategy (cocotb integration recommended)
3. **Medium-term:** Prototype resource estimation on a simple design
4. **Long-term:** Evaluate vendor toolchain integration complexity

---

## 6. Additional References

- **Clock Domain Crossing:** Cummings, "Clock Domain Crossing (CDC) Design & Verification Techniques Using SystemVerilog"
- **SDC Constraints:** "PrimeTime User Guide" - Synopsys
- **FPGA Power:** "UltraFast Design Methodology Guide for the Vivado Design Suite" - Xilinx UG949
- **Verification:** Bergeron, "Writing Testbenches using SystemVerilog"
- **Formal:** "JasperGold Apps User Guide" - Cadence

---

*Review completed. This document should be discussed with the PL and Compiler experts before proceeding to implementation.*

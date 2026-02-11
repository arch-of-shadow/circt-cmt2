# External Modules (PyCMT2 + JIT) E2E Validation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add an end-to-end, simulatable example demonstrating *user-defined* `Circuit.external_module(...)` bindings (custom RTL provided) for both PyCMT2 and `cmt2.jit`.

**Architecture:** Keep external modules as a PyCMT2 concept (`Circuit.external_module`). Validation is via `SimulationWorkspace.add_external_rtl(...)` to provide the Verilog implementation for externs not backed by ModuleLibrary. Add one PyCMT2 example and one JIT example, both running Verilator E2E through the existing example runners.

**Tech Stack:** PyCMT2 (`circt.pycmt2`), Cmt2 JIT (`cmt2.jit`), `SimulationWorkspace` + Testbench DSL, Verilator.

---

### Task 1: Confirm current external-module support surface

**Files:**
- Read: `lib/Bindings/Python/pycmt2/external_module.py`
- Read: `lib/Bindings/Python/pycmt2/circuit.py`
- Read: `lib/Bindings/Python/pycmt2/module.py`
- Read: `lib/Bindings/Python/pycmt2/simulation.py`
- Read: `python/cmt2/jit/_method_ref.py`

**Step 1: Verify PyCMT2 has builder + instantiation path**

Run:
```bash
rg -n "def external_module\\(" lib/Bindings/Python/pycmt2/circuit.py
rg -n "class ExternalModuleBuilder" lib/Bindings/Python/pycmt2/external_module.py
rg -n "isinstance\\(module, ExternalModuleBuilder\\)" lib/Bindings/Python/pycmt2/module.py
```

Expected:
- `Circuit.external_module(...)` exists
- `ExternalModuleBuilder._finalize()` emits `cmt2.ExtModuleFirrtlOp`
- `ModuleBuilder.instance(...)` accepts `ExternalModuleBuilder`

**Step 2: Verify simulation workspace can stage RTL for externs**

Run:
```bash
rg -n "def add_external_rtl\\(" lib/Bindings/Python/pycmt2/simulation.py
```

Expected:
- `SimulationWorkspace.add_external_rtl(filename, content)` exists and writes into `rtl/`.

---

### Task 2: Add a minimal failing E2E example (extern module without RTL)

**Files:**
- Create: `examples/PyCMT2/external_module_custom_rtl.py`

**Step 1: Write failing example (no external RTL staged)**

Create an external module `Accum32` with ports:
- `clk`, `rst`
- value `read` returning `read_data` (+ optional `read_ready`)
- method `add` taking `add_data` with enable/ready handshake (`add_enable`, `add_ready`)

Build a module that calls `accum.add(1)` every cycle and exposes `get_count` value.
Generate workspace + build + run.

**Step 2: Run and confirm failure**

Run:
```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
  python3 -B examples/PyCMT2/external_module_custom_rtl.py
```

Expected:
- Verilator build fails with “module `Accum32` not found” (or similar undefined-module error).

---

### Task 3: Make the PyCMT2 example pass by staging custom RTL

**Files:**
- Modify: `examples/PyCMT2/external_module_custom_rtl.py`

**Step 1: Add minimal SystemVerilog implementation via `add_external_rtl`**

Implement:
```systemverilog
module Accum32(...);
  logic [31:0] sum;
  assign read_ready = 1'b1;
  assign add_ready = 1'b1;
  assign read_data = sum;
  always_ff @(posedge clk) begin
    if (rst) sum <= '0;
    else if (add_enable) sum <= sum + add_data;
  end
endmodule
```

Stage it:
```python
ws.add_external_rtl("Accum32.sv", ACCUM32_SV)
```

**Step 2: Run and confirm pass**

Run:
```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
  python3 -B examples/PyCMT2/external_module_custom_rtl.py
```

Expected:
- Example prints a `PASSED:` marker and exits 0.

**Step 3: Add to PyCMT2 runner**

**Files:**
- Modify: `examples/PyCMT2/run_examples.py`

Add `"external_module_custom_rtl.py"` to `EXAMPLES`.

Run:
```bash
env -u PYTHONPATH python3 -B examples/PyCMT2/run_examples.py --keep-going
```
Expected: runner remains green.

---

### Task 4: Add a JIT E2E example for the same external module

**Files:**
- Create: `examples/JIT/external_module_custom_rtl.py`

**Step 1: Implement the same design using `cmt2.jit`**

Use:
- `with jit.module(circuit, "Top") as m:`
- `accum = m.instance(accum_mod, clk=..., rst=...)`
- In a `@jit.rule(m)` body: `accum.add(one)` and `get_count` returns `accum.read`

Stage RTL via `SimulationWorkspace.add_external_rtl(...)` exactly like the PyCMT2 example.

**Step 2: Run and confirm pass**

Run:
```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
  python3 -B examples/JIT/external_module_custom_rtl.py
```

Expected:
- Example prints a `PASSED:` marker and exits 0.

**Step 3: Add to JIT runner**

**Files:**
- Modify: `examples/JIT/run_examples.py`

Add `"external_module_custom_rtl.py"` to `EXAMPLES`.

Run:
```bash
env -u PYTHONPATH python3 -B examples/JIT/run_examples.py --keep-going --log-dir examples/JIT/_logs
```
Expected: runner remains green.

---

### Task 5: Document “extern module RTL” workflow for users

**Files:**
- Modify: `docs/Cmt2/guides/JIT.md`
- (Optional) Modify: `docs/Cmt2/guides/PyCMT2-Guide.md`

**Step 1: Add a short section**

Cover:
- External modules are declared via `Circuit.external_module(...)`.
- If the extern is not backed by ModuleLibrary, you must supply RTL in the sim workspace:
  - `ws.add_external_rtl("MyExtern.sv", "...")`
- Point to the new examples as the reference workflow.

**Step 2: Quick docs sanity**

Run:
```bash
python3 -m compileall -q docs/Cmt2/guides/JIT.md >/dev/null 2>&1 || true
```

---

### Task 6: Final verification pass

**Files:**
- (No changes expected)

Run:
```bash
python3 -m compileall -q lib/Bindings/Python/pycmt2 python/cmt2/jit examples/PyCMT2 examples/JIT
env -u PYTHONPATH python3 -B examples/PyCMT2/run_examples.py --keep-going
env -u PYTHONPATH python3 -B examples/JIT/run_examples.py --keep-going --log-dir examples/JIT/_logs
rg -n \"Traceback|FAIL:\" examples/JIT/_logs -S || true
```

Expected:
- All examples pass; no FAIL/Traceback in logs.


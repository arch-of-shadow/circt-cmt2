# PyCMT2 External Modules (E2E + Path API) Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `cmt2.module.extern.firrtl` + `cmt2.bind.value` with arguments work end-to-end and provide a path-based API + example for including external RTL in PyCMT2 simulation workspaces.

**Architecture:** Fix FIRRTL extmodule port generation to include `bind.value` argument ports. Add a small Python API to stage external RTL files into a `SimulationWorkspace`, and optionally associate RTL files with external module declarations so the workspace picks them up automatically.

**Tech Stack:** C++ (Cmt2→FIRRTL lowering), Python (`circt.pycmt2`), lit/FileCheck, Verilator via `SimulationWorkspace`.

---

### Task 1: Add failing conversion regression test (bind.value args)

**Files:**
- Create: `test/Dialect/Cmt2/extmodule-bindvalue-args.mlir`

**Step 1: Write the failing test**

Create a CMT2 circuit with:
- `cmt2.module.extern.firrtl` containing a `cmt2.bind.value` with **two args** and **one result**
- A `cmt2.module` that instantiates the extern module and `cmt2.call`s the value

FileCheck should assert the produced FIRRTL extmodule has input ports for both args.

**Step 2: Run to verify it fails**

Run: `build/bin/circt-opt test/Dialect/Cmt2/extmodule-bindvalue-args.mlir -cmt2-to-firrtl | build/bin/FileCheck test/Dialect/Cmt2/extmodule-bindvalue-args.mlir`
Expected: FAIL with `Input port not found` (or missing ports in output).

---

### Task 2: Fix Cmt2→FIRRTL extmodule port generation for bind.value

**Files:**
- Modify: `lib/Conversion/Cmt2ToFIRRTL/Cmt2ToFIRRTL.cpp`

**Step 1: Minimal implementation**

In `LowerCmt2ToFIRRTLPass::createExtModules`, extend the `BindValueOp` handling to also emit **argument input ports** using:
- `bindValue.getArgNames()`
- `bindValue.getFunctionType().getInputs()`

**Step 2: Re-run the regression**

Run: same command as Task 1
Expected: PASS

---

### Task 3: Add path-based external RTL staging API

**Files:**
- Modify: `lib/Bindings/Python/pycmt2/simulation.py`
- Modify (optional convenience): `lib/Bindings/Python/pycmt2/external_module.py`
- Modify (optional convenience): `lib/Bindings/Python/pycmt2/circuit.py`

**Step 1: Add `SimulationWorkspace.add_external_rtl_file(path, dest_name=None)`**

```python
ws.add_external_rtl_file("path/to/ALU.sv")
ws.add_external_rtl_file("path/to/rtl.v", dest_name="SomeName.sv")
```

**Step 2: (Optional) Allow associating RTL with `Circuit.external_module(..., rtl=...)`**

So users can do:

```python
with circuit.external_module("ALU", rtl="../rtl/ALU.sv") as alu:
    ...
```

and the workspace auto-copies it.

**Step 3: Manual check**

Run a tiny script (or the new example in Task 4) and verify the workspace contains the RTL file under `rtl/`.

---

### Task 4: Add external module E2E example with Verilator validation

**Files:**
- Create: `examples/PyCMT2/external_module_e2e.py`
- Create: `examples/PyCMT2/rtl/ALU.sv`

**Step 1: Add the example**

- Define `circuit.external_module("ALU")` with a value method `add(a, b) -> out` (args + result).
- Build a top module that stores inputs in regs, calls `alu.add`, and exposes a `value` to read back the sum.
- Use `SimulationWorkspace` + `Testbench` to:
  - reset
  - drive inputs via top-level action method ports
  - wait 1–2 cycles
  - `expect` the sum output for multiple vectors
- Use the new `add_external_rtl_file` (or `rtl=` association) to stage `examples/PyCMT2/rtl/ALU.sv`.

**Step 2: Run it end-to-end**

Run from `build/`:
`PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/external_module_e2e.py`
Expected: Verilator build succeeds; simulation prints `PASSED`.

---

### Task 5: Rebuild and keep it one commit

**Files:**
- Modify: (as above)

**Step 1: Build**

Run (only what’s needed):
`ninja -C build circt-opt tools/circt/lib/Bindings/Python/CIRCTPythonModules`

**Step 2: Sanity checks**

- Re-run `../examples/PyCMT2/comprehensive_example.py` and confirm no `OutOfBound`/`ResultOutOfBound` in generated SV.
- Run lit for the new regression file.

**Step 3: One commit**

Use `git commit --amend` to keep everything in a single commit.


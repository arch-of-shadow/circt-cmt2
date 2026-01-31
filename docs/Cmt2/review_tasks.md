# Cmt2 Documentation Review Tasks (Release Prep)

This file is the **single source of truth** for documentation work needed to get Cmt2 docs ready for release.

## Ground Rules (Release Prep)

- **Manual audit wins**: tests/examples are helpful for sanity, but they are not treated as proof of correctness.
- **Every doc↔code disagreement must be logged** for review in `docs/Cmt2/disagree.md` with:
  - an extract command (exact command line), and
  - a short summary (1–2 lines), no pasted outputs.
- **Release docs must not link to `docs/Cmt2/tmp/`**.
- Content in `docs/Cmt2/tmp/` that describes **implemented** features must be migrated into `docs/Cmt2/features/`.
- After migration, `docs/Cmt2/tmp/` documents should be **deprecated and removed**.
- Internal design notes should not be referenced by release docs; consolidate relevant content into `docs/Cmt2/features/` and remove stale links.

## Rules

- If any document statement conflicts with the code implementation, either:
  - **Fix the document**, or
  - **Add a new unchecked task** here describing what needs to change (doc and/or code), plus how to validate.
- Keep tasks small and check them off **as soon as they are done**.
- Prefer validation via **manual extract commands** (`rg`, `build/bin/circt-opt --help`, `build/bin/cmt2-dbg --help`, `python -c ...`).
- Use **Cmt2 lit tests** / **PyCMT2 E2E simulations** only as sanity checks (not as proof for doc claims).

## Task format (use this when adding new items)

```
- <checkbox> <ID> <Short title>
  - Owner files: <docs/...>, <lib/...>, <examples/...>
  - Validation: `<command(s)>`
  - Notes: <optional>
```

Where `<checkbox>` is either `- [ ]` (not done) or `- [x]` (done).

## Clarification

- `docs/Dialects/Cmt2/` should be removed once `docs/Cmt2/` is in place (no compatibility stubs).
- Preferred “visuals” format: Mermaid diagrams in Markdown, draw.io assets, or ASCII block diagrams? ASCII block diagram.
- Do you want `docs/Cmt2/tmp/` retained? No — migrate implemented content into `docs/Cmt2/features/` then remove `tmp/`.

---

# Manual Doc↔Code Consistency (New)

- [x] C0 Create `docs/Cmt2/disagree.md` and keep it updated during this work
  - Owner files: `docs/Cmt2/disagree.md`
  - Validation: `test -f docs/Cmt2/disagree.md`

- [x] C1 Remove all links from release docs to `docs/Cmt2/tmp/`
  - Owner files: `docs/Cmt2/guides/*`, `docs/Cmt2/features/*`, `docs/Cmt2/reference/*`, `docs/Cmt2/examples/*`
  - Validation: `rg -n "\\]\\((\\.\\./)?tmp/" docs/Cmt2 | head`

- [x] C2 Migrate implemented feature content out of `docs/Cmt2/tmp/` into `docs/Cmt2/features/`
  - Owner files: `docs/Cmt2/tmp/*.md`, `docs/Cmt2/features/*.md`
  - Validation: `ls docs/Cmt2/tmp` shows only a deprecation stub (or folder removed); `rg -n "docs/Cmt2/tmp" docs/Cmt2 | head`

- [x] C3 Deprecate/remove outdated design-note pages by consolidating into `docs/Cmt2/features/`
  - Owner files: `docs/Cmt2/features/*`, `docs/Cmt2/_index.md`
  - Validation: `docs/Cmt2/_index.md` points primarily to `features/` for release-facing content

- [x] C4 Manual “truth-claims” audit (log disagreements)
  - Owner files: `docs/Cmt2/guides/*`, `docs/Cmt2/features/*`, `docs/Cmt2/reference/*`
  - Validation: `docs/Cmt2/disagree.md` contains the final disagreement list (even if empty)
  - Notes: Use extract commands like `rg`, `build/bin/circt-opt --help`, `build/bin/cmt2-dbg --help`, `python -c ...`, etc.

---

# Migration & Link Fixes

- [x] M1 Move `docs/Dialects/Cmt2/` → `docs/Cmt2/` (keep filenames initially)
  - Owner files: `docs/Dialects/Cmt2/*`, `docs/Cmt2/*`
  - Validation: `rg -n "docs/Dialects/Cmt2" -S .` returns no release-doc references (only tracker history, if any)
  - Notes: Decide what to do with `docs/Dialects/Cmt2/tmp/` (see M4).

- [x] M2 Update repo-wide references to the new path
  - Owner files: `AGENTS.md`, `CLAUDE.md`, `examples/*/README*`, code comments that hardcode doc paths
  - Validation: `rg -n "docs/Dialects/Cmt2" -S .`

- [x] M3 Remove `docs/Dialects/Cmt2/` entirely (no compatibility stubs)
  - Owner files: `docs/Dialects/Cmt2/*`
  - Validation: `test ! -e docs/Dialects/Cmt2`

- [x] M4 Decide policy for `docs/Dialects/Cmt2/tmp/`
  - Owner files: `docs/Cmt2/tmp/*`
  - Validation: final tree clearly distinguishes **release docs** vs **drafts/design notes**
  - Notes: Migrate any implemented content into `docs/Cmt2/features/` and then remove `docs/Cmt2/tmp/`.

---

# Information Architecture (IA) Re-organization

- [x] IA1 Define the top-level structure for `docs/Cmt2/`
  - Owner files: `docs/Cmt2/` (new), `docs/Cmt2/_index.md` or `docs/Cmt2/README.md`
  - Validation: new landing page has “Start here”, “Feature tour”, “Guides”, “Reference”, “Internals”, “Examples”, “Debug/Sim”

- [x] IA2 Create a consistent folder layout and move pages accordingly
  - Owner files: `docs/Cmt2/guides/`, `docs/Cmt2/features/`, `docs/Cmt2/reference/`, `docs/Cmt2/examples/`
  - Validation: links updated; no “orphan” pages; `rg -n "\\]\\(\\.\\./" docs/Cmt2` looks reasonable

- [x] IA3 Add a “What is Cmt2?” one-page overview (release-facing)
  - Owner files: `docs/Cmt2/overview.md` (or `docs/Cmt2/_index.md`)
  - Validation: explains ORAAT/GAA, rules/methods/values, and how users start with PyCMT2

- [x] IA4 Make “Quick start” minimal and correct (first runnable example)
  - Owner files: `docs/Cmt2/guides/QuickStart.md`
  - Validation: copy/paste example runs; points to the correct `PYTHONPATH` and build steps

---

# Feature Tour (User-facing + Implementation Pointers)

- [x] F1 Proc control: dynamic vs static steps, seq/par/if/while, lowering overview
  - Owner files: `docs/Cmt2/features/Proc.md`, `docs/Cmt2/features/Lowering.md`, `docs/Cmt2/reference/Passes.md`
  - Validation: `examples/PyCMT2/proc.py` + `examples/PyCMT2/static_proc.py` covered and referenced

- [x] F2 Multi-cycle timing: `static_latency`, `interval`, pipeline initiation, cycle-precise semantics
  - Owner files: `docs/Cmt2/features/MultiCycle.md`, `docs/Cmt2/reference/Attributes.md`, `docs/Cmt2/features/Lowering.md`
  - Validation: `examples/PyCMT2/timing.py` matches docs; attributes names/examples compile

- [x] F3 Dataflow: tasks, tokens, LI/LS pipeline patterns, fork/join
  - Owner files: `docs/Cmt2/features/Dataflow.md`, `docs/Cmt2/reference/Operations.md`
  - Validation: `examples/PyCMT2/comprehensive_dataflow_example.py` + `examples/PyCMT2/dataflow_forkjoin.py` referenced

- [x] F4 Simulation & debugging: `cmt2-dbg`, debug ports, SimulationWorkspace, Testbench DSL
  - Owner files: `docs/Cmt2/guides/Debugging.md`, `docs/Cmt2/guides/DebugPorts-TestbenchDSL.md`
  - Validation: `examples/PyCMT2/interpret.py` script mode still works; E2E runner passes

- [x] F5 STL/ModuleLibrary semantics in simulation: what is interpreted vs what is RTL
  - Owner files: `docs/Cmt2/features/STL.md`, `docs/Cmt2/guides/Debugging.md`, `lib/Bindings/Python/pycmt2/simulation.py`
  - Validation: SimulationWorkspace only provides interpreter semantics for **true external modules** and dedupes RTL modules already in emitted SV
  - Notes: Address “SV should be self-contained” precisely (workspace-level self-contained vs single-file SV).

- [x] F6 Visual explanations (pick one style and apply consistently)
  - Owner files: `docs/Cmt2/features/*`
  - Validation: diagrams render in the chosen Markdown pipeline (Mermaid/drawio/ASCII)
  - Notes: Keep visuals small and focused per page.

---

# Examples & Release-Confidence Checks (Docs-backed)

- [x] E1 Maintain a curated “run these for confidence” section
  - Owner files: `docs/Cmt2/examples/Examples.md` (or equivalent), `examples/PyCMT2/run_examples.py`
  - Validation: docs list equals the curated runner list; no stale entries

- [x] E2 Ensure banked-memory GEMM example is documented as a “rich feature” showcase
  - Owner files: `docs/Cmt2/examples/Examples.md`, `examples/PyCMT2/banked_gemm_dataflow.py`
  - Validation: E2E simulation passes; docs explain banked memory + tiled/unrolled GEMM + decoupled load/compute/store

- [x] E3 Document the preferred validation commands (exact, copy/paste)
  - Owner files: `docs/Cmt2/_index.md`
  - Validation: commands still correct for the repo layout (build dir, PYTHONPATH)

---

# Per-Document Accuracy Audit (check each page with code)

> Add/remove items as files move during IA work. The goal is: **every release doc page is reviewed**.

- [x] D1 `_index.md` / landing page content is correct and link-complete
- [x] D2 `QuickStart.md` is runnable and points to correct next docs
- [x] D3 `Concepts.md` matches current ORAAT + scheduling semantics
- [x] D4 Release-facing concept docs align with current IR and lowering
- [x] D5 `Operations.md` matches current op set (names, regions, attributes)
- [x] D6 `Attributes.md` matches current attribute names/semantics
- [x] D7 `Passes.md` matches current pass pipeline and tool names
- [x] D8 `STL.md` matches current STL API surface (Reg/FIFO/Memory variants)
- [x] D9 `Debugging.md` matches current tooling and simulation workspace behavior
- [x] D10 `Examples.md` matches current examples; no stale paths or filenames
- [x] D11 `PyCMT2-Guide.md` matches the Python API surface and recommended workflow
- [x] D12 `ECMT2-Guide.md` matches the C++ API surface and build usage
- [x] D13 Internal design notes are not required for release docs

---

# Future / Design Notes (Non-release)

- (none) JIT docs are release-facing:
  - `docs/Cmt2/guides/JIT.md`
  - `docs/Cmt2/reference/TypeSystem.md`

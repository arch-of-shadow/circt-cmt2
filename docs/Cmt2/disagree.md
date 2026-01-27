# Cmt2 Doc↔Code Disagreements (Manual Audit)

This file lists **doc↔code disagreements** found during the manual release audit.

## Entry format (required)

- **Doc:** `<path>`
- **Claim:** <1 sentence>
- **Evidence:** `<exact command line>`
- **Summary:** <1–2 lines, no pasted outputs>
- **Proposed fix:** doc / code / both

---

## Disagreements

- **Doc:** `docs/Cmt2/reference/Passes.md`
  - **Claim:** Pass `cmt2-module-inliner` exists and can be invoked via `circt-opt input.mlir -cmt2-module-inliner`.
  - **Evidence:** `build/bin/circt-opt --help | rg -n "cmt2-module-inliner"`
  - **Summary:** No match; the pass is not registered under that name (inlining passes present are `cmt2-inline-modules` and `cmt2-inline-private-funcs`).
  - **Proposed fix:** doc
  - **Status:** fixed in `docs/Cmt2/reference/Passes.md` (use `cmt2-inline-modules`)

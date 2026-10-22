# JIT-Compiler — notes for cloud sessions

A JIT compiler for math expressions in C (Linux x86-64): lexer → parser → hash-consed SSA IR → IEEE-safe optimizer → backends (reference interpreter, direct x86-64 machine code, GCC via fork/execv/dlopen, WebAssembly).

- Build: `make` (binary `build/jit`). Verify: `make check` (unit tests, golden CLI cases, encoding check against objdump, fuzzers). Needs gcc, make, binutils (objdump), bash; `script` (util-linux) for the REPL test.
- C11, `-Werror`, no third-party libraries, LF line endings, no Python.
- Every backend must match the interpreter bit for bit (NaN as NaN). Semantics live in `src/ir/interp.c` (`ir_apply`); never re-implement an op elsewhere.
- Commit messages: `[TAG] - summary` (FEAT, BUG FIX, UPDATE, REFACTOR, DOCS, TEST). No AI attribution lines in commits.

<!-- fleet:autonomy:start -->
## Autonomy rules (fleet)

- Work only on a branch whose name starts with `claude/`. Never work on or push to `main` or `master`.
- Never read or print secrets or `.env` files.
- Before every push, run this repo's verify command (tests and/or build). Push only if it passes.
- Say "done" only when the task's written stop condition is checkably met. Show the check.
- If a fact is missing, write the question into `BLOCKED.md` at the repo root and move on to other work. Do not guess.
- This repo may lack GitHub branch protection; a push-guard hook blocks pushes to main/master, force-pushes and deletes. Never try to work around it.
- Stay inside this repo. Do not touch anything outside it.
<!-- fleet:autonomy:end -->

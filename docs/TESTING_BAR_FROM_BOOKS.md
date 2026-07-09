# Testing bar from the three normative books

**Sources (Downloads, confirmed present):**

| Book | Path |
|---|---|
| Feathers, *Working Effectively with Legacy Code* (2004/5) | `~/Downloads/Working effectively with legacy code.pdf` |
| Fowler, *Refactoring* 2e (2018) | `~/Downloads/[Addison-Wesley…] Refactoring_ Improving the Design of Existing Code (2018…).pdf` |
| Fowler, *Refactoring* 1e (1999) | `~/Downloads/Refactoring-Improving-the-Design-of-Existing-Code-Addison-Wesley-Professional-1999.pdf` |

This file is the **enforcement map** (not a book recap). Coverage measurement proof lives in [`BRANCH_COVERAGE_REPORT.md`](BRANCH_COVERAGE_REPORT.md).

## What each book requires (normative bar for this repo)

| Book idea | Enforcement in mad_pod_arena |
|---|---|
| **Fowler (both eds.): self-testing code** — tests decide whether behavior is preserved | `./tools/run_truth_suite.sh` is the sole behavioral authority; green suite = ship-safe for physics contracts |
| **Fowler: characterization via tests, not docs** | Fast goldens, gate A/B corpora, EXACT Fidelity↔`fast_physics` on battle JSON — pin *actual* physics law |
| **Feathers: legacy = code without tests; Cover and Modify** | Physics continent under characterization; bot pure island `ga_pure` + `//src/cg:ga_pure_test`; never Edit-and-Pray on world-step |
| **Feathers: unit tests run fast** | `//src/cg:ga_pure_test`, `//src/physics:test_physics` (in-process); battle/EXACT are *higher-level* characterization, kept separate |
| **Feathers: characterization tests when intent unknown** | Goldens + EXACT lock current CG-faithful behavior before refactor |
| **Fowler / Feathers: measure thoroughness, don’t claim it** | LLVM/Bazel coverage over real first-party tests → [`BRANCH_COVERAGE_REPORT.md`](BRANCH_COVERAGE_REPORT.md); uncovered branches either closed by real tests or listed with reason |

## What “every branch covered” means here (scoped honesty)

Literal 100% of every line of `ga_prelude` CG_STANDALONE dual paths, full GA search, and all timeout edges is often unreachable without **test theater**. Per goal plan:

1. **Measure** first-party `src/{physics,core,engine,cg}` product code under real tests.
2. **Close** high-value behavioral branches with tests that call **shipped** entry points.
3. **List** remaining product branches with one-line reasons (dead, amalgam-only, deferred non-goal).
4. **Never** reimplement production logic inside tests; never hard-code theater for a 100% badge.

## How to re-run proof

```bash
./tools/run_truth_suite.sh --quick          # behavior green
./tools/run_branch_coverage.sh             # regenerate coverage + report sections
```

## Ownership of tests vs coverage

| Concern | Owner |
|---|---|
| Behavioral truth | truth suite / verification policy |
| Pure bot math | `//src/cg:ga_pure_test` driving `ga_pure.h` |
| Physics unit/goldens | `//src/physics:test_physics` |
| Arena fidelity | `//src/engine:arena_fidelity_trace_test` |
| Coverage report (measured) | `docs/BRANCH_COVERAGE_REPORT.md` + script output |

# Plans

**Convention:** All design / refactor / architecture plans produced for this repo **must** be written as Markdown under `docs/plans/` (not only in chat).

## Naming

```text
docs/plans/YYYY-MM-DD-<short-kebab-topic>.md
```

Examples:

- `2026-07-09-ssot-forward-plan.md` (see also living `../SSOT_FORWARD_PLAN.md`)
- `2026-07-11-full-repo-refactoring-plan.md`

## Rules

1. **Plan-only work still lands a file** — if the user asked for a plan with no code changes, create/update the MD here and index it in `docs/README.md` when it is a major campaign.
2. **Living registers stay at `docs/` root** when they are continuously updated ownership tables (e.g. `SSOT.md`, `VERIFICATION_TRUTH_POLICY.md`). Snapshot *campaign* plans go under `plans/`.
3. **Do not edit historical plan files** to rewrite history; add a new dated plan or a short “Supersedes: …” line at the top of the new file.
4. Plans are **not** behavioral truth — `./tools/run_truth_suite.sh` is.

## Status values (campaign plans)

`Active | Superseded by <path> | Done`

| Status | Meaning |
|--------|---------|
| **Active** | Current campaign; execute from this file |
| **Superseded by <path>** | Do not execute; history only |
| **Done** | Campaign finished; keep for history |

## Index

| Plan | Status | Topic |
|------|--------|--------|
| [`2026-07-11-full-repo-refactoring-plan.md`](2026-07-11-full-repo-refactoring-plan.md) | **Active** | Full-repository refactoring factors, waves, package plan |
| [`2026-07-11-refactoring-plan-hardening-impl.md`](2026-07-11-refactoring-plan-hardening-impl.md) | **Done** | Implementation plan: harden Active refactoring plan (exit criteria, freeze list) |

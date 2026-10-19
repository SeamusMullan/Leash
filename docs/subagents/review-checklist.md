# WP Review Checklist

Use for every WP pull request. One line per finding: `path:line: severity: problem. fix.`

1. **Scope**: everything in the brief's Scope is present; nothing from Out of
   scope leaked in.
2. **Specs**: behaviour matches `docs/02`, `docs/04`, `docs/06`, `docs/07` and
   the brief's Interfaces; any deviation is documented in the same PR.
3. **Determinism**: no wall-clock, randomness, map-iteration order or
   completion-order dependence outside effect handlers; journal keys are
   structural.
4. **Enforcement**: budgets and capabilities are checked in the runtime path,
   not only in the driver.
5. **Journal**: every new effect kind writes started/finished entries, hashes
   inputs, redacts secrets, uses CAS above 8 KiB.
6. **Errors**: failures carry the right `FailureKind`; diagnostics have spans.
7. **Tests**: acceptance criteria have corresponding tests; live-then-replay
   round trip exists for runtime changes; golden journals updated deliberately
   (`UPDATE_GOLDEN=1` in a separate commit).
8. **Dependencies**: new dependencies justified; no raw owning pointers;
   `clang-format` and `clang-tidy` clean; shared types come from the module
   named in `docs/subagents/shared-interfaces.md` rather than being redefined
   locally.
9. **Docs**: user-facing behaviour documented; `docs/status.md` updated.
10. **Licensing**: every new commentable file carries the two-line SPDX header;
    new dependencies are on the allowlist; vendored code keeps upstream notices
    and says what changed (`docs/14-licensing.md`).
11. **Security**: no secrets in fixtures; sandbox paths cannot escape the
    working copy; shell commands are not passed through a shell unless
    allowlisted.

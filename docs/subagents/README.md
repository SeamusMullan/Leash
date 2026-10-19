# Working on Leash as a Subagent

This directory holds one brief per work package (WP-01 … WP-27). A brief is a
self-contained assignment: an agent (Claude Code, Codex, or a human) should be
able to read the brief plus the documents it links and deliver the package
without further instruction. If a brief is wrong or incomplete, fix the brief
in the same change.

## Before you start

1. Read `CLAUDE.md` at the repository root (conventions, commands, definition
   of done) and `docs/14-licensing.md` (file headers, package metadata,
   dependency allowlist).
2. Read `docs/00-vision.md`, skim `docs/13-glossary.md`, and read
   `docs/subagents/shared-interfaces.md` (normative: which module owns which
   shared type). If your brief sketches a type that file also defines, that
   file wins.
3. Read the brief for your WP and every document listed under "Read first".
4. Check `docs/status.md`: the WP must be `todo` or assigned to you, and its
   hard dependencies must be `done`. If a dependency is not done, stop and
   report; do not stub around it unless the brief says so.
5. Create a branch `wp/NN-short-slug` from `main`.

## While you work

- Deliver exactly the scope in the brief. Out-of-scope discoveries go into
  `docs/status.md` under "Follow-ups", not into your change.
- Keep the design documents true. If you must deviate from a spec, update the
  spec in the same PR and explain why in the PR description and, if it is a
  real decision, in a new ADR (`docs/adr/`).
- The tree must stay green: `cmake --preset dev && cmake --build build/dev`,
  `clang-format --dry-run --Werror`, `clang-tidy` over the compile database
  (`build/dev/compile_commands.json`) and `ctest --preset dev`.
- Tests are part of scope. A WP without the tests named in its acceptance
  criteria is not done.
- Prefer small commits with conventional messages (`feat(runtime): …`,
  `fix(parser): …`, `docs(spec): …`).
- Do not add dependencies beyond those named in the brief or ADR-0011 without
  writing one line of justification in the PR.
- No raw owning pointers and no manual `new` or `delete`; every handle is RAII.
  No network in unit tests. Integration tests that need an API key or a CLI
  carry the CTest label `network` with a reason and run locally with
  `LEASH_TEST_NETWORK=1 ctest --preset dev -L network`.

## When you finish

1. Update `docs/status.md`: set the WP to `review`, add the PR link, list
   follow-ups.
2. Open a PR titled `WP-NN: <title>` whose description contains: what was
   delivered, how it was verified (commands and their results), deviations
   from the brief and specs, follow-ups.
3. Report back in the QAC format (Questions, Actions, Concerns, Blocked) if
   you are an agent reporting to Sarah.

## Brief format

Every brief uses `_template.md`. Sections: Goal · Read first · Scope · Out of
scope · Interfaces · Deliverables · Acceptance criteria · Notes and pitfalls ·
Dependencies · Estimated size.

## Roles

- **Implementer**: executes a WP. Use `.claude/agents/wp-implementer.md` as the
  agent definition when spawning from Claude Code.
- **Reviewer**: reviews a WP PR against its brief and the specs; uses
  `docs/subagents/review-checklist.md`.
- **Architect (Claude, with Sarah)**: owns the specs and ADRs; resolves
  conflicts between briefs.

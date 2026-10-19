# Leash — instructions for coding agents

Leash is a programming language for harnessing coding agents: deterministic,
replayable, evidence-bound agent workflows. Read `docs/00-vision.md` first.

## Where things are

- `LICENSE`, `NOTICE`, `AUTHORS`: Apache-2.0; policy in `docs/14-licensing.md`.
- `docs/` — design documents (numbered, read in order), `docs/adr/` decisions,
  `docs/research/` paper notes, `docs/subagents/` work-package briefs,
  `docs/status.md` progress tracker.
- `spec/` — normative grammar (`grammar.ebnf`) and JSON schemas (LIR, journal, traces).
- `examples/` — Leash programs; they are also parser/type-checker fixtures.
- `src/` — C++ modules (created by WP-01). `std/` — standard library in Leash.
- `reference_paper/README.md` — arXiv links to the three papers the design is grounded in. The PDFs are not redistributed; fetch them if you need them.

## How work is organised

Work is split into work packages (WP-01 … WP-27) described in
`docs/subagents/`. Follow `docs/subagents/README.md` exactly: check
`docs/status.md`, respect dependencies, branch `wp/NN-slug`, deliver the scope,
update status, open a PR titled `WP-NN: <title>`.

## Conventions

- Every new source file starts with the two-line SPDX header from
  `docs/14-licensing.md`. New dependencies must carry a licence on the
  allowlist in that document.
- C++23 with Clang 19+ or GCC 14+. CMake presets and a vcpkg manifest.
  `clang-format --dry-run --Werror`, `clang-tidy` and `ctest` must pass.
- No raw owning pointers. RAII for every handle. No exceptions for control
  flow: use `std::expected<T, Failure>`. No new dependencies without one line
  of justification in the PR (allowlist in `docs/14-licensing.md`, choices in
  `docs/adr/0011-implementation-language-cpp.md`).
- Sum types are `std::variant` with a shared `overloaded` visitor. Each
  visitor has a unit test that visits every alternative.
- Async code is Boost.Asio with C++20 awaitables (`asio::awaitable`,
  `co_spawn`, strands, `asio::cancellation_signal`). Do not block the
  `io_context`.
- Errors: runtime user-facing errors are `Failure` with a `FailureKind`;
  compile-time diagnostics carry spans and go through the hand-written
  diagnostic renderer.
- Logging via spdlog; never write logs into the journal.
- Determinism rules (non-negotiable): no wall-clock, randomness, environment or
  completion-order dependence outside effect handlers; journal keys are
  structural (`docs/04-determinism-and-replay.md`).
- Budgets and capabilities are enforced in the runtime, never only in a driver.
- Every runtime change ships with a live-then-replay round-trip test.
- Secrets never appear in fixtures or journals.
- Docs are code: change the spec in the same PR when behaviour changes; add an
  ADR for real decisions.

## Commands (after WP-01)

```
just check        # format check + clang-tidy + ctest
just test         # ctest --preset dev
just run-example  # build/leash run examples/01_hello.leash with the mock driver
cmake --preset dev && cmake --build build/dev
```

## Style for docs and messages

Plain technical English, short sentences, no marketing. When reporting to
Sarah use the QAC format: Questions, Actions, Concerns, Blocked.

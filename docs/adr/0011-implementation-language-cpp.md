# ADR-0011 Implementation language: C++23

Status: accepted (2026-10-19). Sarah chose C++23 and Boost.Asio. The remaining
library choices are recommended defaults; change one by superseding this ADR,
not by editing briefs.

## Context

ADR-0001 chose Rust and recorded its one unproven assumption: "Sarah's Rust
fluency is unconfirmed". Sarah has now confirmed she does not know Rust and
does not want to learn it on this project. Her working languages are Python and
C++. She is taking a theory-focused module on programming language semantics
and intends to hand-write the interpreter core herself. Agents implement most
other work packages against her architecture. She also wants Boost in the
dependency set, with Boost.Asio as the async substrate, because it leaves room
for performance work later.

The requirements from ADR-0001 stand: lexer and parser, type and effect
checker, interpreter with a scheduler, subprocess and HTTP drivers, MCP client,
sandbox, CLI and LSP. One executable. Fast replay of large journals. Safe to
run against real repositories. Strong static checking, because agents write
most of the code.

One input to the old decision changed. Every work package is reviewed by the
architect, so the implementation language must be one she can read fluently.
ADR-0001 weighed compiler feedback for agents against Sarah's fluency; with her
veto, fluency wins.

## Decision

C++23, built with Clang 19+ or GCC 14+. Apple Clang has partial C++23 library
coverage, so macOS builds use Homebrew LLVM. CMake 3.30+ with
`CMakePresets.json`. Dependencies come from a vcpkg manifest. The build
produces one `leash` executable.

Conventions:

- Errors: `std::expected<T, Failure>` for recoverable errors. Exceptions only
  for unrecoverable allocation failure. `Failure` is a `std::variant` over
  `FailureKind` payloads.
- Sums: `std::variant` with a shared `overloaded` visitor. Every variant has
  one visitor header, and a unit test visits every alternative. Enum switches
  compile with `-Wswitch-enum`. C++ cannot prove exhaustiveness over variant
  alternatives; the test is the substitute.
- Ownership: RAII for every handle (sockets, files, pipes, subprocesses). No
  raw owning pointers, no manual `new` or `delete`. `std::unique_ptr` by
  default.
- Concurrency: Boost.Asio 1.86+ with C++20 awaitables. Effectful interpreter
  nodes are `asio::awaitable<T>`; pure nodes evaluate synchronously inside the
  coroutine. `io_context` is the scheduler, `co_spawn` starts a Leash agent
  task, strands serialise journal writes, `asio::cancellation_signal` carries
  cancellation, and an `asio::steady_timer` carries the wall budget inside the
  clock effect. HTTP is Boost.Beast over `asio::ssl`; a blocking client would
  stall the io_context, which is why libcurl is not used. ADR-0008
  (tree-walking) and ADR-0009 (branch-indexed journaling) stand. Only the
  substrate differs from tokio.
- Safety: `-Wall -Wextra -Wpedantic -Werror`, clang-tidy in CI, an ASan and
  UBSan CI job, libFuzzer targets for the lexer, the parser and journal
  deserialization.
- Format: clang-format with a committed `.clang-format`.

Libraries:

| Need | Choice | Note |
|---|---|---|
| JSON | nlohmann/json | Move hot replay paths to Boost.JSON only if profiling demands |
| HTTP and SSE | Boost.Beast over Boost.Asio SSL (OpenSSL) | SSE parsed from the response body stream |
| Async runtime | Boost.Asio (awaitables, strands, cancellation) | C++20 coroutines; no separate async library |
| Subprocess | Boost.Process v2 | Async pipes on the same io_context |
| CLI | CLI11 | Header-only |
| Logging | spdlog | Named loggers; never writes into the journal |
| Config | toml++ | Parses `leash.toml` |
| Hashing and ids | libblake3, Boost.UUID | UUID generation is an effect (clock/rand), never ambient |
| JSON Schema | valijson | Validates driver output and spec schemas |
| Testing | Catch2 v3, RapidCheck | Property tests inside Catch2 cases |
| MCP client | hand-written JSON-RPC 2.0 over stdio | No official C++ SDK; shares the JSON-RPC module with the LSP server |
| LSP server | hand-written JSON-RPC over stdio | Deferred to WP-23 |
| Glob and shell matching | hand-written in `src/policy` | Replaces globset and shell-words |
| Sandbox | Seatbelt on macOS, bubblewrap or landlock on Linux | Verified at WP-11 |

Boost 1.86 or later is required for Boost.Process v2. The vcpkg manifest pins
`boost-asio`, `boost-beast`, `boost-process`, `boost-uuid`, `openssl`,
`nlohmann-json`, `cli11`, `spdlog`, `tomlplusplus`, `blake3`, `valijson`,
`catch2` and `rapidcheck`.

Module layout, replacing the Rust crates:

```
src/
  spec/      shared vocabulary: Money, Usage, Budget, CapSet, Effect,
             FailureKind, payload refs, price table, embedded schemas
  syntax/    lexer, parser, AST, spans, diagnostics, formatter
  types/     resolution, type checker, effect inference, cap checker, schemas
  ir/        LIR model, lowering, JSON, hashing
  runtime/   interpreter, values, Failure, scopes, scheduler, journal, replay
  store/     content-addressed store, run directory, manifest
  policy/    shell tokenisation and matching, path policy, host matching
  effects/   effect handlers, built-in tools, sandbox
  drivers/   Driver interface, anthropic, hermes, claude_code, codex,
             managed_agents, mock; MCP client
  trace/     trace schemas, writers, report, outcome labels
  std/       Leash standard library source, embedded at build time
  cli/       `leash` binary
  lsp/       language server
  eval/      harness-ladder runner, metrics
  testkit/   fixtures, scripted mock drivers, journal assertions
```

Headers live at `src/<module>/include/leash/<module>/*.hpp`; sources live at
`src/<module>/*.cpp`. Dependency direction is unchanged in spirit: `spec →
syntax → types → ir → runtime → {effects, drivers, trace} → cli/lsp/eval`,
with `store` and `policy` beside `spec` as leaves.

## Alternatives

- **Rust.** Rejected by Sarah. Its type system remains the best fit for the
  checker; the veto is about who can read and review the code.
- **Go.** No sum types. The checker, LIR and `Failure` would lose
  exhaustiveness checking, the same reason ADR-0001 rejected it.
- **TypeScript.** Erased types at exactly the boundaries Leash exists to
  police, and a 90 MB bundled runtime (measured in ADR-0001).
- **OCaml.** Best semantics fit of the four. Rejected on fluency, ecosystem
  and distribution; Sarah does not write it.

## Consequences

- No compiler-proved exhaustiveness. Mitigated by small variants, one visitor
  per variant and an exhaustive unit test per visitor.
- Memory safety is a discipline, not a guarantee. Sanitizers, fuzzing and
  clang-tidy carry it. The sandbox and policy modules get extra review.
- No WebAssembly playground in v1 (ADR-0001 listed it as a Rust bonus).
- `docs/03` must be rewritten. The `async fn eval(&mut self, node)` shape
  survives as `asio::awaitable<Value>`; tokio becomes Asio, reqwest becomes
  Beast, serde becomes nlohmann, logos becomes a hand-written lexer, and
  miette becomes a hand-written diagnostic renderer.
- `CLAUDE.md` conventions must be rewritten: no cargo, clippy, nextest or
  `unsafe`; clang-format, clang-tidy, CMake and Catch2 instead.
- Every work-package brief in `docs/subagents/` is Rust-specific. None may be
  dispatched until its language section is converted. WP-01 becomes a CMake
  scaffold.
- The dependency licence allowlist in `docs/14-licensing.md` must be rebuilt.
  It gains BSL-1.0 (Boost), the OpenSSL licence, CC0 or Apache-2.0 (blake3),
  BSD-2-Clause (valijson) and BSD-3-Clause (CLI11 is already allowed).
- Boost and vcpkg build from source in CI. Pin a vcpkg baseline and enable a
  binary cache, or CI time grows without bound.
- ADR-0001's spike gate still applies. The first technical task is a
  Boost.Beast streaming SSE call against the Anthropic API with usage
  accounting, before WP-03. If it goes badly, reopen this ADR.
- ADR-0001 is superseded.

# ADR-0001 Implementation language: Rust

Status: accepted and confirmed (2026-09-12). Reviewed against a three-way
comparison with Go and TypeScript before confirmation; see Alternatives.

## Context
Leash needs a lexer/parser, a type and effect checker, an interpreter with an
async scheduler, subprocess and HTTP drivers, an MCP client, a sandbox, a CLI
and an LSP. It should ship as a single binary, be fast to replay large
journals, and be safe to run against real repositories. Most implementation
work will be done by coding agents following work-package briefs, so strong
static typing and compiler feedback matter more than typing speed. Sarah's
other projects are mostly Python and C++; a Rust toolchain (1.97) is installed.

## Decision
Rust, edition 2024, stable toolchain, a Cargo workspace of small crates.
Key crates: `logos` (lexer), hand-written parser, `serde`/`serde_json`,
`tokio`, `reqwest` (Anthropic API over raw HTTPS; there is no official Rust
SDK, and raw HTTP is the sanctioned route), `rmcp` (official MCP Rust SDK),
`clap`, `miette` (diagnostics), `tower-lsp`, `blake3`, `jsonschema`
(validation), `globset`, `shell-words`, `thiserror`/`anyhow`, `tracing`.

## The deciding argument
Every candidate has exactly one gap. They differ in whether the gap is paid once
or paid forever, and that is the decision.

| Need | Rust | TypeScript | Go |
|---|---|---|---|
| Anthropic API client | none official | `@anthropic-ai/sdk` | official SDK |
| MCP client and server | `rmcp` | `@modelcontextprotocol/sdk` | official SDK |
| LSP server | `tower-lsp` | `vscode-languageserver` | workable, less polished |
| Sum types for the checker | yes | yes | **no** |
| Runtime type guarantees | yes | **no** | yes |
| Binary size | 10-20 MB | **90 MB measured** | 10-20 MB |

Rust's gap is a one-time cost: an HTTP client against `POST /v1/messages` with
SSE parsing and usage accounting, roughly 400 to 600 lines in one module, then
finished. Go's gap is recurring: without sum types the AST, LIR, `Value` and
`Failure` become interfaces and type switches with no exhaustiveness checking,
across most of the codebase and precisely on the structures the determinism
guarantee rests on. TypeScript's gap is also recurring: erased types mean a
hand-written validator at every boundary Leash exists to police, kept in sync
with the type by discipline alone.

## Alternatives
- **TypeScript (Bun/Node).** Official Anthropic SDK, Claude Agent SDK and MCP
  SDK are first-class; fastest iteration. Verified against TypeScript 7.0.2:
  the Go-native compiler port is a compile-time change only and does not alter
  type erasure. A Bun-compiled binary measured 90 MB with 10 ms startup against
  a 1 ms native baseline. Node's permission model is now Stable and gates
  filesystem, network and child processes, which is better than Rust offers out
  of the box, but the flags are process-global and fixed at startup, so they
  cannot express Leash's per-scope capability narrowing. Documented runner-up.
- **Go.** Official Anthropic and MCP SDKs, single binary, excellent subprocess
  and concurrency, fast for agents to write. Rejected on sum types: the type
  checker, IR and interpreter are the majority of the code and would lose
  exhaustiveness checking entirely.
- **Python.** Sarah's most-used language; official SDKs. Too loose for a
  compiler/runtime that must be correct under concurrency; slow replay; poor
  distribution story for a language tool.
- **Go.** Single binary, simple concurrency. Weaker type system for the
  checker (no sum types), less pleasant parser/AST work.

## Consequences
- The Anthropic driver is hand-written against the HTTP API. Keep every request
  shape in one module with fixture-based tests so API drift is a contained edit.
- Compile times: mitigate with small crates, `cargo nextest`, `sccache`.
- Sarah's Rust fluency is unconfirmed; her other projects are Python and C++.
  Mitigation: the recommended first task is a spike writing the streaming
  Anthropic client, which is the single piece Rust makes hardest and is
  self-contained. If that spike goes badly, reopen this ADR before WP-03.
- A Rust core compiles to WebAssembly, which would allow a browser playground
  for the language later without a second implementation.
- Agent implementers need Rust-competent instructions: briefs include crate
  choices and idioms; CLAUDE.md sets conventions (no `unsafe`, clippy clean).
- LLM calls use raw HTTP; the driver must track API changes itself (headers,
  betas). Keep all API shapes in one module with tests against recorded
  fixtures.

# Work Package Status

Legend: todo · assigned(<who>) · in-progress · review(<PR>) · done · blocked(<why>)

| WP | Title | Brief | Status | Owner | PR | Notes |
|---|---|---|---|---|---|---|
| WP-01 | Repository scaffold, workspace, CI | [WP-01-repo-scaffold](subagents/WP-01-repo-scaffold.md) | done([#2](https://github.com/SeamusMullan/Leash/pull/2)) | | | merged; macOS CI fix in `fix/ci-rapidcheck` |
| WP-02 | Machine-readable spec files | [WP-02-spec-schemas](subagents/WP-02-spec-schemas.md) | review([#4](https://github.com/SeamusMullan/Leash/pull/4)) | | | 23 cases; valijson draft-7 `definitions` workaround |
| WP-03 | Lexer, parser, AST, formatter (v0 subset) | [WP-03-parser-v0](subagents/WP-03-parser-v0.md) | review([#5](https://github.com/SeamusMullan/Leash/pull/5)) | | | 44 cases; CLI, fuzz targets and fixtures |
| WP-04 | Interpreter core, journal, replay/resume (v0) | [WP-04-interpreter-journal-v0](subagents/WP-04-interpreter-journal-v0.md) | todo | | | |
| WP-05 | Mock driver and the Anthropic streaming spike | [WP-05-drivers-mock-anthropic-ask](subagents/WP-05-drivers-mock-anthropic-ask.md) | todo | | | |
| WP-06 | Run directory, manifest, CAS, trace layer v0 | [WP-06-run-dir-cas-traces-v0](subagents/WP-06-run-dir-cas-traces-v0.md) | todo | | | |
| WP-07 | Full parser and formatter | [WP-07-parser-full](subagents/WP-07-parser-full.md) | todo | | | |
| WP-08 | Type checker and JSON Schema generation | [WP-08-typechecker-schema](subagents/WP-08-typechecker-schema.md) | todo | | | |
| WP-09 | Effect inference, capability checks, diagnostics | [WP-09-effects-caps-check](subagents/WP-09-effects-caps-check.md) | todo | | | |
| WP-10 | LIR | [WP-10-lir](subagents/WP-10-lir.md) | todo | | | |
| WP-11 | Sandbox and built-in tools | [WP-11-sandbox-builtin-tools](subagents/WP-11-sandbox-builtin-tools.md) | todo | | | |
| WP-12 | Budget and capability enforcement | [WP-12-budget-cap-enforcement](subagents/WP-12-budget-cap-enforcement.md) | todo | | | |
| WP-13 | Anthropic driver (`ask` and `run` tool loop) | [WP-13-anthropic-run-tool-loop](subagents/WP-13-anthropic-run-tool-loop.md) | todo | | | |
| WP-14 | MCP client, Leash tools, per-run MCP server | [WP-14-mcp-and-tools](subagents/WP-14-mcp-and-tools.md) | todo | | | |
| WP-15 | Tasks, flows, checks, evidence, complete, report | [WP-15-tasks-checks-evidence](subagents/WP-15-tasks-checks-evidence.md) | todo | | | |
| WP-16 | Failures, recovery, state, checkpoints, fork | [WP-16-failures-state-checkpoints](subagents/WP-16-failures-state-checkpoints.md) | todo | | | |
| WP-17 | Context, memory, profiles | [WP-17-context-memory-profiles](subagents/WP-17-context-memory-profiles.md) | todo | | | |
| WP-18 | Claude Code driver | [WP-18-claude-code-driver](subagents/WP-18-claude-code-driver.md) | todo | | | |
| WP-19 | Codex driver | [WP-19-codex-driver](subagents/WP-19-codex-driver.md) | todo | | | |
| WP-20 | Scheduler and concurrency | [WP-20-scheduler-concurrency](subagents/WP-20-scheduler-concurrency.md) | todo | | | |
| WP-21 | Standard library and patterns | [WP-21-stdlib-patterns](subagents/WP-21-stdlib-patterns.md) | todo | | | |
| WP-22 | CLI completeness and UX | [WP-22-cli-ux](subagents/WP-22-cli-ux.md) | todo | | | |
| WP-23 | LSP and VS Code extension | [WP-23-lsp-editor](subagents/WP-23-lsp-editor.md) | todo | | | |
| WP-24 | Documentation site | [WP-24-docs-site](subagents/WP-24-docs-site.md) | todo | | | |
| WP-25 | Test infrastructure | missing | todo | | | |
| WP-26 | `leash eval` | missing | todo | | | |
| WP-27 | Hermes driver (`ask` and `run`) | [WP-27-hermes-driver](subagents/WP-27-hermes-driver.md) | todo | | | |

## Follow-ups

- RapidCheck deferred: its vcpkg port does not compile against current libc++
  (`std::copy`, `std::back_inserter`, `exception_ptr`). WP-25 chooses a
  maintained property-test library or a patched port.
- Verify the macOS CI job on the `fix/ci-rapidcheck` PR before closing it.

## Decisions pending

- None. WP-01 is ready to start.

## Decisions made

- 2026-09-12: Apache-2.0 for the whole repository, SPDX headers as project
  policy, Developer Certificate of Origin instead of a contributor licence
  agreement. See `docs/adr/0010-licensing.md` and `docs/14-licensing.md`.
- 2026-09-12: repository is https://github.com/SeamusMullan/Leash on GitHub
  Actions.
- 2026-10-19: C++23 with Boost.Asio replaces Rust (ADR-0011). Sarah's decision:
  C++ is her fluent language, Boost.Asio is the async substrate, and the
  remaining library choices are recorded in ADR-0011. ADR-0001 is superseded.
- 2026-10-19: the Hermes driver is built before Claude Code, and the M1 tracer
  bullet runs mock plus hermes (ADR-0004, roadmap v0.2).
- 2026-09-12: Rust confirmed as the implementation language after a three-way
  comparison with Go and TypeScript. Superseded 2026-10-19 by ADR-0011.
  See `docs/adr/0001-implementation-language.md`.
- 2026-09-12: `--strict` effect declarations default off for user packages and
  are mandatory for `std`; flip the default before 1.0. See `docs/02` §5.3.

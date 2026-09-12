# ADR-0004 Drivers as configuration; runtime owns the tool loop where possible

Status: accepted.

## Context
Agents can be reached through the Messages API, Claude Code, Codex, Managed
Agents and others. Each has different structured-output, tool and sandbox
features.

## Decision
A `Driver` trait with `ask` and `run`. For the API and mock drivers the
runtime owns the tool loop, so capabilities and budgets are enforced by
construction. For CLI drivers the runtime delegates the loop but (1) translates
capabilities to CLI flags, (2) exposes Leash and MCP tools through a per-run
MCP server so custom tools work uniformly, (3) parses event streams into child
journal entries, and (4) post-checks the diff against `fs.write`. Programs
never reference driver specifics beyond `std.drivers.*` constructors.

## Alternatives
Only API drivers (loses the value of mature agent CLIs); only CLI drivers
(loses precise enforcement and cheap `ask`); the Claude Agent SDK as the sole
backend (not available in Rust; would couple Leash to one vendor's harness).

## Consequences
- Uniform program semantics across drivers, with documented enforcement gaps
  for CLI drivers.
- Driver capability probing (`leash doctor`) and notes in the journal when
  options are ignored.

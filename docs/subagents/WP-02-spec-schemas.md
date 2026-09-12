# WP-02: Machine-readable spec: LIR, journal, trace schemas, profiles, prices

Milestone: M0 · Size: M · Status: see `docs/status.md`

## Goal
Turn the prose contracts in the design documents into JSON Schema and TOML
files that the runtime, trace layer, CLI and tests validate against, embedded
in a `leash-spec` crate. Later WPs code against these files; a change to a
format is a change to a schema first.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/04-determinism-and-replay.md` §3 (journal entry), §9 (manifest)
- `docs/11-episode-package-and-eval.md` §1 (eight traces), §2
- `docs/06-verification-and-evidence.md` §3 (Evidence), §6 (outcome labels)
- `docs/05-harness-model.md` §3 (profile visibility matrix)
- `docs/02-language-spec.md` §11 (type → JSON Schema), `spec/grammar.ebnf` (node kinds)
- `docs/07-budgets-and-capabilities.md` §1.2 (usage → cost)
- `docs/09-drivers-and-tools.md` §8 (price table)

## Scope
- `spec/journal.schema.json` (draft 2020-12): one entry object exactly as
  docs/04 §3, with `status ∈ {started, ok, failed, cancelled, timeout}`,
  `kind` enumerated (`llm.ask llm.run tool shell fs.read fs.write net human
  clock rand env checkpoint spawn race complete fail state`), `path` as an
  array of `[site_id, occurrence]` pairs, `usage`, `driver`, `caps`,
  `budget_after`, `failure` (a `Failure` object with `kind` enumerated),
  `reused_from`, `parent`, `children`, `note`. Payload references are
  `{"cas": "b3:<64 hex>"}`; document the 8 KiB rule in `description`.
- `spec/manifest.schema.json` from docs/04 §9.
- `spec/traces/{action,tool,context,verification,attribution,intervention,entropy,outcome}.schema.json`
  from docs/11 §1; `verification` entries are the Evidence object of docs/06 §3
  including `confidence` and `scope`; `outcome` uses the five labels of
  docs/06 §6 plus `justification` and `metrics` (the per-episode metrics of
  docs/11 §3, all optional).
- `spec/lir.schema.json`: versioned envelope `{lir_version, module, items[]}`
  and an enumeration of node kinds (`Let Var Assign If Match For While Loop
  Return Break Continue Fail Attempt Retry Parallel Race Ask Run Spawn Await
  Verify Complete Budget Allow Lambda Call Field Index Literal Record List
  Tuple Variant Block Checkpoint State`), each with `id`, `span`, `ty`,
  `effects`, and `site_id` on effectful kinds. Detailed per-kind fields are
  left as `additionalProperties: true` for WP-10 to tighten.
- `spec/profiles/h0.toml … h3.toml`: `[profile] name`, `[artifacts]` with a
  boolean per artifact key from the docs/05 §3 table
  (`tool_registry`, `test_command_registry`, `tool_usage_protocol`,
  `project_memory`, `task_state`, `context_selection_protocol`,
  `check_registry`, `reproduction_protocol`, `attribution_protocol`,
  `verification_protocol`, `report_template`), and `[system_prompt] sections = [...]`.
- `std/prices.toml`: `[models."<id>"] input, output, cache_read, cache_write`
  in USD per million tokens, with `verified_on = "2026-09-12"` and a `source`
  comment. Values: `claude-opus-5` 5/25, `claude-opus-4-8` 5/25,
  `claude-opus-4-7` 5/25, `claude-opus-4-6` 5/25, `claude-sonnet-5` 2/10,
  `claude-sonnet-4-6` 3/15, `claude-haiku-4-5` 1/5, `claude-fable-5-1` 10/50,
  `claude-fable-5` 10/50. Cache read = 0.1 × input and cache write = 1.25 ×
  input unless overridden; override `claude-fable-5-1` cache_read = 0.25 with a
  comment that the API reference lists that figure and it must be re-verified.
- `spec/README.md`: versioning rules (`journal_version`, `lir_version`,
  trace schema `$id` with version), how to bump, and the rule "schema first".
- Crate `crates/leash-spec`: embeds the files with `include_str!`, compiles
  them once (`std::sync::LazyLock`), and exposes validators and typed loaders.
- Fixtures: `spec/fixtures/journal/hello.jsonl` (a plausible 6-entry journal
  for `examples/01_hello.leash`: started/ok pairs for one `llm.ask`, a
  `complete`), `spec/fixtures/journal/invalid.jsonl`, one valid fixture per
  trace file, `spec/fixtures/manifest.json`.

## Out of scope
Producing these files at run time (WP-04, WP-06). Tightening per-node LIR
fields (WP-10). Price table auto-update. Non-USD currencies.

## Interfaces
```rust
pub mod journal { pub fn validate_line(line: &str) -> Result<(), SpecError>; pub const VERSION: u32 = 1; }
pub mod manifest { pub fn validate(v: &serde_json::Value) -> Result<(), SpecError>; }
pub mod lir { pub fn validate(v: &serde_json::Value) -> Result<(), SpecError>; pub const VERSION: u32 = 1; }
pub mod traces { pub enum TraceKind { Action, Tool, Context, Verification, Attribution, Intervention, Entropy, Outcome }
                 pub fn validate(kind: TraceKind, v: &serde_json::Value) -> Result<(), SpecError>; }
pub mod profiles { #[derive(serde::Deserialize)] pub struct Profile { pub name: String, pub artifacts: std::collections::BTreeMap<String, bool>, pub system_prompt_sections: Vec<String> }
                   pub fn load(name: &str) -> Result<Profile, SpecError>;  // "h0".."h3" embedded; else read <name>.toml from cwd }
pub mod prices { pub struct ModelPrice { pub input: f64, pub output: f64, pub cache_read: f64, pub cache_write: f64 }
                 pub struct PriceTable;  impl PriceTable { pub fn default_embedded() -> Self; pub fn with_overrides(self, toml: &str) -> Result<Self, SpecError>;
                 pub fn price_for(&self, model: &str) -> Option<&ModelPrice>; pub fn cost_usd(&self, model: &str, usage: &Usage) -> Option<Money>; } }
pub struct Usage { pub input_tokens: u64, pub output_tokens: u64, pub cache_read_input_tokens: u64, pub cache_creation_input_tokens: u64 }
pub struct Money(i64);      // exact micro-USD; see docs/subagents/shared-interfaces.md for the full impl
#[derive(thiserror::Error, Debug)] pub enum SpecError { Schema(String), Parse(String), UnknownProfile(String) }
```
`SpecError::Schema` messages must include the JSON pointer of the first failure.

## Deliverables
- The files under `spec/`, `spec/traces/`, `spec/profiles/`, `spec/fixtures/`, `std/prices.toml`, `spec/README.md`.
- `crates/leash-spec` with the API above and tests.

## Acceptance criteria
- `cargo nextest run -p leash-spec` passes with tests that: every line of
  `spec/fixtures/journal/hello.jsonl` validates; `invalid.jsonl` fails with a
  pointer to the offending field; each trace fixture validates against its
  schema and fails against a different one; `profiles::load("h3")` exposes all
  eleven artifacts and `load("h0")` none; `cost_usd("claude-opus-5", 1_000_000 in / 100_000 out / 0 / 0)` equals `$7.50`;
  `cost_usd` for an unknown model returns `None`.
- `python3 -c "import json,glob;[json.load(open(f)) for f in glob.glob('spec/**/*.json', recursive=True)]"` exits 0 (all files are valid JSON).
- Schemas declare `$schema: https://json-schema.org/draft/2020-12/schema` and a versioned `$id`.
- `spec/README.md` explains how to bump each version.

## Notes and pitfalls
- Use the `jsonschema` crate's compiled validators; compile once per process.
- Keep enumerations in one place per schema (`$defs`) so the runtime can
  reuse them via `serde` enums with identical spellings.
- Money must not be a float in schemas: represent `cost_usd` as a decimal
  string (`"0.2450"`); the Rust type is micro-dollar integer.
- Timestamps are RFC 3339 UTC with millisecond precision.
- Profile files are data on purpose (docs/05 §3): packages may add their own.

## Dependencies
Hard: WP-01. Soft: none.

## Estimated size
M (2–3 days).

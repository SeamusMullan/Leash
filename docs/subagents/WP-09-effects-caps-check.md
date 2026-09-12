# WP-09: Effect inference, capability static checks, diagnostics

Milestone: M2 · Size: M · Status: see `docs/status.md`

## Goal
After this WP, every function, tool, flow and lambda has an inferred effect set;
declared clauses are enforced as upper bounds; `leash check` proves what it can
about capabilities statically and explains a program's effect surface. This is
what makes the determinism guarantee (ADR-0002/0003) real and turns `leash
check --explain-effects` into a review tool.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/02-language-spec.md` §5 (effects), §8.5 (capability narrowing), §10.4 (branch isolation)
- `docs/03-runtime-architecture.md` §4.3, §4.4
- `docs/07-budgets-and-capabilities.md` §2.1, §2.2, §2.4, §4
- ADR-0003, ADR-0009
- WP-08's `TypedProgram`, `Ty::Fn { effects: EffectRow }`, `std_iface`

## Scope
- `EffectSet` (bit set over the 11 effects) and `EffectRow` (`Concrete(EffectSet)` | `Var(id)` | `Union`), with `fs.write ⇒ fs.read` normalisation.
- Inference: worklist fixed point over the call graph (SCCs), starting from the effects of primitive operations (spec §5.5) and std signatures; effect-polymorphic std functions (`map`, `filter`, `fold`, `retry`-like helpers) instantiate their row variables from argument lambdas.
- Declared `! {…}` clauses: body ⊆ declared else E0601; callers see the declared set. `pub` items without a clause: W0602, error under `--strict`. Module-level `let` must be pure: E0603. `tool` bodies: same rules as `fn`.
- Branch isolation (spec §10.4): assignment to an enclosing `var` or `state` field inside `parallel`/`race`/`map` bodies/`spawn` → E0604.
- `--explain-effects`: per item, print the effect set, the capabilities statically required (see below), and up to three sites per effect.
- Capability static checks over literal `allow`/`deny` sets: provable violations of narrowing between nested literal sets → E0701 (rules in Notes); literal `shell("…")`/`test("…")` command strings tokenised with `shell-words` and matched against the innermost literal `allow.shell` (E0702 if no pattern matches; W0703 if the command is non-literal); literal `fs.*` paths against `fs.read`/`fs.write` globs (E0704); literal `http.*` URL hosts against `net` (E0705); `gate`/`ask_human` without `human: true` in the innermost literal set (E0706); `parallel`/`race`/`map(concurrency > 1)`/`spawn` without `spawn: true` (E0707); tasks without an `allow` block use the defaults of docs/07 §2.4.
- Secret taint: values from `secrets.get(...)` flowing intra-procedurally (let bindings, interpolation, concatenation, record fields) into an `ask`/`run` prompt or `system` → W0710; error under `--strict`.
- Diagnostics codes E06xx/E07xx/W06xx/W07xx with spans; JSON output through the existing `leash check --json`.

## Out of scope
- Runtime enforcement of budgets and capabilities (WP-12). This WP never blocks a program for a non-literal case; it warns.
- Inter-procedural taint analysis; only intra-procedural, plus a conservative rule that any function taking a tainted argument taints its result.
- User-defined effects.

## Interfaces
```rust
// crate leash-types (module effects) — or a sibling crate leash-effects-check if leash-types grows too large; decide and record in the PR
#[derive(Clone, Copy, PartialEq, Eq, Hash)] pub enum Effect { Llm, Tool, Shell, FsRead, FsWrite, Net, Spawn, Human, Clock, Rand, Env }
#[derive(Clone, Copy, PartialEq, Eq, Default)] pub struct EffectSet(u16);
impl EffectSet { pub fn union(self, o: Self) -> Self; pub fn is_subset_of(self, o: Self) -> bool; pub fn iter(self) -> impl Iterator<Item = Effect>; pub fn normalised(self) -> Self; }
impl fmt::Display for EffectSet { /* "{llm, fs.read}" in the fixed order above */ }
pub enum EffectRow { Concrete(EffectSet), Var(RowVar), Union(Vec<EffectRow>) }
pub struct EffectAnalysis { pub item_effects: HashMap<ItemId, EffectSet>, pub lambda_effects: HashMap<NodeId, EffectSet>,
                            pub site_effects: HashMap<NodeId, EffectSet>, pub required_caps: HashMap<ItemId, StaticCaps> }
pub fn infer_effects(tp: &TypedProgram, std: &StdInterface, opts: &CheckOptions) -> (EffectAnalysis, Vec<Diagnostic>);
pub struct CapSetLit { pub fs_read: Globs, pub fs_write: Globs, pub shell: Vec<String>, pub net: Vec<String>, pub human: bool, pub spawn: bool, pub env: Vec<String>, pub secrets: Vec<String>, pub span: Span }
pub fn literal_capsets(tp: &TypedProgram) -> HashMap<NodeId, CapSetLit>;     // task-level and block-level `allow`/`deny`
pub fn check_capabilities(tp: &TypedProgram, ea: &EffectAnalysis, opts: &CheckOptions) -> Vec<Diagnostic>;
pub fn explain_effects(tp: &TypedProgram, ea: &EffectAnalysis) -> String;
pub struct CheckOptions { pub strict: bool }   // default false; `[check] strict = true` in leash.toml, or --strict
```
`EffectSet` and `Effect` are re-exported by `leash-ir` and used by the runtime; keep them dependency-free.

## Deliverables
- `effects/{set,infer,rows,isolation}.rs`, `caps/{literal,shell_match,check}.rs`, `taint.rs`, `explain.rs`.
- CLI flags `leash check --strict`, `leash check --explain-effects`. Strictness
  is **off by default** for user packages and **mandatory for `std`**, per
  `docs/02-language-spec.md` §5.3 and `docs/adr/0003-effect-system.md`. Read the
  setting from `[check] strict` in `leash.toml`, with the CLI flag overriding it.
  Add a CI job running `leash check --strict std/` so the standard library stays
  strict-clean from the moment it has content; until WP-21 lands it passes
  vacuously. Do not flip the global default in this WP.
- Tests: `tests/effects_positive.rs` (inferred sets for every item in the examples; assert exact sets), `tests/effects_negative/*.leash` with `insta` snapshots (≥ 20 cases: declared too small, impure module `let`, branch assignment, pub without clause under `--strict`, polymorphic `map` with an `llm` lambda, recursion, mutual recursion), `tests/caps/*.leash` (≥ 15 positive/negative), `tests/taint.rs`.
- A short section in `docs/02-language-spec.md` §5 if you discover a rule that is missing (list in PR).

## Acceptance criteria
- `cargo nextest run -p leash-types` → green.
- `leash check --explain-effects examples/02_fix_bug.leash` prints, among others: `flow FixEmptyPassword: {llm, tool, shell, fs.read, fs.write, spawn}` and `agent attributor: {}`; and for `tool file_issue` in `examples/05_mcp_tools.leash`: `{net, human}`.
- `leash check tests/effects_negative/declared_too_small.leash` → exit 2 with `E0601` naming the missing effect (`shell`).
- `leash check tests/effects_negative/branch_assign.leash` → `E0604`.
- `leash check --strict examples/05_mcp_tools.leash` → warns `W0602` for any `pub` item without a clause, or passes if none; `leash check --strict tests/effects_negative/pub_no_clause.leash` → exit 2.
- `leash check tests/caps/shell_not_allowed.leash` (calls `shell("rm -rf build")` under `allow { shell: ["npm test*"] }`) → `E0702` with the offending command and the nearest allowed pattern.
- `leash check tests/caps/narrowing_violation.leash` (inner `allow { fs.write: "**" }` under a task `fs.write: ["src/**"]`) → `E0701`.
- `leash check tests/caps/secret_in_prompt.leash` → `W0710`; with `--strict` → exit 2.

## Notes and pitfalls
- Provable narrowing violations (E0701), conservative rules only: child grants a boolean the parent denies; child `"**"` where parent is not `"**"`; child list contains a pattern with no parent pattern that is identical or `"**"` or a `<prefix>/**` of which the child is a lexical extension; for `shell`, a child pattern that is not identical to a parent pattern and not a lexical extension of one. Everything else is "cannot prove" → info diagnostic I0708 and runtime enforcement (WP-12 checks every scope on the stack, so unprovable cases are still safe).
- Shell matching semantics are owned by WP-11 (`leash-effects::policy::command_permitted`); to avoid a dependency from `leash-types` on `leash-effects`, put the pure matching function in a tiny crate `leash-policy` (globset + shell-words, no I/O) that both depend on. Coordinate with WP-11; whoever lands first creates the crate. Rule summary: tokenise both; unquoted control operators in the command are rejected outright; a pattern token is a glob over one command token; a last pattern token that is `*` or ends with `*` also matches all remaining tokens; otherwise token counts must be equal.
- Effect polymorphism: keep row variables local to a call; no let-generalisation of user lambdas is needed because lambdas are always applied to a known std or user function type.
- `run` contributes `llm` and, when the call or the agent has tools, `tool`; the tools' own effects (e.g. `shell` for a tool wrapping `shell`) are added to the enclosing effect set because the runtime executes them in the caller's scope.
- `examples/03_triage_parallel.leash` currently has no `allow` block but uses `map(concurrency: 8)` and `gate(...)`; under docs/07 §2.4 defaults this is E0707/E0706. The architect has been notified; until the example or the default changes, add the example to the test allowlist with a comment rather than weakening the rule.
- Keep `EffectSet` ordering fixed (`llm, tool, shell, fs.read, fs.write, net, spawn, human, clock, rand, env`) for deterministic output.

Diagnostic codes owned by this WP:

| Code | Meaning |
|---|---|
| E0601 | body effects exceed the declared clause |
| W0602 / E0602 | `pub` item without an effect clause (error under `--strict`) |
| E0603 | module-level `let` is not pure |
| E0604 | assignment to an enclosing `var`/`state` inside a concurrent branch |
| E0605 | `Handle<T>` value crosses an effect boundary (stored in state, journaled, or returned from a flow) |
| E0701 | provable capability narrowing violation |
| E0702 | literal shell command not permitted by the innermost literal `allow.shell` |
| W0703 | non-literal shell command; enforced at run time |
| E0704 | literal path not permitted by `fs.read`/`fs.write` globs |
| E0705 | literal URL host not permitted by `net` |
| E0706 | `gate`/`ask_human` without `human: true` |
| E0707 | concurrency construct without `spawn: true` |
| I0708 | narrowing cannot be proven statically; runtime enforcement applies |
| W0710 / E0710 | secret value flows into a prompt (error under `--strict`) |

`--explain-effects` output format (plain text, one block per item, sorted by item name):

```
flow examples.fix_bug.FixEmptyPassword
  effects: {llm, tool, shell, fs.read, fs.write, spawn}
  caps (static): fs.read "**"; fs.write src/**, test/**; shell "npm test*", "node harness/probe.js *"; spawn
  sites: llm @ 02_fix_bug.leash:70 (ask attributor), 02_fix_bug.leash:84 (run coder), …
         shell @ 02_fix_bug.leash:62 (shell "node harness/probe.js empty"), …
```
With `--json`, the same data as an array of objects `{item, effects, caps, sites}`.

## Dependencies
Hard: WP-08. Soft: WP-11 (shared `leash-policy` crate).

## Estimated size
M (2–4 days).

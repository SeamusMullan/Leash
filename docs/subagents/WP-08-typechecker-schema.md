# WP-08: Type checker and JSON Schema generation

Milestone: M2 · Size: L · Status: see `docs/status.md`

## Goal
After this WP, `leash check` type-checks whole programs bidirectionally,
produces a `TypedProgram` that later passes (effects, LIR, runtime) consume,
compiles refinement predicates into validators, and generates canonical JSON
Schemas for any type so that `ask`/`run`/tools/`--input` share one mapping.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/02-language-spec.md` §3 (types), §4 (expressions), §6.2–6.3 (`ask`/`run` typing), §7.2 (flows end in `complete`/`fail`), §11 (schema mapping), §12 (std surface)
- `docs/03-runtime-architecture.md` §4.1, §4.2, §4.5
- `spec/grammar.ebnf` and the WP-07 AST
- `docs/09-drivers-and-tools.md` §2 (why schemas need `additionalProperties: false` and full `required`)
- ADR-0006 (LIR needs stable typed nodes)

## Scope
- Name resolution: module symbol tables, `use` imports (named and namespace), item visibility (`pub`), enum variants, UFCS candidates, contextual keywords.
- Bidirectional checker: `infer(expr)` and `check(expr, expected)`; expected types flow into `ask`/`run`, record and variant literals, list/tuple literals, lambdas (including trailing lambdas typed from the callee's last parameter), `null`, and `fail` (type `Never`).
- Types: primitives, `T?`, `[T]`, tuples, structural records, nominal records via `type X = {…}` (coercion of literals by expected type, exact field match), aliases, enums with payload fields, `Map<K, V>` (K ∈ {String, Int}), function types with effect rows (carried, not checked here), generics with unification at call sites, opaque runtime types from the std interface.
- Calls: positional and named arguments, defaults, arity errors, UFCS resolution (`x.f(y)` → builtin method table first, then free `f`; ambiguity is an error), trailing lambda as last argument.
- `match` exhaustiveness for enums, `Bool`, optionals, tuples of those; literal patterns require a wildcard; unreachable arms are warnings.
- Control-flow check: every path of a `flow` body ends in `complete`, `fail`, or a `Never`-typed expression (E0510); `complete` argument matches the task `output`; `state` only as the first statement of a flow; assignments only to `var` locals and `state` fields; `break value` types unify per loop.
- Refinements: predicates type-check as pure `Bool` over the field and sibling fields, restricted to the refinement subset (comparisons, arithmetic, `and`/`or`/`not`, `len`, `contains`, `starts_with`, `ends_with`, `matches`, field access, `== null`); outside the subset → E0420. Each refined type gets a `Validator` evaluable over `serde_json::Value`.
- Task typing: `input` record becomes the flow parameter type; `require` predicates are `Bool`; `check` methods and `expect` predicates are typed against the method result record (`ShellResult`, `TestResult`, fn result); `budget`/`allow` literals are typed as `Budget`/`CapSet`.
- Std interface: a Rust-defined table (`leash_types::std_iface::builtin()`) declaring the v1 std surface of spec §12 with types and effect rows, plus opaque types (`Agent`, `Repo`, `Patch`, `Context`, `Handle<T>`, …). WP-21 may later migrate declarations to Leash source.
- JSON Schema generation per spec §11, canonical serialisation (fixed key order per node kind, declaration order for properties), BLAKE3 hash, `$defs`/`$ref` for recursive types, doc comments as `description`, refinement text appended to `description`.
- Diagnostics with `miette` spans and stable codes: E02xx names, E03xx types, E04xx refinements/schema, E05xx control flow; `leash check` and `leash check --json`.

## Out of scope
- Effect inference and capability checks (WP-09); this WP only records declared effect clauses on function types.
- Lowering/LIR (WP-10). Runtime evaluation of refinement predicates on `Value` (WP-04/15 reuse the JSON evaluator or convert).
- Global type inference across function bodies without annotations (parameters must be annotated, spec §3.7).

## Interfaces
```rust
// crate leash-types
pub type TypeId = u32;
pub enum Ty { Int, Float, Bool, String, Bytes, Duration, Money, Unit, Json, Never,
              Optional(TypeId), List(TypeId), Tuple(Vec<TypeId>), Map(TypeId, TypeId),
              Record { fields: Vec<Field>, nominal: Option<DefId> }, Enum(DefId, Vec<TypeId>),
              Fn { params: Vec<Param>, ret: TypeId, effects: EffectRow }, Named(DefId, Vec<TypeId>),
              Opaque(OpaqueKind), Var(TyVar) }
pub struct Field { pub name: Symbol, pub ty: TypeId, pub refinement: Option<ExprId>, pub doc: Option<String> }
pub struct TypeTable { … } // interning; structural equality; pretty-printing `fmt_type`
pub struct TypedProgram { pub program: Arc<ast::Program>, pub types: TypeTable, pub node_types: HashMap<NodeId, TypeId>,
                          pub items: HashMap<ItemId, ItemInfo>, pub resolutions: HashMap<NodeId, Resolution>,
                          pub validators: HashMap<TypeId, Validator>, pub diagnostics: Vec<Diagnostic> }
pub fn check_program(files: &[(FileId, ast::Program)], std: &StdInterface) -> TypedProgram;
pub mod schema { pub struct CanonicalSchema { pub json: String, pub hash: [u8; 32] }
                 pub fn schema_for(ty: TypeId, t: &TypeTable) -> CanonicalSchema; }
pub mod refine { pub struct Validator { … } pub struct Violation { pub path: String, pub predicate: String }
                 impl Validator { pub fn validate(&self, v: &serde_json::Value) -> Vec<Violation>; } }
pub struct Diagnostic { pub code: &'static str, pub severity: Severity, pub message: String, pub labels: Vec<(Span, String)>, pub help: Option<String> }
```

## Deliverables
- `crates/leash-types/src/{resolve,infer,unify,exhaustive,flow,refine,schema,std_iface,diag}.rs`.
- `leash check` wired to the checker; `--json` diagnostics.
- Tests: `tests/check_examples.rs` (all examples type-check with zero errors), `tests/negative/*.leash` with `insta` snapshots of diagnostics (≥ 30 cases covering every E-code family), `tests/schema.rs` (one test per row of spec §11 plus nested and recursive cases; hash stability), `tests/refine.rs` (validators accept/reject JSON), `tests/ufcs.rs`, `tests/exhaustive.rs`.
- `docs/02-language-spec.md` amended only if you find a hole; list every amendment in the PR.

## Acceptance criteria
- `cargo nextest run -p leash-types` → green.
- `leash check examples/` → exit 0, no diagnostics.
- `leash check tests/negative/missing_complete.leash` → exit 2 and a diagnostic `E0510` pointing at the flow header.
- `leash check tests/negative/nonexhaustive.leash` → `E0330` listing the missing variants by name.
- `leash check tests/negative/bad_refinement.leash` (uses `clock.now()` in a `where`) → `E0420`.
- `leash check --json examples/02_fix_bug.leash | jq '.diagnostics | length'` → `0`.
- A unit test asserts the schema for `Triage` from `examples/03_triage_parallel.leash` equals the checked-in golden JSON and that its hash is unchanged after adding a comment to the source.

## Notes and pitfalls
- `ask` without an expected type is `String`; `run` without one is `RunReport` (spec §6.3). Propagate expected types through `let x: T = …`, function arguments, `complete` (task output), record fields, list elements and `return`.
- Named arguments are matched by parameter name; unknown names are errors; do not reorder positionally in the AST (WP-10 does that).
- Structural record literal → nominal coercion requires an exact field set (spec §3.4). Missing or extra fields are errors with per-field labels.
- `Never` unifies with everything; `if` without `else` has type `Unit`.
- Refinement `Validator` must be deterministic and pure; use the same string semantics as the runtime (`len` counts Unicode scalar values; `matches` uses the `regex` crate with default flags).
- Canonical schema: emit keys in the order `type, enum, const, properties, required, additionalProperties, items, prefixItems, minItems, maxItems, anyOf, oneOf, $ref, $defs, description, pattern, contentEncoding`; no whitespace; UTF-8; this string is what is hashed and journaled.
- Enums with payloads use the tagged form with `"kind"`; a variant whose payload has a field named `kind` is a compile error (E0411).
- Diagnostics should suggest fixes where cheap (e.g. "did you mean `.High`?").

Diagnostic codes owned by this WP:

| Code | Meaning |
|---|---|
| E0201 | unresolved name / import |
| E0202 | duplicate definition |
| E0203 | private item used outside its package |
| E0301 | type mismatch (expected/found, with both pretty-printed) |
| E0302 | wrong arity / unknown named argument / missing argument without default |
| E0303 | ambiguous UFCS call |
| E0304 | cannot infer type (annotation required) |
| E0305 | unknown field / missing field / extra field in record literal |
| E0306 | variant literal without an expected enum type |
| E0330 | non-exhaustive `match` (lists missing patterns) |
| W0331 | unreachable `match` arm |
| E0411 | payload field named `kind` (clashes with the tagged schema) |
| E0420 | refinement predicate outside the refinement subset or not `Bool` |
| E0510 | flow body may fall off the end without `complete`/`fail` |
| E0511 | `complete` value does not match task `output` |
| E0512 | assignment to an immutable binding |

Std interface: `std_iface::builtin()` must at least declare everything in spec §12 that
the five examples use: `len`, `usd`, `file`, `select`, `shell`, `test`, `gate`, `fs.*`,
`drivers.*`, `mcp.connect`, `stdio`, `trace.*`, `Repo.{is_clean,diff}`, list methods
`filter any flat_map join`, string methods `contains lines`, `Agent`, `Attribution`
(as a user type in the example), `FailureKind`, `RunReport`, `ShellResult`, `TestResult`,
`Patch`, `Context`, `McpServer`, `Handle<T>`. Missing entries are found by running
`leash check examples/`; add them rather than special-casing.

## Dependencies
Hard: WP-07. Soft: WP-02 (schema fixtures may be cross-checked against `spec/` files).

## Estimated size
L (1–2 weeks).

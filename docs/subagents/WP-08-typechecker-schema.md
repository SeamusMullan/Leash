# WP-08: Type checker and JSON Schema generation

Milestone: M2 · Size: L · Status: see `docs/status.md`

## Goal
After this WP, `leash check` type-checks whole programs bidirectionally,
produces a `TypedProgram` that later passes (effects, LIR, runtime) consume,
compiles refinement predicates into validators, and generates canonical JSON
Schemas for any type so that `ask`/`run`/tools/`--input` share one mapping.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
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
- Refinements: predicates type-check as pure `Bool` over the field and sibling fields, restricted to the refinement subset (comparisons, arithmetic, `and`/`or`/`not`, `len`, `contains`, `starts_with`, `ends_with`, `matches`, field access, `== null`); outside the subset → E0420. Each refined type gets a `Validator` evaluable over `nlohmann::json`.
- Task typing: `input` record becomes the flow parameter type; `require` predicates are `Bool`; `check` methods and `expect` predicates are typed against the method result record (`ShellResult`, `TestResult`, fn result); `budget`/`allow` literals are typed as `Budget`/`CapSet`.
- Std interface: a C++-defined table (`leash::types::std_iface::builtin()`) declaring the v1 std surface of spec §12 with types and effect rows, plus opaque types (`Agent`, `Repo`, `Patch`, `Context`, `Handle<T>`, …). WP-21 may later migrate declarations to Leash source.
- JSON Schema generation per spec §11, canonical serialisation (fixed key order per node kind, declaration order for properties), blake3 hash through the `src/spec` wrapper, `$defs`/`$ref` for recursive types, doc comments as `description`, refinement text appended to `description`.
- Diagnostics with spans and stable codes: E02xx names, E03xx types, E04xx refinements/schema, E05xx control flow; `leash check` and `leash check --json`.

## Out of scope
- Effect inference and capability checks (WP-09); this WP only records declared effect clauses on function types.
- Lowering/LIR (WP-10). Runtime evaluation of refinement predicates on `Value` (WP-04/15 reuse the JSON evaluator or convert).
- Global type inference across function bodies without annotations (parameters must be annotated, spec §3.7).

## Interfaces
```cpp
// src/types/include/leash/types/ty.hpp
namespace leash::types {

using TypeId = std::uint32_t;
using Diagnostic = syntax::Diagnostic;        // owned by leash_syntax; do not redefine

enum class Prim { Int, Float, Bool, String, Bytes, Duration, Money, Unit, Json, Never };

struct Field {
    std::string name;
    TypeId ty;
    std::optional<ExprId> refinement;
    std::optional<std::string> doc;
};

struct PrimTy     { Prim prim; };
struct OptionalTy { TypeId inner; };
struct ListTy     { TypeId element; };
struct TupleTy    { std::vector<TypeId> elements; };
struct MapTy      { TypeId key; TypeId value; };
struct RecordTy   { std::vector<Field> fields; std::optional<DefId> nominal; };
struct EnumTy     { DefId def; std::vector<TypeId> args; };
struct FnTy       { std::vector<Param> params; TypeId ret; EffectRow effects; };
struct NamedTy    { DefId def; std::vector<TypeId> args; };
struct OpaqueTy   { OpaqueKind kind; };
struct VarTy      { TyVar var; };

using Ty = std::variant<PrimTy, OptionalTy, ListTy, TupleTy, MapTy, RecordTy, EnumTy,
                        FnTy, NamedTy, OpaqueTy, VarTy>;

struct TyVisitor {                            // one overload per alternative; tests reuse it
    void operator()(const PrimTy&) const;
    void operator()(const OptionalTy&) const;
    void operator()(const ListTy&) const;
    void operator()(const TupleTy&) const;
    void operator()(const MapTy&) const;
    void operator()(const RecordTy&) const;
    void operator()(const EnumTy&) const;
    void operator()(const FnTy&) const;
    void operator()(const NamedTy&) const;
    void operator()(const OpaqueTy&) const;
    void operator()(const VarTy&) const;
};

struct TypeTable {                            // interning; structural equality
    std::string fmt_type(TypeId ty) const;
};

// src/types/include/leash/types/refine.hpp
namespace refine {
struct Violation {
    std::string path;
    std::string predicate;
};

class Validator {
public:
    std::vector<Violation> validate(const nlohmann::json& v) const;
};
}  // namespace refine

// src/types/include/leash/types/schema.hpp
namespace schema {
struct CanonicalSchema {
    std::string json;
    std::array<std::byte, 32> hash;
};
CanonicalSchema schema_for(TypeId ty, const TypeTable& t);
}  // namespace schema

struct TypedProgram {
    std::shared_ptr<const syntax::ast::Program> program;
    TypeTable types;
    std::unordered_map<NodeId, TypeId> node_types;
    std::unordered_map<ItemId, ItemInfo> items;
    std::unordered_map<NodeId, Resolution> resolutions;
    std::unordered_map<TypeId, refine::Validator> validators;
    std::vector<Diagnostic> diagnostics;
};

TypedProgram check_program(std::span<const std::pair<FileId, syntax::ast::Program>> files,
                           const StdInterface& std);

}  // namespace leash::types
```
`Ty` is the module's sum type; `TyVisitor` is the shared `std::visit` visitor
and its unit test visits every alternative. valijson is used only at validation
boundaries (driver output, schema self-check), never inside the checker.

## Deliverables
- `src/types/{resolve,infer,unify,exhaustive,flow,refine,schema,std_iface,diag}.cpp`
  with headers under `src/types/include/leash/types/`.
- `leash check` wired to the checker; `--json` diagnostics.
- Tests: `tests/types/check_examples_test.cpp` (all examples type-check with zero errors), `tests/types/negative/*.leash` with `expect_golden` snapshots of diagnostics (≥ 30 cases covering every E-code family), `tests/types/schema_test.cpp` (one test per row of spec §11 plus nested and recursive cases; hash stability), `tests/types/refine_test.cpp` (validators accept/reject JSON), `tests/types/ufcs_test.cpp`, `tests/types/exhaustive_test.cpp`.
- `docs/02-language-spec.md` amended only if you find a hole; list every amendment in the PR.

## Acceptance criteria
- `ctest --preset dev -L types` → green.
- `build/dev/src/cli/leash check examples/` → exit 0, no diagnostics.
- `build/dev/src/cli/leash check tests/types/negative/missing_complete.leash` → exit 2 and a diagnostic `E0510` pointing at the flow header.
- `build/dev/src/cli/leash check tests/types/negative/nonexhaustive.leash` → `E0330` listing the missing variants by name.
- `build/dev/src/cli/leash check tests/types/negative/bad_refinement.leash` (uses `clock.now()` in a `where`) → `E0420`.
- `build/dev/src/cli/leash check --json examples/02_fix_bug.leash | jq '.diagnostics | length'` → `0`.
- A unit test asserts the schema for `Triage` from `examples/03_triage_parallel.leash` equals the checked-in golden JSON and that its hash is unchanged after adding a comment to the source.

## Notes and pitfalls
- `ask` without an expected type is `String`; `run` without one is `RunReport` (spec §6.3). Propagate expected types through `let x: T = …`, function arguments, `complete` (task output), record fields, list elements and `return`.
- Named arguments are matched by parameter name; unknown names are errors; do not reorder positionally in the AST (WP-10 does that).
- Structural record literal → nominal coercion requires an exact field set (spec §3.4). Missing or extra fields are errors with per-field labels.
- `Never` unifies with everything; `if` without `else` has type `Unit`.
- Refinement `Validator` must be deterministic and pure; use the same string semantics as the runtime (`len` counts Unicode scalar values; `matches` uses `std::regex` with the default ECMAScript grammar). TODO(port): confirm `std::regex` gives acceptable `matches` grammar parity and performance, or replace it with Boost.Regex.
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

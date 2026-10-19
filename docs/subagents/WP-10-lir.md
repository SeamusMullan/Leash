# WP-10: LIR — data model, lowering, stable ids, JSON

Milestone: M2 · Size: M · Status: see `docs/status.md`

## Goal
After this WP, every checked program lowers to the Leash Intermediate
Representation: a typed, desugared, JSON-serialisable tree with stable node
and site ids. The interpreter runs LIR (the WP-04 AST path is removed),
`leash ir` dumps it, `leash run --ir` consumes it, and journal keys are
structural rather than positional. This is the artifact tooling and
meta-agents consume (ADR-0006).

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/03-runtime-architecture.md` §5 (LIR), §6.2 (evaluator over LIR)
- `docs/04-determinism-and-replay.md` §2 (effect identity), §9 (manifest `lir_hash`)
- `docs/02-language-spec.md` §4 (semantics that desugaring must preserve), §6, §7, §10
- ADR-0006, ADR-0002
- `spec/lir.schema.json` from WP-02 (the schema is normative; extend it in the same PR if needed)
- WP-08's `TypedProgram`, WP-09's `EffectSet`

## Scope
- LIR data model in `leash_ir`: `Lir { lir_version, package, modules: std::vector<Module> }`, `Module { path, items }`, items (`Const`, `Type`, `Enum`, `Fn`, `Tool`, `Agent`, `Memory`, `Task`, `Flow`, `Test`) and a single `Node` variant for expressions/statements after desugaring. Every node carries `id: NodeId`, `span`, `ty: TypeId`, `effects: EffectSet`; effectful nodes carry `site: SiteId`.
- Id derivation (normative; put it in `docs/03` §5 when you land):
  - `NodeId` = the structural path from the item root, child indices joined by `.`, e.g. `0.2.1` (child order = evaluation order as defined in the LIR node's field order).
  - `SiteId` = `<module path>::<item name>#<node kind>@<NodeId>`, e.g. `examples.fix_bug::FixEmptyPassword#run@0.4.0.1.2`. Ids never contain line numbers; the `span` field carries them for humans.
  - Ids therefore change only when the structure before the node changes; formatting, comments, renaming a local variable, and changes *after* the node do not affect it. Test this.
- Lowering from `TypedProgram` with these desugarings: trailing lambda → last argument; UFCS `x.f(y)` → `Call(f, [x, y])`; named/default arguments → positional in declaration order with defaults inlined; `if/else if` chains → nested `If`; string interpolation → `Concat([Str, ToString(expr), …])`; `for p in xs {}` → `Iter { source, pattern, body }` with defined semantics (index order for lists, sorted keys for maps); `?.` → `Match` on null; `.X` variant shorthand → qualified `Variant(Enum, X)`; record literal coerced to nominal → `Construct(DefId, fields)`; `Duration` literals normalised to milliseconds; `Money` to integer micro-units plus currency; `budget {…} {…}` / `allow {…} {…}` → `Scope { kind, spec, body }`; `verify ensure` → `Verify(All)`; `checkpoint` keeps its name expression.
- JSON (de)serialisation with free `to_json` / `from_json` overloads for nlohmann/json behind a `kind` discriminator field, `lir_version: 1`, canonical output (object keys sorted, no insignificant whitespace, UTF-8), `lir_hash = blake3(canonical bytes)` through the libblake3 wrapper in `leash_spec`.
- Validation of emitted JSON against `spec/lir.schema.json` in tests with valijson.
- CLI: `leash ir <file> [--flow X] [--pretty]`; `leash run --ir <file.lir.json>` (CLI11 subcommands).
- Interpreter switch: replace the WP-04 AST evaluator with an LIR evaluator covering the M1 subset plus every node the lowering emits for the examples that WP-04 already ran; delete the AST evaluation path; all WP-04 tests pass against LIR (journal keys change; regenerate M1 golden journals in a separate commit and say so).
- Golden LIR fixtures under `tests/lir/<example>.lir.json`.

## Out of scope
- New runtime semantics (tasks, verification, scopes, concurrency) beyond what WP-04 already executed; those WPs implement evaluation for their nodes. This WP must still *lower* every construct so later WPs only add evaluator arms.
- Optimisations of any kind (ADR-0008).

## Interfaces
```cpp
// src/ir/include/leash/ir/lir.hpp
namespace leash::ir {

inline constexpr std::uint32_t kLirVersion = 1;

struct Lir {
    std::uint32_t lir_version = kLirVersion;
    std::string package;
    std::vector<Module> modules;
    TypeTableJson types;
    std::map<std::string, std::string> schemas;   // name -> JSON Schema text
};

struct Node;

// One struct per kind. Children are `std::vector<Node>` in evaluation order;
// a kind that owns a single child by value uses `std::unique_ptr<Node>`.
using NodeKind = std::variant<Lit, Local, Global, Construct, List, Tuple, Variant,
                              Field, Index, Call, Lambda, Block, Let, Var, Assign,
                              If, Match, Iter, While, Loop, Break, Continue, Return,
                              Fail, Attempt, Retry, Parallel, Race, Ask, Run, Spawn,
                              Await, Verify, Complete, Scope, Checkpoint, State,
                              Concat, ToString /* , … */>;

struct Node {
    NodeId id;
    Span span;
    TypeId ty;
    EffectSet effects;
    std::optional<SiteId> site;   // set on effectful kinds
    NodeKind kind;
};

// One operator() per alternative, in the order of the alias above.
struct NodeKindVisitor {
    void operator()(const Lit& n) const;
    void operator()(const Local& n) const;
    // … one per remaining alternative
};

struct NodeId { std::string value; };   // "0.4.0.1.2"
struct SiteId { std::string value; };   // "examples.fix_bug::FixEmptyPassword#run@0.4.0.1.2"

struct LirError {
    std::string message;
    std::optional<std::uint32_t> found_version;
};

std::expected<Lir, std::vector<Diagnostic>> lower(const TypedProgram& tp, const EffectAnalysis& ea);

std::string to_json(const Lir& lir);                        // canonical bytes
std::expected<Lir, LirError> from_json(std::string_view s); // checks lir_version
std::array<std::uint8_t, 32> hash(const Lir& lir);          // blake3, spans stripped

}  // namespace leash::ir
```
The runtime's `Interpreter(std::shared_ptr<const Lir> lir, …)` constructor is the
only entry point after this WP.

## Deliverables
- `src/ir/{model,lower,desugar,ids,json}.cpp` with headers under
  `src/ir/include/leash/ir/`, `spec/lir.schema.json` updated if needed.
- Evaluator switch in `src/runtime` (delete `eval_ast*`), CLI commands.
- Tests: `tests/ir/lower_examples_test.cpp` (all examples lower; JSON validates against the schema), `tests/ir/id_stability_test.cpp` (reformat, add comments, rename locals, append statements after a site → ids of earlier sites unchanged; insert a statement before → changed, as expected), `tests/ir/golden_lir_test.cpp` (testkit `expect_golden`, `UPDATE_GOLDEN=1` rewrites), `tests/ir/desugar_test.cpp` (one test per desugaring rule with expected LIR fragment), runtime tests re-pointed at LIR.
- `docs/03-runtime-architecture.md` §5 amended with the id derivation text.

## Acceptance criteria
- `ctest --preset dev -L '(ir|runtime)'` → green.
- `leash ir examples/02_fix_bug.leash | jq -r .lir_version` → `1`; `leash ir … | jq '[.. | .site? | strings] | map(select(test("@L")))' ` → `[]` (no line numbers in ids).
- `leash ir examples/01_hello.leash > /tmp/h.json && leash run --ir /tmp/h.json --driver anthropic=mock --input '{"topic":"x"}'` → completes with the mock driver and writes an episode package.
- `for f in examples/*.leash; do leash ir $f | python3 -m json.tool >/dev/null || exit 1; done` → exit 0.
- `grep -rn "eval_ast" src/runtime` → no matches.
- Golden LIR regeneration (`UPDATE_GOLDEN=1 ctest --preset dev -L ir`) produces no diff on a clean checkout.
- A test formats `examples/02_fix_bug.leash` with extra comments and blank lines and asserts every `SiteId` in the lowered LIR is identical to the original's.

## Notes and pitfalls
- Child index order must be the evaluation order the spec defines (e.g. for `Call`, callee first, then arguments left to right; for `Record`, fields in source order). Write the order down in comments in `ids.hpp`; WP-04/WP-16/WP-20 rely on it for occurrence counting.
- Spans remain in LIR for diagnostics and traces but are excluded from `hash` (hash the tree with spans stripped) so that `lir_hash` matches the id-stability property.
- `docs/04` §3 shows example journal paths like `…#retry@L61`; that example predates this WP. Journal paths must use the `SiteId` format above; the `site` object in the journal entry keeps file/line/col.
- Effect-polymorphic std calls are already resolved to concrete `EffectSet`s by WP-09; LIR stores only concrete sets.
- Keep `NodeKind` closed (no `Other`/`Raw` alternatives); the schema must enumerate kinds exactly.
- Header order matters: `struct Node;` first, then the kind structs holding `std::vector<Node>` children, then the `NodeKind` alias, then `Node` itself. `std::vector` tolerates the incomplete `Node`; a by-value `Node` member would not compile.
- The removal of the AST evaluator is deliberate (ADR-0006): resist keeping both paths.

## Dependencies
Hard: WP-08, WP-09, WP-02. Soft: WP-04 (the evaluator being switched).

## Estimated size
M (2–4 days).

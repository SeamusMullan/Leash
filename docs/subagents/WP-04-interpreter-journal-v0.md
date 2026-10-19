# WP-04: Interpreter core, journal, replay/resume (v0 subset)

Milestone: M1 · Size: L · Status: see `docs/status.md`

## Goal
Execute v0 programs end to end with the determinism model in place from day
one: a tree-walking evaluator, an effect gateway, a journal with structural
effect ids, and `leash run`, `leash replay`, `leash resume`. After this WP,
`examples/01_hello.leash` runs against a driver, and replaying its journal
reproduces the outcome with no network.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/adr/0011-implementation-language-cpp.md` (C++23, Boost.Asio awaitables, `std::expected` errors)
- `docs/04-determinism-and-replay.md` (all; §2 and §4 are the algorithms to implement)
- `docs/03-runtime-architecture.md` §5–§7
- `docs/02-language-spec.md` §4 (expressions), §5 (effects), §6.2 (`ask`), §9 (failures), §11 (schema mapping, minimal subset here)
- `spec/journal.schema.json` (WP-02), `docs/10-cli-and-tooling.md` §1 (exit codes, flags)
- `docs/adr/0002`, `0003`, `0008`

## Scope
- `src/runtime` (CMake target `leash_runtime`):
  - `program.cpp`: the evaluator's input model for v0 (`Program`, `Flow`,
    `Fn`, `Agent`, `Task`, `Expr`, `Stmt`, `TypeRef`) and
    `std::expected<Program, Failure> from_ast(const syntax::ast::Program&)`.
    The evaluator depends only on `program.hpp`; WP-10 replaces `from_ast` with
    `from_lir`. Write this migration note at the top of the header.
  - `value.cpp`: `Value` per docs/03 §6.1 as a `std::variant` over the tags v0
    needs (`Unit`, `Null`, `Bool`, `Int`, `Float`, `Str`,
    `std::chrono::nanoseconds`, `Money`, `List`, `Record`, `Enum`, `Fn`,
    `Opaque`), every payload behind
    `std::shared_ptr<const T>` so copies are reference-count bumps; canonical
    JSON conversion (`to_canonical_json`: sorted keys, no floats with NaN,
    `Money` as decimal string), structural equality. `ValueVisitor` is declared
    beside the variant and a unit test visits every alternative (ADR-0011).
  - `env.cpp`: lexical scopes, immutable bindings, closures capturing by value.
  - `eval.cpp`: `asio::awaitable<std::expected<Value, Control>> Interpreter::eval(const Expr& expr)`
    with `Control = std::variant<Fail, Complete>`. v0 has no `break` or
    `return`, so those two alternatives are the whole set; WP-07 extends it.
    Built-ins for v0:
    `len`, `usd`, `to_string`, list `filter/map/any/all/first`, string
    `contains/lines/trim`; `if`; calls with named args and defaults; lambdas;
    UFCS dispatch to built-in methods.
  - `effect.cpp`: `EffectKind`,
    `EffectRequest { EffectPath path; EffectKind kind; nlohmann::json input; Site site; }`,
    `EffectResult`, and
    `asio::awaitable<std::expected<EffectResult, Failure>> EffectGateway::perform(const EffectRequest&, EffectCtx&)`
    implementing the docs/04 §4 algorithm for modes `live | replay | resume`
    (`fork` is WP-16).
  - `effect_path.cpp`: activation stack and occurrence counters producing the
    `EffectPath` of docs/04 §2; `effect_id = blake3(canonical_json(path))`
    rendered as `b3:<hex>` through the libblake3 wrapper in `leash_spec`;
    `site_id` for v0 = `"{module}::{item}#{node_kind}@L{line}"`
    (WP-10 supplies structural ids; keep the function in one place).
  - `journal.cpp`: `Journal::create(path)`, `open(path)`, `append_started(entry)`,
    `append_finished(entry)` (fsync after each line),
    `std::map<EffectId, JournalEntry> index() const` (last entry wins), line
    validation against `leash_spec`'s journal schema in debug builds;
    `PayloadStore` interface with `InlineStore` (everything inline; WP-06 adds
    CAS) and the 8 KiB threshold constant.
  - `schema.cpp`: v0 type annotation → JSON Schema for records of primitives,
    lists, optionals, named record types (spec §11 rows for those); hash of
    the canonical schema. WP-08 replaces this module.
  - `failure.cpp`: `Failure { FailureKind kind; std::string message;
    std::optional<nlohmann::json> data; Site site; std::unique_ptr<Failure> cause; }`
    as declared in `docs/subagents/shared-interfaces.md`, and `FailureKind` (all
    eleven kinds) from `leash_spec`.
  - `run.cpp`: `Runner(program, DriverRegistry, mode, journal_path)` and
    `asio::awaitable<RunOutcome> run(std::string_view flow, Value input)`
    where `RunOutcome { std::variant<Completed, Failed> status; std::uint64_t effects; }`;
    `require`/`ensure`/budgets are ignored in v0 but the task `budget` record is written into the journal's first entry as `note`.
  - `ask` semantics of docs/02 §6.2 for `String` and record/list results:
    schema derived from the `let` annotation, driver call, parse, validate
    against the schema (valijson), on failure re-ask with the error
    appended up to `retries` (default 2), each attempt a separate effect with
    its own occurrence.
- `src/cli`: `run <file> [--flow NAME] [--input JSON|@file] [--driver NAME=KIND:PATH]... [--offline]`,
  `replay <run-id-or-path>`, `resume <run-id-or-path>`. `--offline` installs a
  gateway guard that fails any live effect with `Replay`. Run directory for now:
  `.leash/runs/<UTC>_<flow>_<6hex>/journal.jsonl` (WP-06 formalises the rest).
- Exit codes per docs/10 §1 (0, 1, 2, 3).

## Out of scope
Budgets and capabilities (WP-12), `run`/tools (WP-11/13), CAS and traces
(WP-06), fork/checkpoint/state (WP-16), concurrency (WP-20), the full type
checker (WP-08), LIR (WP-10), any driver code beyond calling the WP-05 interface.

## Interfaces
```cpp
// src/runtime/include/leash/runtime/mode.hpp
enum class Mode { live, replay, resume };

// src/runtime/include/leash/runtime/effect_path.hpp
struct EffectPath { std::vector<std::pair<SiteId, std::uint32_t>> segments; };

struct EffectId {
    std::array<std::uint8_t, 32> bytes{};
    std::string to_string() const;                  // "b3:<hex>"
    auto operator<=>(const EffectId&) const = default;
};

// src/runtime/include/leash/runtime/payload_store.hpp
class PayloadStore {
public:
    virtual ~PayloadStore() = default;
    virtual std::expected<PayloadRef, Failure> put(std::span<const std::byte> bytes) = 0;
    virtual std::optional<std::vector<std::byte>> get(const PayloadRef& ref) const = 0;
};

// EffectCtx is owned by leash_effects (src/effects/include/leash/effects/effect_ctx.hpp,
// WP-11): handlers consume it and leash_effects cannot depend on the runtime. The
// runtime passes it by reference.
//
// DriverRegistry is owned by leash_drivers (src/drivers/include/leash/drivers/registry.hpp,
// WP-05); the runtime includes that header.

// src/runtime/include/leash/runtime/runner.hpp
class Runner {
public:
    Runner(Program program, DriverRegistry drivers, Mode mode, std::filesystem::path journal_path);
    asio::awaitable<RunOutcome> run(std::string_view flow, Value input);
};
```
`PayloadRef` is `leash_spec`'s. The runtime depends on `leash_drivers` (WP-05)
for the `Driver` interface and adapts `EffectCtx` to `DriverCtx`. Until WP-05
lands, code against the interface in WP-05's brief and use a local stub in
tests.

## Deliverables
- `src/runtime` (target `leash_runtime`) with the sources above and Catch2
  tests under `tests/runtime/`, registered with CTest and the `runtime` label.
- CLI subcommands `run`, `replay`, `resume` under `src/cli/cmd/`.
- `tests/fixtures/mock/hello.json` mock script for `examples/01_hello.leash`
  and an integration test executable `leash_integration` under
  `tests/integration/` with the tests below.

## Acceptance criteria
- `build/dev/src/cli/leash run examples/01_hello.leash --input '{"topic":"agent harness"}' --driver anthropic=mock:tests/fixtures/mock/hello.json`
  prints a run id and `outcome: completed` and writes `journal.jsonl` whose every line validates against `spec/journal.schema.json`.
- `env -u ANTHROPIC_API_KEY build/dev/src/cli/leash replay <run-id> --offline` exits 0, performs zero driver calls (assert via a counting mock in the integration test), and prints the same completed value.
- Integration test `live_then_replay_round_trip`: run with mock → replay → identical `RunOutcome`, identical effect id multiset.
- Integration test `shuffled_journal_replays`: shuffle journal lines (keeping started/finished order per id) → replay succeeds with identical outcome.
- Integration test `crash_then_resume`: mock script fails the second `ask` with an injected crash hook (`Runner::set_crash_at(seq)`); `resume` completes the run; the journal shows the interrupted entry as `started` followed by a fresh `ok` line.
- Integration test `divergence_detected`: change the prompt text in the program, replay the old journal → exit code 3 and a message naming the site and showing the input hash mismatch.
- Integration test `ask_validation_retry`: mock returns invalid JSON once, then valid → two `llm.ask` effects with occurrences 0 and 1; final value typed.
- `ctest --preset dev -L runtime` and `ctest --preset dev -L integration` pass;
  `run-clang-tidy -p build/dev -warnings-as-errors '*'` is clean over the new
  sources.

## Notes and pitfalls
- fsync the journal before returning any effect result to the evaluator (docs/03 §7).
- Never key journal lookups by `seq`; only by `EffectId`. Keep `seq` for humans.
- JSON object ordering: build canonical JSON from `std::map`-backed objects and
  do not switch the project to `nlohmann::ordered_json`.
- Floats in canonical JSON: use shortest round-trip formatting (`std::to_chars`);
  reject NaN/Inf at construction with an `Unknown` failure.
- Do not implement `Fork` yet, but make `Mode` an enum so WP-16 adds it.
- The `started` line must be written before the driver is called; the
  `finished` line replaces it logically (same id, later `seq`).
- Keep `eval.cpp` free of Asio primitives other than `co_await`; concurrency
  arrives in WP-20 through the gateway and a scheduler module.
- Secrets redaction is WP-12; leave a `redact()` hook in `journal.cpp` that is
  the identity for now, and call it on every payload.
- `Value` is immutable and persistent-by-copy (docs/03 §6.1): a payload is
  `std::shared_ptr<const T>`, so a "mutation" builds a new payload and swaps
  the `shared_ptr`. Never cast the const away.
- Awaitables are not copyable and `asio::cancellation_signal` is not copyable
  either: pass `EffectCtx` and `DriverCtx` by reference through the whole call
  chain, and keep them alive for the duration of the `co_await`.

## Dependencies
Hard: WP-02, WP-03. Soft: WP-05 (needed for the live acceptance criterion; the round-trip tests can use an in-test stub driver).

## Estimated size
L (1–2 weeks).

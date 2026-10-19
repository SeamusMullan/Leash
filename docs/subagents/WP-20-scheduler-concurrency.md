# WP-20: Scheduler: parallel, race, map, spawn/await, cancellation

Milestone: M6 · Size: L · Status: see `docs/status.md`

## Goal
After this WP, Leash programs can run branches concurrently with `parallel`, `race`, `map(concurrency:)` and `spawn`/`await`, and every such run replays deterministically from its journal regardless of the order in which branches finished. Concurrency is structured (nothing outlives its block), budget-aware (branches draw from the enclosing scope or from an explicit partition) and cancellable end to end (interpreter → effect handlers → drivers → subprocesses). `examples/03_triage_parallel.leash` and `examples/04_review_debate.leash` run and replay.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/02-language-spec.md` §6.4 (spawn/await), §9.5 (failures across branches), §10 (concurrency)
- `docs/03-runtime-architecture.md` §6.2–§6.4 (interpreter, scopes, scheduler), §7 (effect gateway), §8 (handler `cancel`)
- `docs/04-determinism-and-replay.md` §2 (effect identity, branch activations), §4 (replay algorithm), §5 (`race`, cancellation rows), §10 (tests)
- `docs/07-budgets-and-capabilities.md` §1.1 (scopes, `parallel(budget:)`)
- `docs/08-multi-agent.md` §5–§6
- `docs/adr/0009-structured-concurrency.md`
- `spec/grammar.ebnf` (`ParallelExpr`, `RaceExpr`, `SpawnExpr`, `AwaitExpr`, `TrailingLambda`)

## Scope
- `parallel { e1, …, en }` evaluates all branches concurrently and yields a tuple in syntactic order. Named options: `limit: Int` (max concurrent branches), `budget: Budget` (per-branch partition), `on_fail: .CancelOthers | .WaitAll` (default `.CancelOthers`).
- `race { e1, …, en }` yields the value of the first branch that completes successfully; the winner index is journaled as a `race` entry; losing branches are cancelled and their in-flight effects get `status: "cancelled"` entries. If every branch fails, `race` fails with the first failure in syntactic order and the others in `data.siblings`.
- `map(xs, concurrency: k) { x => e }` (std native, effect-polymorphic) yields a list in input order; item `i` runs under branch index `i`.
- `spawn <ask|run|call>` returns `Handle<T>`; `await h` joins. A handle not awaited when its enclosing block exits normally is awaited there; on failure exit, un-awaited handles are cancelled, then the failure propagates. Awaiting twice is a runtime `Unknown` failure ("handle already consumed"); rejecting it statically is a follow-up.
- Branch effect paths per `docs/04` §2: each branch activation appends `(site_id_of_construct, branch_index)`; `map` uses the item index; `spawn` uses the occurrence counter of the `spawn` site. Journal lookup during replay is by effect id only, so completion order is irrelevant.
- Budgets: branches charge the enclosing `BudgetScope`. `parallel(budget: B)` gives each branch a child scope with limits `B`; if `n × B` exceeds the parent's remaining amounts in any dimension, fail `Budget` at entry, before starting any branch. `race` losers' partial consumption is charged.
- Failure propagation by syntactic branch order after settlement: with `.CancelOthers`, on the first failure the runtime cancels the rest, waits for them to settle, then raises the failure of the lowest-index failed branch with `data.siblings = [failures/cancellations of other branches]`. With `.WaitAll`, all branches run to completion before the same selection is made.
- Cancellation: one `asio::cancellation_signal` per branch, forwarded from the block's signal, itself forwarded from the run's signal (Ctrl-C). The effect gateway passes the signal in `EffectCtx`; handlers must abort HTTP requests, send SIGTERM then SIGKILL after the grace period (`docs/02` §6.4, default 10 s) to subprocesses, and write `cancelled` journal entries for in-flight effects. TODO(port): `asio::cancellation_signal` has no parent/child link, so the scheduler needs one forwarding slot per child (`parent.slot().assign([&] { child.emit(...); })`).
- Compile-time isolation check (implemented in WP-09's checker, specified here): inside a branch body (a `parallel` or `race` branch expression, the trailing lambda of `map`, or a `spawn` operand) any assignment (`=`, `+=`, `-=`) whose target is a `var` or `state` field declared outside that body is an error. Proposed code `E0410`, message: ``branch assigns enclosing mutable binding `{name}`; branches communicate by return values (docs/02 §10.4)``. Reads remain allowed. Also reject a `Handle` value flowing into an effect input (`E0411: handle cannot cross an effect boundary`).
- Determinism test with randomised mock latencies (`latency_ms: {"jitter": [0, 500]}` in the mock script, WP-05/WP-25): the set of effect ids and the outcome are identical across runs; replay succeeds.
- Chaos test for concurrent runs: kill the process at random points during `examples/03` with the mock driver, `leash resume`, assert the outcome equals an uninterrupted run and no effect id has two `ok` entries.
- Writer serialisation: two `run`s with write capability on the same working copy execute one after the other; the order is journaled as a `note` on the second.

## Out of scope
- Shared mutable state between branches, blackboards, channels, actors (roadmap M11).
- Transactional merging of concurrent working-copy edits (`docs/08` §5, M11).
- Distributed execution across machines.
- Optimising the tree-walking evaluator.

## Interfaces
```cpp
// src/runtime/include/leash/runtime/sched.hpp
enum class OnFail { CancelOthers, WaitAll };

struct BranchCtx {
    EffectPath path_prefix;               // enclosing path + (construct_site, branch_index)
    std::shared_ptr<asio::cancellation_signal> cancel;  // one signal per branch, owned by the scheduler
    BudgetScope budget;                   // shared parent or per-branch partition
    CapScope caps;
    Env env;                              // immutable snapshot of enclosing bindings
};

class Interpreter {
public:
    Interpreter fork_for_branch(BranchCtx ctx) const;   // shares std::shared_ptr<Journal>, Cas,
                                                        // BudgetAccounting and TraceSink
    asio::awaitable<std::expected<Value, Failure>> eval_parallel(const lir::Parallel& node);  // Tuple
    asio::awaitable<std::expected<Value, Failure>> eval_race(const lir::Race& node);
    asio::awaitable<std::expected<Value, Failure>> eval_spawn(const lir::Spawn& node);       // Opaque(Handle)
    asio::awaitable<std::expected<Value, Failure>> eval_await(const lir::Await& node);
    std::vector<HandleId> outstanding_handles() const;  // used by `complete` (WP-15)
};

struct JoinSlot;                          // scheduler-owned completion slot; see TODO(port) below

struct Handle {                           // held by `Value` as std::shared_ptr<Handle>; non-copyable
    HandleId id;
    std::shared_ptr<JoinSlot> join;
    std::shared_ptr<asio::cancellation_signal> cancel;
    std::atomic<bool> consumed{false};
};
// TODO(port): Asio has no joinable task handle; decide whether `JoinSlot` is
// built on `asio::experimental::promise` or a hand-written slot filled by the
// `co_spawn` completion handler.

// Journal entries (docs/04 §3 kinds): "spawn" (started when the task is created, finished when joined),
// "race" (output: {"winner": usize}), and per-branch effect entries with status "cancelled" for losers.

// std/core.leash signature (WP-21 owns the file; this WP provides the native):
// fn map<T, U>(xs: [T], f: fn(T) -> U ! e, concurrency: Int = 1) -> [U] ! e + {spawn}
```
`EffectCtx` gains `asio::cancellation_signal cancel`; `EffectHandler::perform` must race its operation against it with `asio::experimental::awaitable_operators` (`operator||`). Drivers (WP-05/13/18/19) receive the signal through `EffectCtx`; this WP adds the cancellation conformance test they must pass.

## Deliverables
- `src/runtime/sched.cpp` and `src/runtime/include/leash/runtime/sched.hpp` (parallel, race, map native, spawn/await, cancellation tree, settlement and propagation).
- `src/runtime/budget.cpp`: partitioned child scopes for `parallel(budget:)`, atomic reserve-then-perform charging.
- Journal support for `spawn`/`race` entries and `cancelled` status in `src/runtime/journal.cpp`; `spec/journal.schema.json` updated if fields are missing.
- Checker rules `E0410`/`E0411` in `src/types/check/branches.cpp` (coordinate with the WP-09 owner; land in the same PR or a follow-up PR referenced in `docs/status.md`).
- Tests: `tests/runtime/concurrency_test.cpp` (ordering, race, budgets, propagation, cancellation, replay-with-jitter), `tests/runtime/chaos_concurrent_test.cpp`, checker fixtures `tests/fixtures/check/branch_assign_*.leash`, `tests/fixtures/issues_20.json`.
- Docs: worked branch-path examples for `race` and `spawn` added to `docs/04` §2; `E0410`/`E0411` documented in `docs/02` §10.4; `docs/02` §10.1 lists `limit`, `budget`, `on_fail` together.

## Acceptance criteria
- `leash run examples/03_triage_parallel.leash --driver anthropic=mock --input @tests/fixtures/issues_20.json` completes; `traces/outcome.json` lists 20 results in input order. With mock jitter enabled the run is repeated 10 times and `leash diff <run-i> <run-j>` reports no differing effect ids or outputs for any pair.
- `leash replay <run>` for each of those runs exits 0 and the mock invocation counter stays 0.
- A fixture with `race { slow, fast }`: the journal contains one `race` entry with `winner = 1`; the `slow` branch's `llm.ask` entry has `status: "cancelled"`; replay yields the same value without waiting.
- `parallel(budget: { cost: usd(1) })` with three branches and remaining cost `usd(2)` fails with `Budget` before any branch effect is journaled (the journal has no branch entries).
- `.CancelOthers`: a fixture where branch 2 fails after 50 ms and branch 1 would take 2 s fails in under 500 ms with the branch-2 failure and `data.siblings` containing a `cancelled` marker for branch 1; with `.WaitAll` the run takes ≥ 2 s and reports both.
- `ctest --preset dev -R chaos_concurrent` passes 50 randomised kill/resume iterations; every effect id appears at most once with `status: ok`.
- `leash check tests/fixtures/check/branch_assign_var.leash` prints `E0410` with the offending span; `branch_handle_escape.leash` prints `E0411`.
- Ctrl-C during a concurrent run writes `cancelled` entries for all in-flight effects within the grace period and prints the `leash resume` hint (`docs/10` §4).
- `parallel(limit: 1)` produces exactly the same journal (ids, statuses, outputs) as the unconstrained form on a fixture with three `ask` branches.
- `clang-tidy` over the compile database and `clang-format --dry-run --Werror` clean.

## Notes and pitfalls
- Never key journal lookups by `seq`. Branch results are placed in the tuple by branch index, not by arrival.
- Occurrence counters are per activation: a `spawn` inside a `for` loop gets occurrences 0, 1, 2… within the loop's activation; the branch it spawns starts a fresh counter space under its own prefix.
- Cancellation is cooperative in drivers; the settlement step must time out (`grace + 1 s`), after which the runtime marks the effect `cancelled` even if the handler has not confirmed, and logs a warning.
- The evaluator is mutable state; branches need their own `Interpreter` instances sharing `std::shared_ptr` handles (see `fork_for_branch`).
- `race` must not journal losers' *results* if they complete during the cancellation window; mark them `cancelled` with `note: "completed after loss"` and discard the value so replay is unambiguous.
- Budget charging from concurrent branches must be atomic, and the "would exceed" check must reserve before performing so two branches cannot both pass the check.
- Handles cannot be journaled or cross effect boundaries; they live only in the interpreter.

## Dependencies
Hard: WP-04 (interpreter, journal), WP-12 (budget scopes), WP-16 (failures, checkpoints).  Soft: WP-05 (mock jitter), WP-09 (checker hook for E0410/E0411), WP-13/18/19 (drivers implement cancellation; conformance test provided here).

## Estimated size
L (1–2 weeks).

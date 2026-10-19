# WP-16: Failures, recovery, `state`, checkpoints and `fork`

Milestone: M4 · Size: M · Status: see `docs/status.md`

## Goal
Programs can fail with typed kinds, recover by kind, retry with journaled
backoff, keep typed task state, checkpoint it together with the working copy,
and fork a new run from any checkpoint, including with a modified program.
This closes the "attribution before recovery" loop and makes the journal a
tool for experimentation, not only for audit.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/02-language-spec.md` §4.7 (checkpoints), §9 (failures), §10.4
- `docs/04-determinism-and-replay.md` §3–§7 (journal, replay modes, fork, snapshots), §9 (manifest)
- `docs/05-harness-model.md` §1 row 5 (task state), §4 (taxonomy)
- `docs/10-cli-and-tooling.md` §1 (`fork`, `diff`)
- `docs/adr/0002-event-sourced-execution.md`
- WP-04 (interpreter, replay modes), WP-10 (LIR), WP-11 (working-copy snapshots)

## Scope
- `Failure { kind, message, data, site, cause }` as a runtime value and as a
  `Control::Fail` alternative in the interpreter's control channel; `fail
  Kind(...)`, `fail Kind(msg, data)`, `fail expr` (re-raise, keeps `site`).
- Constructors for every runtime-raised kind (docs/02 §9.6):
  `Failure::budget`, `::permission`, `::tool`, `::model`, `::context`,
  `::verify`, `::replay`, `::feedback`, `::recovery`, `::entropy`, `::unknown`;
  every runtime error path uses them (audit `leash_runtime` and
  `leash_effects` for stray `Failure { kind: Unknown }` constructions and for
  exceptions reaching programs).
- `attempt { } recover Kind as e { } … recover _ as e { }`: arms tried in
  order; unmatched failures propagate; a recover arm that fails sets
  `cause` to the original.
- `retry(n, on: Kind | [Kind], backoff: .None | .Fixed(d) | .Exponential(d)) { }`
  with defaults `on: [Tool, Feedback]`, `backoff: .None`; each attempt is an
  activation with its own occurrence index (docs/04 §2); backoff sleeps are
  `clock` effects journaled with requested and actual durations; replay
  never sleeps; the final failure carries the previous one as `cause`.
- `state { ... }` in flows: typed mutable record `state` in the flow frame;
  assignment through `state.field = ...`; forbidden inside `parallel`/`map`
  branches (WP-09 diagnoses; the runtime guards).
- `checkpoint "name"`: journal entry `{ kind: "checkpoint", name, state,
  budget_consumed, snapshot_ref }`; runtime error `Unknown("duplicate
  checkpoint <name>")` on reuse; requests a working-copy snapshot from WP-11
  (`refs/leash/<run>/<seq>`); writes `artifacts/task_state.json` (latest) and
  keeps every snapshot in the journal entry.
- Journal `fail` and `complete` entries at run end (already in WP-04/WP-15;
  ensure `cause` chains serialise).
- `leash fork <run> --at <checkpoint> [--file <program>] [--input …]`: create a
  new run dir with `manifest.parent_run`, `forked_at` (name and seq); copy the
  journal entries up to and including the checkpoint's `seq`; restore the
  working copy from the snapshot; replay to the checkpoint (must not diverge;
  divergence → `Replay` failure naming the site and the input diff); then
  continue live with the given program (its `lir_hash` may differ).
- `leash diff <run> --from <cp> --to <cp|HEAD>`: working-copy diff between
  snapshots; `leash diff <run-a> <run-b>`: journal diff aligned by effect id
  (first divergence highlighted).
- `leash resume` uses the same restore path when the run has a snapshot
  newer than the last completed effect.

## Out of scope
- `parallel`/`race`/`map` failure propagation (WP-20).
- Entropy classification of failures (WP-21).
- Interactive "pick a checkpoint" UX.

## Interfaces
```cpp
// src/spec/include/leash/spec/failure_kind.hpp
enum class FailureKind { context, tool, feedback, verify, recovery, entropy,
                         model, budget, permission, replay, unknown };

// src/runtime/include/leash/runtime/failure.hpp
struct Failure {
    FailureKind kind;
    std::string message;
    std::optional<nlohmann::json> data;
    Site site;
    std::unique_ptr<Failure> cause;     // null when there is no cause
};

// src/runtime/include/leash/runtime/control.hpp
struct Control {
    struct Fail     { Failure failure; };
    struct Return   { Value value; };
    struct Break    { std::optional<Value> value; };
    struct Continue {};
    struct Complete { Value value; };

    std::variant<Fail, Return, Break, Continue, Complete> value;
};

// src/runtime/include/leash/runtime/checkpoint.hpp
struct CheckpointEntry {
    std::string name;
    std::uint64_t seq = 0;
    nlohmann::json state;
    BudgetTotals budget_consumed;
    std::optional<std::string> snapshot_ref;
};

class Snapshotter {
public:
    virtual ~Snapshotter() = default;

    // WP-11
    virtual asio::awaitable<std::expected<std::string, Failure>>
    snapshot(const RunId& run, std::uint64_t seq) = 0;
    virtual asio::awaitable<std::expected<void, Failure>>
    restore(const RunId& run, std::string_view snapshot_ref) = 0;
};

// src/runtime/include/leash/runtime/run_mode.hpp     (WP-04 extends)
struct RunMode {
    struct Live {};
    struct Replay { std::optional<std::uint64_t> until; };
    struct Resume {};
    struct Fork   { std::uint64_t at_seq = 0; };

    std::variant<Live, Replay, Resume, Fork> value;
};
```
CLI: `leash fork`, `leash diff` as in docs/10 §1. Manifest fields `parent_run`,
`forked_at: { name, seq }`.

`Failure` is the canonical shape of `docs/subagents/shared-interfaces.md`; that
file wins if this block drifts. Each variant above gets one `std::visit`
visitor in its own header and one unit test that visits every alternative.

## Deliverables
- `src/runtime/{failure.cpp, recover.cpp, retry.cpp, state.cpp, checkpoint.cpp, fork.cpp}` with headers under `src/runtime/include/leash/runtime/`
- `src/cli/cmd/{fork.cpp, diff.cpp}`
- Tests: unit tests for arm ordering, re-raise and cause chains, retry counts
  and backoff journal entries, duplicate checkpoint error; integration
  `tests/integration/fork_test.cpp` (see criteria).
- Docs: `docs/04-determinism-and-replay.md` §4 updated if the fork rules change.

## Acceptance criteria
- `ctest --preset dev -L 'runtime|cli'` passes.
- Program A: `checkpoint "cp"` then an `ask` then `complete`. Run live with
  the mock driver. `leash fork <run> --at cp --file B.leash` where B changes
  only code after the checkpoint: the new run completes, its journal contains
  the copied prefix plus new entries, and `manifest.parent_run` is set.
- Same, but B changes code before the checkpoint (an extra `ask`): fork exits
  3 with `Replay` naming the site of the first divergent effect.
- `retry(3, on: Tool, backoff: .Exponential(100ms))` around a mock tool that
  fails twice: three `tool` entries, two `clock` entries with requested
  100ms/200ms, success; `leash replay` finishes in well under 300ms (no sleep).
- `attempt { fail Verify("x") } recover Tool as e { … } recover Verify as e { e.message }`
  yields `"x"`; `recover _` catches `Budget` raised by the runtime.
- `leash diff <run> --from cp --to HEAD` prints the working-copy diff made
  after the checkpoint.

## Notes and pitfalls
- `fail` has type `Never`; the checker (WP-08) handles typing, the runtime
  must still treat `Control::Fail` as unwinding through every construct,
  including `parallel` (WP-20 will refine).
- Journal the `clock` sleep *before* sleeping with `status: started` so a
  crash mid-backoff resumes correctly.
- Fork copies journal lines verbatim; do not renumber `seq`. New entries
  continue from the checkpoint's `seq + 1` and any later original entries are
  simply absent from the new run.
- Snapshots exclude `.leash/` and `harness/` (WP-17 renders the latter).
- `state` snapshots are JSON; closures and handles cannot be stored in
  `state` (type error in WP-08; runtime guard here).

## Dependencies
Hard: WP-04, WP-10, WP-11. Soft: WP-15 (`complete` entry), WP-09 (diagnostics for `state` in branches).

## Estimated size
M (2–4 days).

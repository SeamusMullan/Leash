# WP-25: Test infrastructure: `leash test`, golden journals, chaos and property tests

Milestone: M8 · Size: M · Status: see `docs/status.md`

## Goal
Leash programs and the Leash runtime both become testable without spending money or touching the network. `leash test` runs the `test` declarations in a package against the mock driver and then replays each one to prove determinism; golden journals in the repository catch accidental semantic changes to the interpreter; chaos and property tests attack the guarantees in `docs/04` §10 directly; and a `leash_testkit` module gives every other work package the same fixtures, mock builders and journal assertions instead of each inventing its own.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type; `leash_testkit` is owned here)
- `docs/04-determinism-and-replay.md` §2 (effect identity), §4 (replay algorithm), §8 (`--reuse`), §10 (the four test kinds this WP implements)
- `docs/10-cli-and-tooling.md` §6 (`leash test`, golden layout), §1 (`test` row), §3 (run directory)
- `docs/02-language-spec.md` §1 (items include `test`), §2.3 (`test` is contextual), and `spec/grammar.ebnf` (`TestDecl`)
- `docs/subagents/WP-05-drivers-mock-anthropic-ask.md` (mock script format; extended here)
- `docs/subagents/WP-20-scheduler-concurrency.md` (mock jitter, concurrent chaos test)
- `CLAUDE.md` (conventions; golden updates land in their own commit)

## Scope
- `leash test [paths] [--reuse] [--update-golden] [--filter <substring>] [--jobs N] [--json]`, implemented in `src/cli/cmd/test.cpp` over a `leash_testkit` runner:
  - Discovers `test "name" with { … } { … }` items (`spec/grammar.ebnf` `TestDecl`) in the package and in `std/` when `--include-std` is passed.
  - The `with` record overrides configuration for that test only: `drivers`, `profile`, `budget`, `allow`, `input`. Unset drivers default to `drivers.mock` with an empty script, so a test that performs an unscripted `ask` fails with a clear `MockUnmatched` message rather than reaching the network. A test whose `with` names a non-mock driver is skipped unless `--allow-live` is passed, and the skip is reported.
  - Each test runs in a temporary run directory (`.leash/test-runs/<test>/`, removed on success, kept on failure and named in the output), with the journal written to disk so the replay pass can consume it.
  - After a test body succeeds, the runner replays the same journal and asserts the outcome, evidence set and derived traces are identical; a divergence is reported as a test failure with the site and input diff from `docs/04` §6.
  - Output: one line per test (`ok`/`FAILED`/`skipped`, duration, effect count), a summary, exit code 0 or 1; `--json` emits one object per test for CI.
- `assert` support. `docs/02` §12 now lists `assert(cond: Bool, message: String = "")` and `assert_eq<T>(a: T, b: T)` under `std.core`; implement them here and coordinate with the WP-21 owner so the std source matches. A failed assertion raises `Failure { kind: Unknown, message, data: {"assert": true, "left": …, "right": …} }`; the runner renders it as an assertion failure, not a crash.
- Mock script extensions (same file format as WP-05, `version: 1`): `latency_ms` accepts a number or `{"jitter": [lo, hi]}` sampled from the run seed so jitter is reproducible; `usage` may be omitted and defaults to zero; a top-level `"strict": true` makes any unmatched prompt a hard error (the default for `leash test`).
- `--reuse` per `docs/04` §8: `leash test --reuse` serves reusable effects (`ask`, `fs.read`, or any call site with `reuse: true`) from `.leash/test-cache/` keyed by `(kind, input_hash)`, and writes new results back. Entries carry `reused_from`. `--reuse` never applies to `shell`, `test`, `run` or `human` effects.
- Golden journals under `tests/golden/<name>/{program.leash, input.json, journal.jsonl, expected_outcome.json}` (`docs/10` §6). `leash test --golden` replays every golden against its checked-in program and compares the outcome and derived traces; `--update-golden` rewrites them, which sets `UPDATE_GOLDEN=1` for the `leash_testkit` golden helper. CI runs without `--update-golden`, so a semantic change to the interpreter fails the build until the author regenerates goldens deliberately in a separate commit.
- Chaos tests (`tests/runtime/chaos_test.cpp` plus a `leash_testkit` driver): run a mock program to completion to get the reference outcome, then repeat N times killing the process (SIGKILL) at a uniformly random point between effect boundaries, `leash resume`, and assert the final outcome, evidence and patch equal the reference and that no effect id has two `ok` entries. Covers a sequential program and, with WP-20 present, a concurrent one.
- Property tests (a maintained property-test library; RapidCheck is not in the vcpkg manifest because its port fails on current libc++, so choose a patched port or an alternative and add it to `vcpkg.json` and the licence table): (a) shuffling the lines of a journal does not change replay results (`docs/04` §10); (b) `leash fmt` is idempotent and preserves the parse tree on every file in `examples/`, `std/` and `tests/fixtures/`; (c) site ids are stable under reformatting and comment insertion (reformat, re-lower, compare `SiteId` sets from WP-10).
- Determinism CI job `determinism`: for every file in `examples/`, run live against the mock driver, then `leash replay`, then diff the derived traces; fail on any difference or on a non-zero mock call count during replay.
- Coverage: a `coverage` CMake preset with `-fprofile-instr-generate -fcoverage-mapping`, and a `coverage` CI job that runs `ctest --preset coverage`, merges the profiles with `llvm-profdata merge`, exports `llvm-cov export -format=lcov` to `lcov.info`, and uploads the report as an artifact. No enforced threshold in v1; the job records the number so regressions are visible.
- `leash_testkit` (linked by every other module's test targets): temp git repositories, scripted mock builders, journal assertions, and the fixture directory conventions used repository-wide.

## Out of scope
- Unit tests for other modules' logic; each WP owns its own tests. This WP owns the shared harness they call.
- Live API or CLI integration tests (those stay behind the CTest `network` label in their own WPs and run only with `LEASH_TEST_NETWORK=1`).
- Benchmarking or performance regression tracking beyond the replay budget in `docs/03` §12.
- Evaluation suites and metrics (WP-26), which use this module but are a separate deliverable.
- Mutation testing, fuzzing beyond WP-07's parser fuzz target.

## Interfaces
```cpp
// src/testkit/include/leash/testkit/temp_repo.hpp
namespace leash::testkit {

class TempRepo {                                  // move-only; owns a TempDir, so the tree dies with the fixture
public:
    static TempRepo from_fixture(std::string_view name);  // copies tests/fixtures/<name>, `git init`, one commit

    std::filesystem::path path;
    std::string head;

    TempRepo& write(std::string_view rel, std::string_view contents);
    std::string commit(std::string_view msg);
    std::string diff() const;

private:
    TempDir dir_;
};

class MockScriptBuilder {
public:
    MockScriptBuilder() = default;

    MockScriptBuilder& on_seq(std::uint32_t n, MockResponse resp);
    MockScriptBuilder& on_prompt_contains(std::string_view needle, MockResponse resp);
    MockScriptBuilder& fail_once(MockFailure kind);
    MockScriptBuilder& jitter(std::uint64_t lo_ms, std::uint64_t hi_ms);
    MockScriptBuilder& strict(bool yes);
    std::filesystem::path write(const std::filesystem::path& dir) const;  // JSON file consumable by MockDriver::from_script
};

struct RunFixture {
    std::filesystem::path run_dir;
    Journal journal;
    OutcomeRecord outcome;
};

asio::awaitable<RunFixture> run_program(const std::filesystem::path& program,
                                        nlohmann::json input, RunOpts opts);
asio::awaitable<std::expected<RunFixture, Failure>> replay(const std::filesystem::path& run_dir);

// Journal assertions — all take const Journal& and fail the Catch2 case with a readable diff.
void assert_effect_order(const Journal& j, std::span<const std::string_view> kinds);  // by seq, ignoring "started" lines
void assert_no_divergence(const RunFixture& live, const RunFixture& replayed);
void assert_outcome(const Journal& j, std::string_view label);
void assert_effect_ids_eq(const Journal& a, const Journal& b);                        // set equality, order-insensitive
void assert_no_network(const RunFixture& f);                                          // mock call count == 0 during replay
void assert_evidence(const RunFixture& f, std::string_view check, std::string_view result);

}  // namespace leash::testkit
```
Fixture layout, normative for the repository: `tests/fixtures/<name>/` for repository fixtures (WP-15's `repoA` included), `tests/fixtures/mock/*.json` for scripts, `tests/fixtures/parse/*.leash` and `tests/fixtures/check/*.leash` for front-end cases, `tests/golden/<name>/` for golden journals, `tests/cli/` for CLI transcript cases (WP-22).

`leash test --json` object per test: `{"name", "file", "line", "status": "ok|failed|skipped", "duration_ms", "effects", "replayed": true, "failure": {"kind", "message", "site"} | null}`.

TODO(port): `trycmd` has no C++ equivalent; `tests/cli/` assumes WP-22's golden-file harness (`expect_golden`, `UPDATE_GOLDEN=1`) plus a helper that runs the `leash` binary with a command line and captures stdout, stderr and the exit code. Agree the helper's name and location with the WP-22 owner instead of adding a second harness here.

## Deliverables
- `src/testkit/` (new module; add it to the root `CMakeLists.txt` and to `docs/03-runtime-architecture.md` §2 and `docs/subagents/shared-interfaces.md` if the row is missing).
- `src/cli/cmd/test.cpp` and the runner in `src/testkit/runner.cpp` (`leash::testkit::runner`).
- `std/core.leash` additions for `assert`/`assert_eq` plus their natives (the spec entry in `docs/02` §12 already exists).
- Mock script extensions in `src/drivers/mock.cpp` (jitter, `strict`), with WP-05's format documentation updated in place.
- `tests/runtime/{chaos_test.cpp, properties_test.cpp}`; `tests/golden/` with at least three goldens (`01_hello`, `02_fix_bug` on `repoA`, `03_triage_parallel`).
- `.github/workflows/ci.yml`: `determinism` and `coverage` jobs; `CMakePresets.json` gains the `coverage` configure, build and test presets.
- `docs/10-cli-and-tooling.md` §6 expanded with the flags and the `.leash/test-cache` location.

## Acceptance criteria
- `leash test` in a package containing the `docs/10` §6 example passes, prints one `ok` line, and its `--json` output matches the shape above.
- A test whose `ask` is unscripted fails with `MockUnmatched` naming the prompt excerpt, and the temporary run directory is retained and printed.
- `leash test --golden` passes on `main`; changing a constant in the interpreter that alters an effect's input hash makes it fail and name the first differing effect id; `leash test --golden --update-golden` then makes it pass again and touches only files under `tests/golden/`.
- `ctest --preset dev -R chaos` passes 50 kill/resume iterations on a sequential program and 50 on `examples/03_triage_parallel.leash`; every asserted outcome equals the uninterrupted reference.
- `ctest --preset dev -R properties` passes: 200 shuffled-journal cases replay identically; `leash fmt` is idempotent over all `.leash` files in the repository; site ids are unchanged after reformatting each example.
- The `determinism` CI job runs all five examples live with the mock driver and replays each; `assert_no_network` holds for every replay (mock invocation counter 0) and trace diffs are empty.
- `leash test --reuse` run twice on the same package performs zero `llm` effects the second time (assert via the journal's `reused_from` fields) and produces the same outcome.
- `ctest --preset coverage` followed by `llvm-profdata merge` and `llvm-cov export -format=lcov` produces `lcov.info` in CI and uploads the artifact.
- `run-clang-tidy -p build/dev -warnings-as-errors '*'` and `clang-format --dry-run --Werror` clean.

## Notes and pitfalls
- The replay pass inside `leash test` must construct the runtime in replay mode with no drivers registered at all, so a missing journal entry is impossible to paper over with a live call.
- Chaos tests need the journal's started/finished two-line protocol (`docs/04` §3) to be fsynced before the effect returns; if a kill between the two lines makes `resume` misbehave, that is a runtime bug to file, not a test to weaken.
- Kill the process, not the coroutine: spawn the runner as a subprocess in chaos tests so the fsync semantics are real.
- `--update-golden` must never run in CI. Guard it with an environment check and fail loudly if `CI=true`.
- Property test (c) depends on WP-10's site-id derivation; if WP-10 has not landed, implement (a) and (b) and leave (c) as a CTest case carrying the `DISABLED` property with a note in `docs/status.md`.
- Keep `leash_testkit` free of test-only compilation guards (`#ifdef` around test helpers): other modules link it as an ordinary library in their test targets.
- Temporary directories must be removed on success even when a test aborts; use the `TempDir` guard type rather than manual cleanup.
- Jitter must be seeded from the run seed (`manifest.seed`, `docs/04` §9) so a failing randomised case is reproducible from the run directory alone.

## Dependencies
Hard: WP-05 (mock driver and script format), WP-16 (failures, checkpoints, resume), WP-20 (concurrency for the concurrent chaos case).  Soft: WP-10 (site-id stability property), WP-15 (`repoA` fixture and outcome records), WP-21 (`assert` lands in `std.core`), WP-22 (CLI argument parsing for `leash test`).

## Estimated size
M (2–4 days).

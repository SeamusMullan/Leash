# WP-26: `leash eval` — harness ladder, metrics and benchmark suites

Milestone: M9 · Size: L · Status: see `docs/status.md`

## Goal
`leash eval` answers the question the whole project exists to make answerable: does this harness change help, and which runtime support was missing when it did not? It runs a suite of tasks across the cells of `(task × flow × profile × driver)`, produces one episode package per episode, adjudicates each with evaluator checks the agent never sees, computes the metrics of `docs/11` §3 with confidence intervals, and emits `metrics.csv`, `metrics.json` and a markdown summary. A ladder suite of constructed fixture repositories ships with the repository so the numbers can be reproduced offline with the mock driver.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/11-episode-package-and-eval.md` (all sections; §3 metrics, §4 suite schema, §5 benchmarks, §6 `leash diff`)
- `docs/06-verification-and-evidence.md` §3 (Evidence fields), §6 (outcome labels), §10 (entropy audit)
- `docs/05-harness-model.md` §3 (profiles H0–H3 and the visibility matrix), §4 (failure taxonomy)
- `docs/research/paper-notes.md` §1 (AVSR, M-HIR, the H0–H3 ladder, repoA-T1)
- `docs/subagents/WP-15-tasks-checks-evidence.md` (`repoA` fixture, evaluator `checks.toml`), `WP-17-context-memory-profiles.md` (profile rendering), `WP-25-test-infra.md` (`leash_testkit`)
- `docs/10-cli-and-tooling.md` §1 (`eval` row), §3 (run directory)

## Scope
- `src/eval` with the CLI surface `leash eval <suite.toml> [--profiles h0,h3] [--models …] [--driver name=spec] [--episodes N] [--out <dir>] [--filter <task-id>] [--jobs N] [--seed N] [--json]`. WP-22 owns argument parsing; this WP owns everything behind it.
- Suite parsing exactly as in `docs/11` §4, extended with the fields the runner needs and documented back into `docs/11` §4 in this PR:
  ```toml
  [suite]
  name = "ladder"
  profiles = ["h0", "h1", "h2", "h3"]
  episodes_per_cell = 5
  drivers = ["anthropic:claude-opus-5"]     # cell dimension; `mock:<script-dir>` for offline runs
  hidden_paths = ["evaluator/**"]           # removed from every episode worktree before the run

  [[task]]
  id = "repoA-T1"
  flow = "flows/fix_bug.leash:FixEmptyPassword"
  repo = { git = "fixtures/repoA", rev = "a1b2c3" }
  input = { issue = "…" }
  evaluator_checks = "fixtures/repoA/evaluator/checks.toml"
  budget = { cost = "usd 5", wall = "20m" }  # per episode; overrides the task's own budget downward only
  ```
- Cell expansion and execution: one episode per `(task, flow, profile, driver, episode_index)`; episodes run concurrently up to `--jobs` (default: physical cores, capped at 4 for live drivers); each gets its own worktree and run directory under `<out>/runs/`. The suite seed plus the cell coordinates derive each episode's `seed` so a single episode can be re-run reproducibly.
- Evaluator-check isolation, which is the correctness-critical part of this WP: before an episode starts, the runner creates the worktree at `repo.rev`, deletes every path matching `hidden_paths`, and asserts they are absent. The program's capability set cannot reference them because they do not exist in the worktree. After the episode finishes, the runner restores the evaluator files into a *separate* adjudication sandbox seeded from the episode's final snapshot (`refs/leash/<run>/<seq>`, `docs/04` §7) and executes `checks.toml` there. Adjudication effects are journaled into `<out>/runs/<run_id>/adjudication.jsonl`, never into `journal.jsonl`, so the episode package stays a faithful record of what the agent did.
- Adjudicated versus autonomous labels: the autonomous label comes from `traces/outcome.json` (WP-15); the adjudicated label applies the same five-label rule of `docs/06` §6 to the evaluator checks. Both are reported for every episode, and their disagreement is itself a reported column (`label_agreement`), because "patch correct but unverified" is the case the project cares about.
- Metrics: every row of `docs/11` §3, computed per episode and aggregated per cell — AVSR, M-HIR, verification autonomy, context-trace meaningfulness, tool recovery rate, failure attribution completeness, entropy delta, trajectory efficiency (tool calls, model turns, tokens, cost, wall, edits), verification strength (oracle diversity, judge-only evidence count), recovery ability (retries, `Recovery` failures), state consistency (merge conflicts, reverts, re-verifications), safety compliance (`Permission` events, reverted out-of-scope edits), replayability (replay of each episode succeeds). Aggregation reports mean and, when a cell has ≥ 10 episodes, a bootstrap 95% confidence interval (10 000 resamples, seeded); below that the CI columns are empty rather than misleadingly narrow.
- Outputs under `--out` (default `.leash/eval/<suite>-<timestamp>/`): `metrics.csv` (one row per cell), `episodes.csv` (one row per episode), `metrics.json` (nested, with the suite manifest and per-cell CIs), `summary.md` (markdown tables: cells × AVSR, a per-profile ladder table, and a failure-kind breakdown), and `runs/` containing every episode package.
- Replayability check: after each episode the runner executes `leash replay` on it and records success or the divergence site. A divergence is a failed metric, not a crash.
- Ladder suite `suites/ladder.toml` with three constructed fixture repositories under `tests/fixtures/`:
  1. `repoA` (from WP-15): the empty-password validation defect — stresses the verification protocol.
  2. `repoDeps`: a Node or Python package where the task is to remove an unused dependency and its dead import; an agent that adds dependencies, leaves residue or edits lockfiles unnecessarily scores entropy — stresses the entropy auditor.
  3. `repoFlaky`: a test suite with one order-dependent test; the task is to diagnose and fix the flake without weakening assertions; an agent that adds `it.skip` or deletes the assertion is `unsafe_invalid` — stresses attribution and recovery.
  Each fixture ships `evaluator/checks.toml` plus a mock driver script directory so the whole suite runs offline.
- Optional SWE-bench Lite adapter behind the CMake option `LEASH_ENABLE_SWEBENCH` (`src/eval/adapters/swebench.cpp`, guarded by `#ifdef LEASH_ENABLE_SWEBENCH`): reads a locally downloaded instances file (never downloads implicitly), converts each instance into a task whose `ensure` block holds `FAIL_TO_PASS` and `PASS_TO_PASS` as `test` checks and whose repo/rev come from the instance, and emits a generated suite TOML. Documented as post-v1 in `docs/11` §5; the option is off by default and the adapter is excluded from the default CI matrix.

## Out of scope
- Harness evolution (`leash evolve`, change contracts): roadmap M10.
- Long-horizon suites that carry repository state across episodes (`docs/11` §5): post-v1.
- Statistical hypothesis testing beyond bootstrap intervals; no p-values in v1.
- A web dashboard. `summary.md` and the CSVs are the interface.
- New metrics not listed in `docs/11` §3. Propose additions there first.

## Interfaces
```cpp
// src/eval/include/leash/eval/suite.hpp
namespace leash::eval {

struct Suite {
    std::string name;
    std::vector<std::string> profiles;
    std::uint32_t episodes_per_cell = 0;
    std::vector<DriverSpec> drivers;
    std::vector<std::string> hidden_paths;
    std::vector<TaskSpec> tasks;
};

struct TaskSpec {
    std::string id;
    FlowRef flow;
    RepoRef repo;
    nlohmann::json input;
    std::optional<std::filesystem::path> evaluator_checks;
    std::optional<Budget> budget;
};

struct Cell {                                  // std::map key: members are ordered
    std::string task;
    FlowRef flow;
    std::string profile;
    DriverSpec driver;
    auto operator<=>(const Cell&) const = default;
};

struct EpisodeResult {
    Cell cell;
    std::uint32_t index = 0;
    std::string run_id;
    std::filesystem::path run_dir;
    Label autonomous_label;
    Label adjudicated_label;
    bool agreement = false;
    EpisodeMetrics metrics;
    bool replay_ok = false;
    std::optional<Failure> failure;
};

struct EpisodeMetrics { /* one field per docs/11 §3 row, all std::optional<double> or counts */ };

struct CellMetrics {
    EpisodeMetrics mean;
    std::optional<std::pair<EpisodeMetrics, EpisodeMetrics>> ci;
    std::uint32_t n = 0;
};

asio::awaitable<std::expected<EvalReport, EvalError>> run_suite(const Suite& suite, const EvalOpts& opts);
std::map<Cell, CellMetrics> aggregate(const std::vector<EpisodeResult>& results);
std::optional<std::pair<double, double>> bootstrap_ci(std::span<const double> values,
                                                      std::size_t resamples, std::uint64_t seed);  // nullopt when values.size() < 10
std::string render_summary(const EvalReport& report);                                              // markdown
std::expected<void, Failure> write_csv(const EvalReport& report, const std::filesystem::path& dir);

// Adjudication runs in its own sandbox and never touches the episode journal.
asio::awaitable<AdjudicationResult> adjudicate(const std::filesystem::path& run_dir,
                                               const std::filesystem::path& checks,
                                               RunWorkspace& sandbox);

}  // namespace leash::eval
```
`metrics.csv` columns, in order: `suite,task,flow,profile,driver,model,episodes,avsr,avsr_ci_low,avsr_ci_high,m_hir,verification_autonomy,context_meaningfulness,tool_recovery_rate,attribution_completeness,entropy_delta,tool_calls,model_turns,tokens,cost_usd,wall_ms,edits,oracle_diversity,judge_only_evidence,retries,recovery_failures,permission_events,reverted_edits,replay_ok_rate,label_agreement_rate`. `episodes.csv` uses the same metric columns plus `run_id,episode_index,autonomous_label,adjudicated_label,failure_kind`.

## Deliverables
- `src/eval/CMakeLists.txt`, the umbrella header `src/eval/include/leash/eval/eval.hpp` and `src/eval/{suite.cpp, runner.cpp, adjudicate.cpp, metrics.cpp, bootstrap.cpp, report.cpp, adapters/swebench.cpp}`.
- `src/cli/cmd/eval.cpp` (thin delegation; flags owned by WP-22).
- `suites/ladder.toml`; fixtures `tests/fixtures/{repoA,repoDeps,repoFlaky}/` each with `evaluator/checks.toml`, `input.json` and `mock/` scripts (`repoA` extends WP-15's fixture rather than duplicating it).
- `.github/workflows/ci.yml`: an `eval-ladder` job running the offline ladder.
- `docs/11-episode-package-and-eval.md` §4 updated with the full suite schema; §3 annotated with the exact CSV column name for each metric.
- Tests: `tests/eval/{suite_parse_test.cpp, adjudication_isolation_test.cpp, metrics_test.cpp, ladder_test.cpp}`.

## Acceptance criteria
- `leash eval suites/ladder.toml --driver anthropic=mock --episodes 2 --out /tmp/eval` completes h0–h3 × 3 tasks × 2 episodes (24 episodes) in under 2 minutes on CI hardware and exits 0.
- `head -1 /tmp/eval/metrics.csv` matches the documented column list exactly, and the file has 12 data rows (3 tasks × 4 profiles for the single mock driver).
- `jq '.cells[] | select(.profile=="h3") | .mean.verification_autonomy' /tmp/eval/metrics.json` is 1.0 for `repoA-T1`, and strictly greater than the `h0` value for the same task, demonstrating the ladder produces the expected evidence gradient.
- Adjudication isolation: `ctest --preset dev -R adjudication_isolation` asserts that (a) no episode worktree contains any path matching `hidden_paths` at any point during the run, (b) no journal entry in `journal.jsonl` references an evaluator path, and (c) `adjudication.jsonl` exists with one entry per evaluator check.
- A deliberately over-claiming mock script for `repoA-T1` (declares success without passing probes) yields `autonomous_label = unverified_success` or `failed`, `adjudicated_label = failed`, and `label_agreement = false` in `episodes.csv`.
- A mock script for `repoFlaky` that adds `it.skip` yields `unsafe_invalid` in both labels and a non-zero `entropy_delta`.
- `bootstrap_ci` returns `nullopt` for 9 values and a stable interval for 10 identical seeds (unit test); with `episodes_per_cell = 10` the CI columns in `metrics.csv` are populated.
- Every episode's `replay_ok` is true in the ladder run; `replay_ok_rate` is 1.0 in `metrics.csv`.
- `cmake --preset dev -DLEASH_ENABLE_SWEBENCH=ON && cmake --build build/dev --target leash_eval` compiles, and `leash eval --adapter swebench <instances.jsonl> --emit-suite out.toml` produces a suite whose `ensure` blocks contain the instance's `FAIL_TO_PASS` tests (unit test with two recorded instances; no network).
- `cat /tmp/eval/summary.md` renders a cells × AVSR table and a failure-kind breakdown; the file is valid markdown (checked by the link/table lint used in WP-24).
- `run-clang-tidy -p build/dev -warnings-as-errors '*'` and `clang-format --dry-run --Werror` clean.

## Notes and pitfalls
- The isolation property is the one thing this WP must not get wrong: an evaluator check leaking into a worktree silently invalidates every number the suite produces. Assert absence at worktree creation, and again after the episode, before adjudication restores the files.
- Profiles change only what the model *sees* (`docs/05` §3); enforcement is always on. Do not weaken capabilities at h0 to "make it fair" — the ladder measures exposed support, not permissions.
- Adjudication must run against the episode's final snapshot, not the live worktree, so a crashed or cancelled episode is still adjudicable.
- Cost control for live drivers: apply the suite's per-episode budget, refuse to start a live run whose worst-case cost (cells × episodes × budget) exceeds `--max-cost` (default `usd(50)`), and print the estimate before starting.
- Bootstrap resampling must be seeded from the suite seed so re-running `aggregate` on the same episodes yields identical intervals.
- Metrics that cannot be computed for an episode (no tool failures, so no recovery rate) are `nullopt`/empty, never 0.0; aggregation ignores them and reports `n` per metric.
- Concurrency across episodes must not share worktrees or run directories; reuse `leash::testkit::TempRepo` conventions rather than inventing new ones.
- The SWE-bench adapter must not download anything implicitly, and its instances file path is a required argument; note the dataset licence in the adapter's file header comment and in `docs/11` §5.

## Dependencies
Hard: WP-15 (checks, evidence, outcome labels, `repoA`), WP-17 (profiles and rendering), WP-22 (CLI surface).  Soft: WP-11 (snapshots for adjudication), WP-16 (fork/resume semantics), WP-20 (concurrent episode execution), WP-21 (entropy audit feeding `entropy_delta`), WP-25 (`leash_testkit`, golden/chaos conventions).

## Estimated size
L (1–2 weeks).

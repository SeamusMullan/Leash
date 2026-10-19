# WP-06: Run directory, manifest, CAS, trace layer v0, report skeleton

Milestone: M1 · Size: M · Status: see `docs/status.md`

## Goal
Give every run a well-formed episode package directory: manifest, journal,
content-addressed blobs, a first derived trace (action), an outcome record and
a report skeleton, plus the CLI commands to list runs and inspect them. Later
WPs add the other seven traces and the real outcome rules on top of this
layer; the derivation-from-journal principle (docs/03 §10) is established here.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/10-cli-and-tooling.md` §3 (run directory), §1 (`trace`, `report`, `runs`, `gc`)
- `docs/04-determinism-and-replay.md` §3 (CAS rule), §9 (manifest)
- `docs/11-episode-package-and-eval.md` §1 (package contents), §2
- `docs/06-verification-and-evidence.md` §6 (labels; v0 uses two of them), §7 (report shape)
- `docs/03-runtime-architecture.md` §7, §10
- `spec/manifest.schema.json`, `spec/traces/action.schema.json`, `spec/traces/outcome.schema.json` (WP-02)

## Scope
- New module `src/store` (`leash_store`, no dependency on `leash_runtime`):
  - `cas.hpp`/`cas.cpp`: `Cas::open(root)`, `put(bytes) -> Hash` (blake3 through
    the `src/spec` wrapper, stored at `<root>/<first two hex chars>/<hex>`,
    write-to-temp-then-rename, no overwrite), `get(&Hash) ->
    std::optional<std::vector<std::byte>>`, `contains`, `iter()`; hashes render
    as `b3:<hex>`.
  - `rundir.hpp`/`rundir.cpp`: `RunDir::create(runs_root, flow_name, now)` with
    id `<UTC yyyy-mm-ddTHH-MM-SS>_<flow>_<6 hex>`; paths for `manifest.json`,
    `journal.jsonl`, `traces/`, `artifacts/`, `worktree/`; `RunDir::open(id_or_path)`
    resolving ids under `.leash/runs` and absolute/relative paths; `list(runs_root)`
    sorted by creation time; `remove`.
  - `manifest.hpp`/`manifest.cpp`: `Manifest` struct mirroring docs/04 §9, with
    free `to_json`/`from_json` functions for nlohmann/json; `write_manifest`,
    `read_manifest`, validation against the schema in `leash::spec::manifest`.
  - Runtime-side adapter in `src/runtime/payload_store.cpp`: `CasPayloadStore`
    implementing the runtime's `PayloadStore` interface over `leash::store::Cas`
    (runtime depends on store; the store never includes a runtime header).
- `src/trace` (`leash_trace`):
  - `journal_reader.hpp`/`.cpp`: streaming reader of `journal.jsonl` into
    `nlohmann::json` entries, validating each line against `leash::spec::journal`,
    resolving CAS references lazily.
  - `action.hpp`/`.cpp`: derive `traces/action.jsonl` from the journal: one line
    per effect with `{seq, at, op, site, summary, effect_id, status}` where `op ∈
    {ask, run, read_file, edit_file, run_tool, verify, checkpoint, gate,
    complete, fail}` (map journal kinds to ops; unknown kinds → `op: "effect"`).
  - `outcome.hpp`/`.cpp`: v0 rules: `complete` entry present with `status: ok` →
    `unverified_success` (no evidence machinery yet; WP-15 upgrades to
    `autonomous_verified_success`); a terminal `fail` → `failed`; otherwise
    `incomplete` (a sixth, provisional label allowed only until WP-15 —
    document it in the schema as provisional). Includes budget totals summed
    from `usage`.
  - `report.hpp`/`.cpp`: `report.md` skeleton per docs/06 §7 with task/flow/outcome
    header, an empty requirements table with a note "no checks declared" in
    v0, budget totals, interventions and entropy sections stating "not
    collected (v0)".
  - `derive.hpp`/`.cpp`: `derive_all(rundir) -> std::expected<DeriveReport,
    TraceError>` regenerating every derived file; the output must be
    byte-identical for the same journal (deterministic serialisation, sorted
    keys, fixed timestamp formatting).
- `leash_runtime` integration: `Runner` takes a `RunDir` and a `PayloadStore`;
  writes `manifest.json` at start (`mode`, program hash = blake3 of the source
  bytes until WP-10 provides `lir_hash`, inputs hash, seed, drivers from
  `Driver::id()`), and calls `derive_all` at the end of `run`/`resume`.
- `leash_cli`: `trace <run> --kind action [--json]` (table by default),
  `report <run> [--regenerate]`, `runs list`, `runs show <id>`, `runs rm <id>`,
  `runs migrate <id>` (checks `journal_version`; v1 → "nothing to migrate";
  unknown → error), `gc --keep-days N` (removes run dirs older than N days and
  CAS objects unreferenced by any remaining journal).
- Journal format versioning: `manifest.journal_version` written by the runtime;
  `leash replay`/`resume` refuse mismatches with a message pointing to `runs migrate`.

## Out of scope
The other seven traces (WP-15/16/17 add them), the real outcome classifier
(WP-15), worktree snapshots (WP-11), `leash diff` (WP-22), artifacts saving
API (`artifacts.save`, WP-21), metrics (WP-26).

## Interfaces
```cpp
// src/store/include/leash/store/cas.hpp
namespace leash::store {

struct Hash {
    std::array<std::byte, 32> bytes{};
    std::string display() const;              // "b3:<hex>"
};

struct StoreError {
    FailureKind kind;
    std::string message;
};

class Cas {
public:
    static std::expected<Cas, StoreError> open(std::filesystem::path root);
    std::expected<Hash, StoreError> put(std::span<const std::byte> bytes) const;
    std::expected<std::optional<std::vector<std::byte>>, StoreError> get(const Hash& h) const;
    bool contains(const Hash& h) const;
    std::generator<Hash> iter() const;
};

// src/store/include/leash/store/rundir.hpp
struct RunSummary {
    std::string id;
    std::string flow;
    std::chrono::system_clock::time_point created_at;
    std::optional<std::string> outcome;
    std::optional<std::string> cost_usd;
};

struct RunDir {
    std::string id;
    std::filesystem::path root;

    static std::expected<RunDir, StoreError> create(const std::filesystem::path& runs_root,
                                                    std::string_view flow,
                                                    std::chrono::system_clock::time_point now);
    static std::expected<RunDir, StoreError> open(const std::filesystem::path& runs_root,
                                                  std::string_view id_or_path);
    static std::expected<std::vector<RunSummary>, StoreError> list(
        const std::filesystem::path& runs_root);

    std::filesystem::path manifest() const;
    std::filesystem::path journal() const;
    std::filesystem::path traces() const;
    std::filesystem::path artifacts() const;
};

// src/store/include/leash/store/manifest.hpp
struct Manifest { /* fields of docs/04 §9 */ };

nlohmann::json to_json(const Manifest& m);
std::expected<Manifest, StoreError> from_json(const nlohmann::json& j);
std::expected<void, StoreError> write_manifest(const RunDir& run, const Manifest& m);
std::expected<Manifest, StoreError> read_manifest(const RunDir& run);   // validates via leash::spec

}  // namespace leash::store

// src/runtime/include/leash/runtime/payload_store.hpp
namespace leash::runtime {

class PayloadStore {                          // abstract; owned by leash_runtime
public:
    virtual ~PayloadStore() = default;

    virtual std::expected<PayloadRef, Failure> put(const nlohmann::json& value) = 0;
    virtual std::expected<nlohmann::json, Failure> get(const PayloadRef& ref) const = 0;
};

class CasPayloadStore final : public PayloadStore {
public:
    explicit CasPayloadStore(store::Cas cas);  // maps StoreError to Failure
};

}  // namespace leash::runtime

// src/trace/include/leash/trace/derive.hpp
namespace leash::trace {

struct TraceError {
    FailureKind kind;
    std::string message;
    std::optional<std::filesystem::path> file;
};

struct DeriveReport { std::vector<std::filesystem::path> files_written; };
std::expected<DeriveReport, TraceError> derive_all(const store::RunDir& run,
                                                   const store::Cas& cas);

std::generator<std::expected<nlohmann::json, TraceError>> read_journal(
    const store::RunDir& run);

struct BudgetTotals { /* sums from journal `usage` entries */ };
struct Outcome {
    std::string label;
    std::vector<std::string> justification;
    BudgetTotals budget;
};
std::expected<Outcome, TraceError> outcome(const store::RunDir& run);

}  // namespace leash::trace
```
`iter()` and `read_journal` are C++23 `std::generator` coroutines whose bodies
live in the `.cpp`; callers range-for over journal entries without loading the
file. `RunSummary { id, flow, created_at, outcome, cost_usd }` backs
`leash runs list`.

## Deliverables
- `leash_store` and `leash_trace` CMake targets with headers, sources and
  Catch2 tests.
- Runtime integration and CLI subcommands `trace`, `report`, `runs`, `gc`.
- `tests/fixtures/runs/hello/` — a complete checked-in run directory produced
  from `examples/01_hello.leash` with the mock driver, used as a golden
  fixture for derivation tests.

## Acceptance criteria
- After `build/dev/src/cli/leash run examples/01_hello.leash --input '{"topic":"x"}' --driver anthropic=mock:tests/fixtures/mock/hello.json`,
  the run directory contains `manifest.json` (validates), `journal.jsonl`,
  `traces/action.jsonl` (validates line by line), `traces/outcome.json`
  (validates; label `unverified_success`), `artifacts/report.md`.
- `build/dev/src/cli/leash trace <id> --kind action` prints one row per effect; `--json` prints the raw lines.
- `build/dev/src/cli/leash report <id>` prints the Markdown; `report --regenerate` rewrites `traces/` and `report.md` byte-identically (test asserts equality with a `sha256sum` before/after).
- `build/dev/src/cli/leash runs list` shows the run with its outcome and cost; `runs show <id>` prints the manifest; `runs rm <id>` removes it.
- CAS test: putting the same 20 KiB payload twice yields one file; a journal
  entry whose payload exceeds 8 KiB stores `{"cas": "b3:…"}` and `read_journal`
  resolves it.
- `build/dev/src/cli/leash gc --keep-days 0` on a directory with two runs, one deleted, removes only unreferenced CAS objects (test).
- `ctest --preset dev -L 'store|trace'` passes; `run-clang-tidy -p build/dev -warnings-as-errors '*'` is clean.
- Derivation over `tests/fixtures/runs/hello/journal.jsonl` reproduces the checked-in `traces/` byte for byte.

## Notes and pitfalls
- Derived files are never the source of truth; `derive_all` must be safe to
  run on a journal produced by an older runtime as long as `journal_version` matches.
- Use `std::chrono` with RFC 3339 and millisecond precision everywhere; no
  local time. One helper, `rfc3339_millis(std::chrono::system_clock::time_point)`
  in `leash_store`, formats run ids, manifests and traces: `std::format` with
  `{:%FT%T}` after `floor<milliseconds>`, then `Z`.
- `RunDir::open` must not follow symlinks outside `runs_root` when given an id.
- The provisional `incomplete` label must be removed in WP-15; add a `TODO(WP-15)` where it is produced and in the outcome schema `description`.
- Write `traces/` files atomically (temp + rename) so a crash during derivation cannot leave half files.
- `gc` must never touch `.leash/mcp/` or worktrees of live runs (check for a `.lock` file that WP-11 will introduce; absent lock = not live).

## Dependencies
Hard: WP-02. Soft: WP-04 (integration; the derivation tests run on the checked-in fixture without it).

## Estimated size
M (3–4 days).

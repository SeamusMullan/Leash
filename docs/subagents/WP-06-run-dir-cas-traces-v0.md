# WP-06: Run directory, manifest, CAS, trace layer v0, report skeleton

Milestone: M1 · Size: M · Status: see `docs/status.md`

## Goal
Give every run a well-formed episode package directory: manifest, journal,
content-addressed blobs, a first derived trace (action), an outcome record and
a report skeleton, plus the CLI commands to list runs and inspect them. Later
WPs add the other seven traces and the real outcome rules on top of this
layer; the derivation-from-journal principle (docs/03 §10) is established here.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/10-cli-and-tooling.md` §3 (run directory), §1 (`trace`, `report`, `runs`, `gc`)
- `docs/04-determinism-and-replay.md` §3 (CAS rule), §9 (manifest)
- `docs/11-episode-package-and-eval.md` §1 (package contents), §2
- `docs/06-verification-and-evidence.md` §6 (labels; v0 uses two of them), §7 (report shape)
- `docs/03-runtime-architecture.md` §7, §10
- `spec/manifest.schema.json`, `spec/traces/action.schema.json`, `spec/traces/outcome.schema.json` (WP-02)

## Scope
- New crate `crates/leash-store` (no dependency on `leash-runtime`):
  - `cas.rs`: `Cas::open(root)`, `put(&[u8]) -> Hash` (blake3, stored at
    `<root>/<hex[0..2]>/<hex>`, write-to-temp-then-rename, no overwrite),
    `get(&Hash) -> Option<Vec<u8>>`, `contains`, `iter()`; hashes render as `b3:<hex>`.
  - `rundir.rs`: `RunDir::create(runs_root, flow_name, now) -> RunDir` with id
    `<UTC yyyy-mm-ddTHH-MM-SS>_<flow>_<6 hex>`; paths for `manifest.json`,
    `journal.jsonl`, `traces/`, `artifacts/`, `worktree/`; `RunDir::open(id_or_path)`
    resolving ids under `.leash/runs` and absolute/relative paths; `list(runs_root)`
    sorted by creation time; `remove`.
  - `manifest.rs`: `Manifest` struct (serde) mirroring docs/04 §9; `write`,
    `read`, validation via `leash_spec::manifest`.
  - `store.rs`: `CasPayloadStore` implementing the runtime's `PayloadStore`
    trait via a small adapter in `leash-runtime` (runtime depends on store).
- `crates/leash-trace`:
  - `journal_reader.rs`: streaming reader of `journal.jsonl` into
    `serde_json::Value` entries, validating each line with `leash_spec::journal`,
    resolving CAS references lazily.
  - `action.rs`: derive `traces/action.jsonl` from the journal: one line per
    effect with `{seq, at, op, site, summary, effect_id, status}` where `op ∈
    {ask, run, read_file, edit_file, run_tool, verify, checkpoint, gate,
    complete, fail}` (map journal kinds to ops; unknown kinds → `op: "effect"`).
  - `outcome.rs`: v0 rules: `complete` entry present with `status: ok` →
    `unverified_success` (no evidence machinery yet; WP-15 upgrades to
    `autonomous_verified_success`); a terminal `fail` → `failed`; otherwise
    `incomplete` (a sixth, provisional label allowed only until WP-15 —
    document it in the schema as provisional). Includes budget totals summed
    from `usage`.
  - `report.rs`: `report.md` skeleton per docs/06 §7 with task/flow/outcome
    header, an empty requirements table with a note "no checks declared" in
    v0, budget totals, interventions and entropy sections stating "not
    collected (v0)".
  - `derive_all(rundir) -> Result<()>` regenerating every derived file; the
    output must be byte-identical for the same journal (deterministic
    serialisation, sorted keys, fixed timestamp formatting).
- `leash-runtime` integration: `Runner` takes a `RunDir` and a `PayloadStore`;
  writes `manifest.json` at start (`mode`, program hash = blake3 of the source
  bytes until WP-10 provides `lir_hash`, inputs hash, seed, drivers from
  `Driver::id()`), and calls `derive_all` at the end of `run`/`resume`.
- `leash-cli`: `trace <run> --kind action [--json]` (table by default),
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
```rust
// leash-store
pub struct Hash([u8; 32]);  impl std::fmt::Display for Hash /* "b3:<hex>" */;
pub struct Cas { /* root */ }  impl Cas { pub fn open(root: &camino::Utf8Path) -> std::io::Result<Self>; pub fn put(&self, bytes: &[u8]) -> std::io::Result<Hash>; pub fn get(&self, h: &Hash) -> std::io::Result<Option<Vec<u8>>>; }
pub struct RunDir { pub id: String, pub root: camino::Utf8PathBuf }
impl RunDir { pub fn create(runs_root: &camino::Utf8Path, flow: &str, now: time::OffsetDateTime) -> std::io::Result<Self>;
              pub fn open(runs_root: &camino::Utf8Path, id_or_path: &str) -> std::io::Result<Self>;
              pub fn list(runs_root: &camino::Utf8Path) -> std::io::Result<Vec<RunSummary>>;
              pub fn manifest(&self) -> camino::Utf8PathBuf; pub fn journal(&self) -> camino::Utf8PathBuf; pub fn traces(&self) -> camino::Utf8PathBuf; pub fn artifacts(&self) -> camino::Utf8PathBuf; }
#[derive(serde::Serialize, serde::Deserialize)] pub struct Manifest { /* fields of docs/04 §9 */ }
// leash-trace
pub fn derive_all(run: &RunDir, cas: &Cas) -> Result<DeriveReport, TraceError>;   // DeriveReport { files_written: Vec<Utf8PathBuf> }
pub fn read_journal(run: &RunDir) -> impl Iterator<Item = Result<serde_json::Value, TraceError>>;
pub fn outcome(run: &RunDir) -> Result<Outcome, TraceError>;                       // Outcome { label: String, justification: Vec<String>, budget: BudgetTotals }
```
`RunSummary { id, flow, created_at, outcome: Option<String>, cost_usd: Option<String> }` backs `leash runs list`.

## Deliverables
- `crates/leash-store`, `crates/leash-trace` with tests.
- Runtime integration and CLI subcommands `trace`, `report`, `runs`, `gc`.
- `tests/fixtures/runs/hello/` — a complete checked-in run directory produced
  from `examples/01_hello.leash` with the mock driver, used as a golden
  fixture for derivation tests.

## Acceptance criteria
- After `cargo run -p leash-cli -- run examples/01_hello.leash --input '{"topic":"x"}' --driver anthropic=mock:tests/fixtures/mock/hello.json`,
  the run directory contains `manifest.json` (validates), `journal.jsonl`,
  `traces/action.jsonl` (validates line by line), `traces/outcome.json`
  (validates; label `unverified_success`), `artifacts/report.md`.
- `cargo run -p leash-cli -- trace <id> --kind action` prints one row per effect; `--json` prints the raw lines.
- `cargo run -p leash-cli -- report <id>` prints the Markdown; `report --regenerate` rewrites `traces/` and `report.md` byte-identically (test asserts equality with a `sha256sum` before/after).
- `cargo run -p leash-cli -- runs list` shows the run with its outcome and cost; `runs show <id>` prints the manifest; `runs rm <id>` removes it.
- CAS test: putting the same 20 KiB payload twice yields one file; a journal
  entry whose payload exceeds 8 KiB stores `{"cas": "b3:…"}` and `read_journal`
  resolves it.
- `gc --keep-days 0` on a directory with two runs, one deleted, removes only unreferenced CAS objects (test).
- `cargo nextest run -p leash-store -p leash-trace` passes; clippy clean.
- Derivation over `tests/fixtures/runs/hello/journal.jsonl` reproduces the checked-in `traces/` byte for byte.

## Notes and pitfalls
- Derived files are never the source of truth; `derive_all` must be safe to
  run on a journal produced by an older runtime as long as `journal_version` matches.
- Use `time` with RFC 3339 and millisecond precision everywhere; no local time.
- `RunDir::open` must not follow symlinks outside `runs_root` when given an id.
- The provisional `incomplete` label must be removed in WP-15; add a `TODO(WP-15)` where it is produced and in the outcome schema `description`.
- Write `traces/` files atomically (temp + rename) so a crash during derivation cannot leave half files.
- `gc` must never touch `.leash/mcp/` or worktrees of live runs (check for a `.lock` file that WP-11 will introduce; absent lock = not live).

## Dependencies
Hard: WP-02. Soft: WP-04 (integration; the derivation tests run on the checked-in fixture without it).

## Estimated size
M (3–4 days).

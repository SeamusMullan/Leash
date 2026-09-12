# WP-17: Context selection, memory resources and harness profiles H0–H3

Milestone: M4 · Size: M · Status: see `docs/status.md`

## Goal
Models only see what the program selects, and what they see is recorded.
`select(...)` builds a deterministic, hashed, budgeted `Context`; `memory`
resources carry project knowledge; the context trace shows what was selected
and whether it mattered; and profiles H0–H3 control which harness artifacts
and protocol sections are exposed to the model, so the harness contribution
can be measured.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/05-harness-model.md` §1 rows 2, 4, 5; §3 (profiles matrix and mechanics); §5
- `docs/02-language-spec.md` §1 (resources are pure descriptors), §12 (`std.context`)
- `docs/01-language-tour.md` §8
- `docs/09-drivers-and-tools.md` §2 (first user message, caching)
- `docs/11-episode-package-and-eval.md` §1 (context trace), §3 (context-trace meaningfulness)
- `docs/04-determinism-and-replay.md` §5 (file reads captured as hashes + CAS)
- WP-11 (fs tools), WP-13 (where the rendered context goes)

## Scope
- `select(repo, files: [String] = [], globs: [String] = [], memory: [Resource] = [],
  max_tokens: Int = 100_000, strategy: .Whole | .Truncate = .Truncate) -> Context`
  as an `fs.read` effect. Files are resolved against the working copy,
  de-duplicated, sorted by path; memory artifacts come first in declaration
  order. Each file's content hash is journaled and content goes to the CAS.
- Token estimation: a local approximation (characters / 3.2, documented as
  ±20 %) plus optional exact counting through the Messages API
  `count_tokens` endpoint when `--exact-tokens` or `[context] exact_tokens = true`
  (journaled as a `net` effect).
- Truncation policy `.Truncate`: drop the largest files first until the
  estimate fits `max_tokens`; `.Whole` fails with `Context` if it does not
  fit. Every drop is recorded in the context trace entry with a reason.
- Rendering: one stable text format — a header line per artifact
  (`### memory: AGENT_GUIDE.md`, `### file: src/validator.js (sha256 …)`), a
  fenced block with the language inferred from the extension, files separated
  by blank lines. `Context.render()` is pure; `Context.hash` is the BLAKE3 of
  the rendering.
- `memory name = file(path)` declarations: a `Resource` descriptor read at
  `select` time; missing files fail with `Context` naming the declaration.
- Context trace entry per `select`: `{ seq, hash, tokens_est, tokens_exact?,
  max_tokens, strategy, files: [{ path, hash, bytes, tokens_est, included, reason? }],
  memory: [...], later_referenced: [] }`. At run end the trace layer fills
  `later_referenced` with the selected paths that a later `fs.write`/`fs.edit`,
  `verify` evidence or attribution `evidence` field mentions (feeds the
  context-trace meaningfulness metric).
- Profiles per docs/05 §3: `std/profiles/{h0,h1,h2,h3}.toml` (each lists
  artifact keys and protocol-section keys; monotonic), `--profile` flag and
  `[profile] default` in `leash.toml`, recorded in the manifest.
- Artifact rendering into `<workdir>/harness/` before every `run` (and
  removed from diffs, snapshots and entropy audits): `TOOLS.md` (tool registry
  from the run's tools and `allow.shell` patterns), `TEST_COMMANDS.md`,
  `memory/<name>.md` (copies of memory resources), `TASK_STATE.md` (rendered
  from the flow's `state`, re-rendered before each `run`), `CHECKS.md`
  (deterministic check registry rendered from `ensure`, including expected
  outputs), `REPORT_TEMPLATE.md`.
- Protocol sections appended to the agent's system prompt in fixed order
  after the program's own system prompt: `std/protocols/{tool_usage,
  context_selection, bug_reproduction, failure_attribution, verification}.md`,
  gated by profile. For API drivers, `TASK_STATE.md` content is also appended
  as the last (volatile) system block so caching of the stable prefix holds.
- Manifest field `rendered_artifacts: [{ path, hash }]` and `profile`.

## Out of scope
- Symbol-level selection (`symbols:`) and AST-aware chunking (post-v1; the
  parameter is rejected with a clear `Context` failure).
- Retrieval, embeddings, ranking strategies.
- Experiential memory / skill libraries (docs/05 §5).
- Custom ladders beyond the four shipped profiles (the file format supports
  them; no CLI to author them).

## Interfaces
```rust
pub struct Context { pub hash: Blake3, pub tokens_est: u64, pub tokens_exact: Option<u64>,
                     pub artifacts: Vec<ContextArtifact>, rendered: Arc<str> }
pub struct ContextArtifact { pub kind: ArtifactKind /* Memory | File */, pub path: String, pub hash: Blake3,
                             pub bytes: u64, pub tokens_est: u64, pub included: bool, pub reason: Option<String> }
impl Context { pub fn render(&self) -> &str; }

pub struct Profile { pub name: String, pub artifacts: BTreeSet<ArtifactKey>, pub protocols: Vec<ProtocolKey> }
pub enum ArtifactKey { ToolRegistry, TestCommandRegistry, Memory, TaskState, CheckRegistry, ReportTemplate }
pub enum ProtocolKey { ToolUsage, ContextSelection, BugReproduction, FailureAttribution, Verification }
pub trait ProfileRenderer { async fn render(&self, run: &RunCtx, profile: &Profile, workdir: &Path) -> Result<Vec<RenderedArtifact>, Failure>; }
```
`RunRequest.profile_artifacts` (docs/09 §1) is filled by this WP; WP-13/18/19
consume it (system prompt sections and, for CLI drivers, the files on disk).

## Deliverables
- `crates/leash-effects/src/context/{select.rs, render.rs, tokens.rs}`
- `crates/leash-runtime/src/profile/{mod.rs, render.rs}`; `std/profiles/*.toml`; `std/protocols/*.md`
- `crates/leash-trace/src/context_trace.rs` (later-reference pass)
- Tests: rendering golden files; hash stability; truncation order; profile
  matrix; manifest recording.
- Docs: `docs/05-harness-model.md` §3 updated with final file names.

## Acceptance criteria
- `cargo nextest run -p leash-effects -p leash-runtime -p leash-trace` passes.
- Selecting the same files twice, or in a different `files:` order, yields
  the same `Context.hash`; changing one byte in one file changes it.
- With `max_tokens: 1_000` and three files of 2k, 500 and 300 estimated
  tokens under `.Truncate`, the 2k file is dropped and the trace entry says
  `reason: "exceeds max_tokens"`; under `.Whole` `select` fails with `Context`.
- `leash run … --profile h0` renders nothing under `harness/` and adds no
  system sections; `--profile h3` renders exactly `TOOLS.md`,
  `TEST_COMMANDS.md`, `memory/*.md`, `TASK_STATE.md`, `CHECKS.md`,
  `REPORT_TEMPLATE.md` and appends five protocol sections; `h1`/`h2` match
  the matrix rows. `manifest.rendered_artifacts` lists them with hashes.
- `harness/` never appears in `artifacts/patch.diff` or in snapshots.
- After a mock run that edits one of three selected files, the context trace
  entry's `later_referenced` contains exactly that path.

## Notes and pitfalls
- Keep the rendering byte-stable across platforms (LF line endings, UTF-8,
  no timestamps); it is a prompt-cache key.
- Do not read files through `std::fs` directly; go through the `fs.read`
  effect so hashes and CAS entries are journaled and replay works offline.
- Memory resources may live outside the working copy (e.g. a shared
  `AGENT_GUIDE.md`); they need `fs.read` capability for their path.
- The check registry at H3 exposes expected outputs to the model by design
  (paper's visibility matrix); hidden evaluator checks (WP-26) must never be
  rendered.
- Re-render `TASK_STATE.md` before each `run`, not each turn; CLI agents
  re-read it themselves.

## Dependencies
Hard: WP-11, WP-13. Soft: WP-15 (`ensure` for `CHECKS.md`; render an empty registry until then), WP-16 (`state`).

## Estimated size
M (2–4 days).

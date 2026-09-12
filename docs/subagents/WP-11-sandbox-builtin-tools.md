# WP-11: Sandbox and built-in tools (fs, shell, test, git), snapshots

Milestone: M3 · Size: L · Status: see `docs/status.md`

## Goal
After this WP, a run has an isolated working copy of the target repository, and
the built-in tools (`fs.*`, `shell`, `test`, `git.*`) execute inside it under a
path policy, a no-shell command policy, a scrubbed environment, timeouts and
output caps. Working-copy snapshots exist at checkpoints so `fork` and `diff`
work. This is the mechanism layer that WP-12 wires into enforcement.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/03-runtime-architecture.md` §8 (effect handlers), §8.1 (tool layer), §8.2 (sandbox)
- `docs/07-budgets-and-capabilities.md` §2.1 (glob/shell/net semantics), §2.3 (enforcement points), §5 (known limits), §6 (threat model)
- `docs/04-determinism-and-replay.md` §5 (what is captured), §7 (working-copy snapshots)
- `docs/09-drivers-and-tools.md` §7 (built-in tool table, middleware order)
- `docs/06-verification-and-evidence.md` §2 (`test` result fields and runner parsing)
- ADR-0007
- WP-04's `EffectCtx`, `EffectHandler`, journal writer; WP-06's CAS API

## Scope
- `RunWorkspace` creation: `git worktree add --detach <run_dir>/worktree <rev>` from the input `Repo` (path + rev; default `HEAD`); warn if the source tree is dirty; fallback to a recursive copy for non-git directories; per-run `HOME` and temp dirs inside the run directory.
- Path policy: relative paths only; reject any `..` segment; resolve symlinks and require the resolved path to stay under the workspace root; glob matching (`globset`) on the lexical relative path for read and write sets.
- Environment scrubbing: start empty; set `PATH`, `HOME` (sandbox home), `LANG=C.UTF-8`, `TMPDIR`; add allowlisted variables from the active `CapSet.env`; inject requested secrets on demand (values come tagged from WP-12/WP-04).
- Command execution: tokenise with `shell-words`; reject unquoted control operators (`; | & && || > < >> << $( ) ` ` `` newline`) unless the innermost allow pattern starts with `sh -c ` / `bash -c `, in which case run `sh -c <raw string>` and match the pattern against the entire raw string; otherwise `execve` the argv directly (no shell); new process group; `kill_on_drop`; timeout → SIGTERM, then SIGKILL after 5 s; stdout/stderr streamed, spilled to CAS beyond 8 KiB, capped at 1 MiB by default with a `truncated` flag; `cwd` must resolve inside the workspace.
- Shared policy crate `leash-policy` (pure, no I/O): `tokenize_command`, `command_permitted(argv, patterns)`, `path_permitted(rel, globs)`, `host_permitted(host, hosts)`; used by WP-09 for static checks and here at run time. Create it if WP-09 has not.
- Built-in tools registered as `Tool` values with schemas: `fs.read(path) -> String`, `fs.read_bytes`, `fs.write(path, content)`, `fs.edit(path, old, new)` (exactly one match required; rejects with `Tool("stale")` if the file changed since the last read in this run — keep a read ledger of content hashes), `fs.glob(pattern) -> [String]`, `fs.grep(pattern, glob) -> [{path, line, text}]`, `fs.mkdir(path)`, `fs.rm(path, recursive: Bool = false)`, `shell(cmd, timeout: Duration = 10m, cwd: String?) -> ShellResult`, `test(cmd, runner: RunnerKind?, junit: String?) -> TestResult`, `git.status/diff/log/commit/checkout/worktree` (restricted to the workspace; no `push`, no `remote`; those belong to WP-12 gating and `net`).
- Test-runner parsing into `TestResult { passed, failed, skipped, output, exit_code, duration, runner }`: Jest, Vitest, pytest, cargo test, go test, JUnit XML (when `junit:` points at a file); unknown runner → counts unknown, `runner: .Unknown`, exit code only, warning in the result.
- Snapshots: write the working tree (tracked + untracked, excluding `.leash/` and ignored files) to `refs/leash/<run_id>/<seq>` using a temporary index (`GIT_INDEX_FILE`) so the user's index is untouched; `restore(snapshot)` checks out that tree and removes files not in it; `diff(from, to)` returns a unified diff plus a file list; hook points for WP-16 (checkpoints), WP-15 (`complete`) and WP-13/18/19 (after `run`).
- Journal integration: every tool call produces started/finished entries through the WP-04 gateway with argv/cwd/env-allowlist/exit code/duration/timeout flag and CAS references for outputs.

## Out of scope
- Budget accounting, scope stacks, capability narrowing and post-hoc diff reverts (WP-12; this WP exposes `RunWorkspace::diff` and the policy predicates it needs).
- Container backend (M10); network enforcement for subprocesses (documented limit).
- MCP and Leash-defined tools (WP-14).

## Interfaces
```rust
// crate leash-policy (pure)
pub fn tokenize_command(cmd: &str) -> Result<Vec<String>, CommandError>;     // CommandError::ControlOperator(char) etc.
pub fn command_permitted(argv: &[String], raw: &str, patterns: &[ShellPattern]) -> Permit;  // Permit::{Direct, ViaSh, Denied(reason)}
pub fn path_permitted(rel: &Path, globs: &GlobSet) -> bool;
pub fn host_permitted(host: &str, allowed: &[HostPattern]) -> bool;

// crate leash-effects
pub trait SandboxBackend: Send + Sync { fn create(&self, spec: &WorkspaceSpec) -> Result<RunWorkspace, SandboxError>; }
pub struct WorkspaceSpec { pub source: RepoSource, pub run_dir: PathBuf }   // RepoSource::{Git { path, rev }, Dir { path }}
pub struct RunWorkspace { pub root: PathBuf, pub home: PathBuf, pub git: Option<GitRepo> }
impl RunWorkspace {
    pub fn resolve(&self, rel: &str, access: Access, caps: &CapSet) -> Result<PathBuf, Failure>;   // policy + symlink resolution
    pub async fn exec(&self, spec: ExecSpec, caps: &CapSet, cancel: CancellationToken, cas: &Cas) -> Result<ExecResult, Failure>;
    pub fn snapshot(&self, run_id: &RunId, seq: u64) -> Result<SnapshotRef, SandboxError>;
    pub fn restore(&self, snap: &SnapshotRef) -> Result<(), SandboxError>;
    pub fn diff(&self, from: Option<&SnapshotRef>, to: Option<&SnapshotRef>) -> Result<Diff, SandboxError>;  // None = working tree
}
pub struct ExecSpec { pub command: String, pub cwd: Option<String>, pub timeout: Duration, pub stdin: Option<Vec<u8>>, pub output_cap: usize, pub extra_env: BTreeMap<String, String> }
pub struct ExecResult { pub argv: Vec<String>, pub stdout: Blob, pub stderr: Blob, pub exit_code: Option<i32>, pub timed_out: bool, pub truncated: bool, pub duration: Duration }
pub struct TestResult { pub passed: u32, pub failed: u32, pub skipped: u32, pub output: Blob, pub exit_code: Option<i32>, pub duration: Duration, pub runner: RunnerKind, pub counts_known: bool }
pub fn builtin_tools() -> Vec<Tool>;   // Tool { name, description, input_schema, output_type, caps_required, impl: ToolImpl::Builtin(..) }
```

## Deliverables
- `crates/leash-policy/`, `crates/leash-effects/src/{sandbox/{workspace,exec,env,snapshot},tools/{fs,shell,test,git,runners}}.rs`.
- Tests with temporary git repositories (`tempfile` + `git init` + commits): `tests/workspace.rs`, `tests/policy.rs` (tokeniser, pattern matching table), `tests/exec.rs` (timeout kills a `sleep 60` process group; output cap; env scrub shows only allowlisted vars), `tests/fs_tools.rs` (read/write/edit/staleness/glob/grep), `tests/runners.rs` (fixture outputs for each runner), `tests/snapshot.rs` (snapshot → modify → restore → identical tree, untracked files included), `tests/escape.rs`.
- `docs/09-drivers-and-tools.md` §7 amended if signatures change.

## Acceptance criteria
- `cargo nextest run -p leash-policy -p leash-effects` → green.
- Test `escape_via_symlink_is_denied`: a symlink `inside/link -> /etc` in the workspace; `fs.read("inside/link/passwd")` under `fs.read: "**"` → `Permission` failure; the file is never opened.
- Test `control_operators_rejected`: `shell("npm test; rm -rf /")` under `allow.shell = ["npm test*"]` → `Permission` with message containing `control operator ';'`; no process spawned.
- Test `prefix_pattern_permits_args`: `shell("npm test -- login")` under `["npm test*"]` → permitted, executed without a shell (assert argv is `["npm","test","--","login"]`).
- Test `sh_c_requires_explicit_pattern`: `shell("sh -c 'a | b'")` under `["npm test*"]` → denied; under `["sh -c *"]` → runs via `sh -c` and the journal entry records `via_sh: true`.
- Test `timeout_kills_group`: `shell("sh -c 'sleep 60 & wait'", timeout: 1s)` under `["sh -c *"]` → `timed_out: true` within 7 s and no leftover `sleep` process.
- Test `edit_stale`: read file, modify it on disk outside the tool, `fs.edit` → `Tool("stale")`.
- Test `snapshot_restore`: create files, snapshot, delete/modify, restore → `git status --porcelain` empty relative to snapshot and untracked files restored.
- `leash run tests/fixtures/shell_probe.leash --driver anthropic=mock` (a flow calling `shell("git status")`) → completes; the journal shows argv, exit code 0, duration.

## Notes and pitfalls
- Pattern semantics (shared with WP-09; one implementation in `leash-policy`): tokenise pattern and command alike; each pattern token is a glob over one command token; a last pattern token equal to `*` or ending in `*` matches that token and all remaining tokens; otherwise token counts must match. Quoted arguments are single tokens.
- Symlink resolution must happen *after* the lexical check and *before* opening; use `std::fs::canonicalize` on the parent directory for not-yet-existing write targets.
- Never pass the user's environment through; `PATH` should be the user's `PATH` (tools like `npm`, `node`, `cargo` must be findable) but nothing else unless allowlisted.
- Output streaming: read stdout/stderr concurrently to avoid pipe deadlocks; write to CAS incrementally.
- Snapshot cost is dominated by `git add -A` of untracked files; skip `node_modules`/`target` only if git-ignored (respect `.gitignore`; do not invent rules).
- Journal `input` for `shell` must include the resolved argv and `cwd` relative to the workspace so replay diffs are readable.
- Runner detection from the command is heuristic; a wrong guess must not fail the check, only mark `counts_known: false`.

## Dependencies
Hard: WP-04, WP-06. Soft: WP-09 (shared `leash-policy`), WP-12 (consumer).

## Estimated size
L (1–2 weeks).

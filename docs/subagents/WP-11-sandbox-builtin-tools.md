# WP-11: Sandbox and built-in tools (fs, shell, test, git), snapshots

Milestone: M3 · Size: L · Status: see `docs/status.md`

## Goal
After this WP, a run has an isolated working copy of the target repository, and
the built-in tools (`fs.*`, `shell`, `test`, `git.*`) execute inside it under a
path policy, a no-shell command policy, a scrubbed environment, timeouts and
output caps. Working-copy snapshots exist at checkpoints so `fork` and `diff`
work. This is the mechanism layer that WP-12 wires into enforcement.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/03-runtime-architecture.md` §8 (effect handlers), §8.1 (tool layer), §8.2 (sandbox)
- `docs/07-budgets-and-capabilities.md` §2.1 (glob/shell/net semantics), §2.3 (enforcement points), §5 (known limits), §6 (threat model)
- `docs/04-determinism-and-replay.md` §5 (what is captured), §7 (working-copy snapshots)
- `docs/09-drivers-and-tools.md` §7 (built-in tool table, middleware order)
- `docs/06-verification-and-evidence.md` §2 (`test` result fields and runner parsing)
- ADR-0007
- WP-04's `EffectCtx`, `EffectHandler`, journal writer; WP-06's CAS API

## Scope
- `RunWorkspace` creation: `git worktree add --detach <run_dir>/worktree <rev>` from the input `Repo` (path + rev; default `HEAD`); warn if the source tree is dirty; fallback to a recursive copy for non-git directories; per-run `HOME` and temp dirs inside the run directory.
- Path policy: relative paths only; reject any `..` segment; resolve symlinks and require the resolved path to stay under the workspace root; glob matching with the hand-written matcher in `leash_policy` on the lexical relative path for read and write sets.
- Environment scrubbing: start empty; set `PATH`, `HOME` (sandbox home), `LANG=C.UTF-8`, `TMPDIR`; add allowlisted variables from the active `CapSet.env`; inject requested secrets on demand (values come tagged from WP-12/WP-04).
- Command execution: tokenise with the hand-written tokenizer in `leash_policy`; reject unquoted control operators (`; | & && || > < >> << $( ) ` ` `` newline`) unless the innermost allow pattern starts with `sh -c ` / `bash -c `, in which case run `sh -c <raw string>` and match the pattern against the entire raw string; otherwise `execve` the argv directly (no shell) through Boost.Process v2; new process group; the child is owned by an RAII handle whose destruction terminates the group; timeout → SIGTERM, then SIGKILL after 5 s; stdout/stderr streamed as async pipes on the run's `io_context`, spilled to CAS beyond 8 KiB, capped at 1 MiB by default with a `truncated` flag; `cwd` must resolve inside the workspace.
- Shared policy module `leash_policy` (pure, no I/O): `tokenize_command`, `command_permitted(argv, patterns)`, `path_permitted(rel, globs)`, `host_permitted(host, hosts)`; used by WP-09 for static checks and here at run time. WP-01 creates the target empty; fill it if WP-09 has not.
- Built-in tools registered as `Tool` values with schemas: `fs.read(path) -> std::string`, `fs.read_bytes`, `fs.write(path, content)`, `fs.edit(path, old, new)` (exactly one match required; rejects with `Tool("stale")` if the file changed since the last read in this run — keep a read ledger of content hashes), `fs.glob(pattern) -> std::vector<std::string>`, `fs.grep(pattern, glob) -> std::vector<GrepHit>` where `GrepHit { path, line, text }`, `fs.mkdir(path)`, `fs.rm(path, recursive: bool = false)`, `shell(cmd, timeout: std::chrono::nanoseconds = 10min, cwd: std::optional<std::string>) -> ShellResult`, `test(cmd, runner: std::optional<RunnerKind>, junit: std::optional<std::string>) -> TestResult`, `git.status/diff/log/commit/checkout/worktree` (restricted to the workspace; no `push`, no `remote`; those belong to WP-12 gating and `net`).
- Test-runner parsing into `TestResult { passed, failed, skipped, output, exit_code, duration, runner }`: Jest, Vitest, pytest, libtest output, go test, JUnit XML (when `junit:` points at a file); unknown runner → counts unknown, `runner = RunnerKind::Unknown`, exit code only, warning in the result.
- Snapshots: write the working tree (tracked + untracked, excluding `.leash/` and ignored files) to `refs/leash/<run_id>/<seq>` using a temporary index (`GIT_INDEX_FILE`) so the user's index is untouched; `restore(snapshot)` checks out that tree and removes files not in it; `diff(from, to)` returns a unified diff plus a file list; hook points for WP-16 (checkpoints), WP-15 (`complete`) and WP-13/18/19 (after `run`).
- Journal integration: every tool call produces started/finished entries through the WP-04 gateway with argv/cwd/env-allowlist/exit code/duration/timeout flag and CAS references for outputs.

## Out of scope
- Budget accounting, scope stacks, capability narrowing and post-hoc diff reverts (WP-12; this WP exposes `RunWorkspace::diff` and the policy predicates it needs).
- Container backend (M10); network enforcement for subprocesses (documented limit).
- MCP and Leash-defined tools (WP-14).

## Interfaces
```cpp
// src/policy/include/leash/policy/command.hpp — pure, no I/O
namespace leash::policy {

struct ControlOperator { char op; };            // ';' '|' '&' '>' '<' '\n'
struct UnterminatedQuote { std::size_t offset; };
struct EmptyCommand {};

using CommandError = std::variant<ControlOperator, UnterminatedQuote, EmptyCommand>;

std::expected<std::vector<std::string>, CommandError> tokenize_command(std::string_view cmd);

struct Permit {
    struct Direct {};
    struct ViaSh {};
    struct Denied { std::string reason; };

    std::variant<Direct, ViaSh, Denied> value;
};

// One operator() per alternative. A unit test visits every alternative.
struct PermitVisitor {
    void operator()(const Permit::Direct& p) const;
    void operator()(const Permit::ViaSh& p) const;
    void operator()(const Permit::Denied& p) const;
};

Permit command_permitted(const std::vector<std::string>& argv, std::string_view raw,
                         const std::vector<ShellPattern>& patterns);

bool path_permitted(const std::filesystem::path& rel, const Globs& globs);
bool host_permitted(std::string_view host, const std::vector<HostPattern>& allowed);

}  // namespace leash::policy

// src/effects/include/leash/effects/sandbox/backend.hpp
namespace leash::effects {

class SandboxBackend {
public:
    virtual ~SandboxBackend() = default;
    virtual std::expected<RunWorkspace, SandboxError> create(const WorkspaceSpec& spec) = 0;
};

struct RepoSource {
    struct Git { std::filesystem::path path; std::string rev; };
    struct Dir { std::filesystem::path path; };

    std::variant<Git, Dir> value;
};

struct WorkspaceSpec {
    RepoSource source;
    std::filesystem::path run_dir;
};

struct RunWorkspace {
    std::filesystem::path root;
    std::filesystem::path home;
    std::optional<GitRepo> git;

    // path policy plus symlink resolution
    std::expected<std::filesystem::path, Failure>
    resolve(std::string_view rel, Access access, const CapSet& caps) const;

    asio::awaitable<std::expected<ExecResult, Failure>>
    exec(const ExecSpec& spec, const CapSet& caps, asio::cancellation_signal& cancel, Cas& cas);

    std::expected<SnapshotRef, SandboxError> snapshot(const RunId& run_id, std::uint64_t seq);
    std::expected<void, SandboxError> restore(const SnapshotRef& snap);
    // nullopt = working tree
    std::expected<Diff, SandboxError> diff(const std::optional<SnapshotRef>& from,
                                           const std::optional<SnapshotRef>& to) const;
};

struct ExecSpec {
    std::string command;
    std::optional<std::string> cwd;
    std::chrono::nanoseconds timeout = std::chrono::minutes(10);
    std::optional<std::vector<std::byte>> stdin_data;
    std::size_t output_cap = 1u << 20;
    std::map<std::string, std::string> extra_env;
};

struct ExecResult {
    std::vector<std::string> argv;
    Blob stdout_blob;    // not `stdout`: that name is a macro under some standard libraries
    Blob stderr_blob;
    std::optional<int> exit_code;
    bool timed_out = false;
    bool truncated = false;
    std::chrono::nanoseconds duration{};
};

struct TestResult {
    std::uint32_t passed = 0;
    std::uint32_t failed = 0;
    std::uint32_t skipped = 0;
    Blob output;
    std::optional<int> exit_code;
    std::chrono::nanoseconds duration{};
    RunnerKind runner = RunnerKind::Unknown;
    bool counts_known = false;
};

// Tool { name, description, input_schema, output_type, caps_required,
//        body = ToolImpl::Builtin{...} }
std::vector<Tool> builtin_tools();

}  // namespace leash::effects
```

## Deliverables
- `src/policy/`, `src/effects/sandbox/{workspace,exec,env,snapshot}.cpp`, `src/effects/tools/{fs,shell,test,git,runners}.cpp`, with headers under `src/policy/include/leash/policy/` and `src/effects/include/leash/effects/`, registered in `tests/policy/CMakeLists.txt` and `tests/effects/CMakeLists.txt`.
- Tests with temporary git repositories (testkit `TempDir` + `git init` + commits): `tests/effects/workspace_test.cpp`, `tests/policy/policy_test.cpp` (tokeniser, pattern matching table), `tests/effects/exec_test.cpp` (timeout kills a `sleep 60` process group; output cap; env scrub shows only allowlisted vars), `tests/effects/fs_tools_test.cpp` (read/write/edit/staleness/glob/grep), `tests/effects/runners_test.cpp` (fixture outputs for each runner), `tests/effects/snapshot_test.cpp` (snapshot → modify → restore → identical tree, untracked files included), `tests/effects/escape_test.cpp`.
- `docs/09-drivers-and-tools.md` §7 amended if signatures change.

## Acceptance criteria
- `ctest --preset dev -L '(policy|effects)'` → green.
- Test `escape_via_symlink_is_denied`: a symlink `inside/link -> /etc` in the workspace; `fs.read("inside/link/passwd")` under `fs.read: "**"` → `Permission` failure; the file is never opened.
- Test `control_operators_rejected`: `shell("npm test; rm -rf /")` under `allow.shell = ["npm test*"]` → `Permission` with message containing `control operator ';'`; no process spawned.
- Test `prefix_pattern_permits_args`: `shell("npm test -- login")` under `["npm test*"]` → permitted, executed without a shell (assert argv is `["npm","test","--","login"]`).
- Test `sh_c_requires_explicit_pattern`: `shell("sh -c 'a | b'")` under `["npm test*"]` → denied; under `["sh -c *"]` → runs via `sh -c` and the journal entry records `via_sh: true`.
- Test `timeout_kills_group`: `shell("sh -c 'sleep 60 & wait'", timeout: 1s)` under `["sh -c *"]` → `timed_out: true` within 7 s and no leftover `sleep` process.
- Test `edit_stale`: read file, modify it on disk outside the tool, `fs.edit` → `Tool("stale")`.
- Test `snapshot_restore`: create files, snapshot, delete/modify, restore → `git status --porcelain` empty relative to snapshot and untracked files restored.
- `leash run tests/fixtures/shell_probe.leash --driver anthropic=mock` (a flow calling `shell("git status")`) → completes; the journal shows argv, exit code 0, duration.

## Notes and pitfalls
- Pattern semantics (shared with WP-09; one implementation in `leash_policy`): tokenise pattern and command alike; each pattern token is a glob over one command token; a last pattern token equal to `*` or ending in `*` matches that token and all remaining tokens; otherwise token counts must match. Quoted arguments are single tokens.
- Symlink resolution must happen *after* the lexical check and *before* opening; use `std::filesystem::canonical` on the parent directory for not-yet-existing write targets (`weakly_canonical` when the parent itself may not exist).
- Never pass the user's environment through; `PATH` should be the user's `PATH` (tools like `npm`, `node` and other project toolchains must be findable) but nothing else unless allowlisted.
- Output streaming: read stdout/stderr concurrently on the same `io_context` to avoid pipe deadlocks; write to CAS incrementally.
- Snapshot cost is dominated by `git add -A` of untracked files; skip `node_modules` or a build output directory only if git-ignored (respect `.gitignore`; do not invent rules).
- Journal `input` for `shell` must include the resolved argv and `cwd` relative to the workspace so replay diffs are readable.
- Runner detection from the command is heuristic; a wrong guess must not fail the check, only mark `counts_known: false`.

## Dependencies
Hard: WP-04, WP-06. Soft: WP-09 (shared `leash_policy`), WP-12 (consumer).

## Estimated size
L (1–2 weeks).

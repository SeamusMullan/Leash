# WP-22: CLI completeness and UX

Milestone: M8 · Size: M · Status: see `docs/status.md`

## Goal
`leash` exposes every command in `docs/10` §1 with the documented flags, loads `leash.toml` with a defined precedence, implements gate policies, prints progress a person can follow, exits with the documented codes and offers `--json` for machines. Earlier WPs shipped minimal subcommands to test their own work; this WP makes the surface complete, consistent and snapshot-tested.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/10-cli-and-tooling.md` (all sections)
- `docs/04-determinism-and-replay.md` §4 (modes), §6 (divergence output), §8 (`--reuse`), §9 (manifest)
- `docs/07-budgets-and-capabilities.md` §3 (gate policies), §4 (secrets providers)
- `docs/11-episode-package-and-eval.md` §6 (`leash diff`)
- `docs/03-runtime-architecture.md` §11–§12
- `docs/09-drivers-and-tools.md` §1 (driver construction from config)

## Scope
- Commands exactly as tabled in `docs/10` §1: `new check fmt ir run replay resume fork trace report diff runs gc test mcp mcp-serve eval lsp doctor`, plus `config show`, `completions`, `doc` (WP-24) and `--version` on every subcommand. `test`, `eval`, `lsp`, `mcp-serve`, `doc` delegate to WP-25/26/23/14/24 crates; this WP owns argument parsing, help text and exit codes for all of them, and adds the new commands to `docs/10` §1.
- Configuration: `Config` built from layers with precedence CLI flags > environment (`LEASH_*`, e.g. `LEASH_PROFILE`, `LEASH_GATE`, `LEASH_DRIVER_ANTHROPIC_MODEL`) > `leash.toml` (nearest ancestor directory) > std defaults. `leash config show --json` prints the merged result with the source of each value.
- Gate policies (`--gate tty|deny|approve|fail`; default `tty` when stdin is a TTY, otherwise `deny`): a `GatePolicy` handed to the runtime's `HumanHandler`; `approve` prints a loud warning at start; every gate is journaled (WP-16 writes the entry; this WP supplies the answer and its `source: tty|policy`).
- Terminal UX (`docs/10` §4): one progress line per effect with elapsed time, kind, target, usage and cost; a final summary block (label, budget totals, run directory, next commands); `--quiet` suppresses progress; `--json` emits one JSON object on stdout and progress on stderr; `--log <level>` routes `tracing` to stderr; `--color auto|always|never`.
- Ctrl-C: first press cancels in-flight effects through the run's cancellation token (WP-20), waits for settlement, prints `resume with: leash resume <run_id>`; second press exits 130 immediately.
- Exit codes 0, 1, 2, 3, 4, 130 as in `docs/10`.
- `leash new <name> --template fix-bug|triage|review`: scaffolds `leash.toml`, `flows/main.leash`, `prompts/*.md`, `.gitignore` (`.leash/runs`, `.leash/cas`, `.leash/test-cache`), `README.md`; templates are trimmed versions of `examples/02`, `03`, `04`.
- `leash doctor`: versions of `claude` and `codex` on `PATH`, API key presence (never values), git availability, sandbox prerequisites, price table age, `leash.toml` validity, writable run directory.
- `leash runs list|show|rm`, `leash gc --keep-days N` (CAS objects and snapshots unreferenced by kept runs), `leash trace` filters, `leash report --regenerate`.
- `leash diff <run-a> <run-b>`: align journals by effect id; report added, removed and changed entries (input hash, output hash, status, cost), the first divergence and outcome differences; `leash diff <run> --from cp1 --to cp2` shows the working-copy diff between snapshots via the sandbox (WP-11).
- Shell completions: `leash completions bash|zsh|fish` (clap_complete).
- Snapshot tests with `trycmd` for help output and every command's happy path against the mock driver; `insta` snapshots for `--json` outputs.

## Out of scope
- New runtime behaviour. If a command needs a runtime feature that does not exist, record it in `docs/status.md` follow-ups and leave the flag documented as "not yet implemented" (exit 2).
- LSP internals (WP-23), `leash test` internals (WP-25), `leash eval` internals (WP-26), `leash doc` internals (WP-24).
- A TUI.

## Interfaces
```rust
// crates/leash-cli/src/config.rs
pub struct Config { pub package: PackageCfg, pub drivers: DriverCfgs, pub profile: String, pub caps_default: CapSet,
                    pub budget_default: Budget, pub secrets: SecretsProvider, pub prices: PriceTable, pub runs: RunsCfg,
                    pub gate: GatePolicy, pub sources: BTreeMap<String, ConfigSource> }
pub enum ConfigSource { Cli, Env(String), File(PathBuf), Default }
pub fn load(cwd: &Path, cli: &CliOverrides) -> Result<Config, ConfigError>;

// crates/leash-cli/src/exit.rs
pub fn exit_code(result: &RunResult) -> i32;   // 0 ok, 1 fail, 2 usage/compile, 3 replay, 4 budget/permission, 130 interrupted

// crates/leash-cli/src/ui.rs
pub trait Reporter { fn effect_started(&self, e: &EffectStart); fn effect_finished(&self, e: &EffectEnd);
                     fn gate(&self, prompt: &str) -> GateAnswer; fn summary(&self, r: &RunResult); }
pub struct TtyReporter; pub struct QuietReporter; pub struct JsonReporter;
```
`--json` object for `run`/`replay`/`resume`/`fork`:
`{"run_id": "…", "label": "autonomous_verified_success", "outcome": "complete|fail", "failure": {"kind", "message"} | null, "budget": {"tokens", "cost_usd", "tool_calls", "llm_calls", "wall_ms"}, "evidence": {"pass", "fail", "timeout", "skipped"}, "run_dir": "…"}`.
`--json` object for `check`: `{"diagnostics": [{"code", "severity", "message", "file", "line", "col", "end_line", "end_col"}], "effects": {"<item>": ["llm", …]}}`.

## Deliverables
- `crates/leash-cli/src/{main.rs, config.rs, exit.rs, ui.rs, commands/*.rs}`.
- `templates/` for `leash new`.
- `tests/cli/` with `trycmd` cases and `insta` snapshots.
- `docs/10-cli-and-tooling.md` updated for added or renamed flags; `docs/generated/cli-help.md` (checked-in `leash --help` output, refreshed by a test).

## Acceptance criteria
- `leash --help` and `leash <cmd> --help` for every command match `tests/cli/help/*.trycmd`.
- `LEASH_PROFILE=h1 leash run examples/01_hello.leash --driver anthropic=mock --profile h3 --json | jq -r .run_id` succeeds; `leash config show --json` reports `profile` from `Cli`, then from `Env` when the flag is dropped, then from `File` when the env var is unset.
- Exit codes: a program that `fail`s → 1; a syntax error → 2; `leash replay` after editing the program's `ask` prompt → 3; a `Budget` failure → 4; SIGINT in a test → 130 with the resume hint on stderr.
- `leash run … --gate deny` on a fixture that triggers a gate ends with `Permission` and an intervention entry `answer: false, source: policy`.
- `leash new demo --template fix-bug && cd demo && leash check` passes with zero diagnostics.
- `leash doctor` exits 0 on a machine with `claude` and `codex` installed and reports both versions; exits 1 and lists what is missing otherwise.
- `leash diff <a> <b>` for two runs differing in one mock answer prints exactly one changed effect id and the outcome delta; `--json` output validates against the shape above.
- `leash gc --keep-days 0` after `leash runs rm <id>` removes unreferenced CAS objects (count assertions in the test).
- `leash completions zsh` produces a script that `zsh -n` accepts.
- `cargo nextest run -p leash-cli` and `cargo clippy --all-targets -- -D warnings` clean.

## Notes and pitfalls
- Never print secrets: `doctor` and `config show` print presence only.
- Progress lines go to stderr when `--json` is set, stdout otherwise; the journal is never touched by UI concerns.
- `leash run --driver name=override` overrides the driver constructor defaults (model, effort), not the program's explicit arguments; say so in `--help`.
- Timestamps in `--json` are RFC 3339 UTC; run ids follow `docs/10` §3.
- Help text must be environment-independent ("default: from leash.toml").
- `resume` refuses when the working-copy snapshot for the last checkpoint is missing and explains how to `fork` instead.
- Render diagnostics with `miette`; keep `--json` diagnostics free of ANSI codes.

## Dependencies
Hard: WP-15 (outcome/report), WP-16 (failures, checkpoints, fork), WP-17 (profiles).  Soft: WP-11 (snapshot diffs), WP-20 (cancellation), WP-14 (`mcp refresh`), WP-23/24/25/26 (delegated subcommands).

## Estimated size
M (2–4 days).

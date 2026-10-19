# WP-27: Hermes driver (`ask` and `run`)

Milestone: M1 · Size: M · Status: see `docs/status.md`

## Goal
After this WP a Leash program can drive Hermes Agent as its first real harness.
`ask` sends one prompt and returns text or schema-validated JSON. `run` delegates
the tool loop to Hermes with translated capabilities, a scrubbed environment, a
bounded working copy and a post-check. Every call is journaled with the final
response and the usage report, so replay performs no subprocess.

## Read first
- `docs/adr/0004-drivers.md` (driver contract, priority, the `hermes` section)
- `docs/adr/0011-implementation-language-cpp.md` (C++23, Boost.Asio, Boost.Process)
- `docs/09-drivers-and-tools.md` §1 (the Driver interface this WP implements)
- `docs/03-runtime-architecture.md` §7 (effect gateway and handlers)
- `docs/07-budgets-and-capabilities.md` §5 (enforcement gaps for subprocess drivers)
- `docs/04-determinism-and-replay.md` (journal keys, child entries)
- `docs/subagents/shared-interfaces.md` (Driver types)
- `CLAUDE.md` (conventions)

## Scope
- `src/drivers/hermes/` implementing the `Driver` interface: argument
  construction, subprocess lifecycle, stdout and usage parsing, cancellation,
  and journal notes for every unsupported option.
- Subprocess: Boost.Process v2 child with pipes on the run's `io_context`.
  argv exactly as ADR-0004: `hermes -z <task> --usage-file <path> --in <workdir>`
  plus `-m`, `--provider`, `--reasoning`, `-t <toolsets>`,
  `--ignore-user-config`, `--ignore-rules`; `--worktree` when isolation is
  requested; `-r <session>` when a session is resumed.
- Environment: scrubbed to `PATH`, `LANG`, and the names listed in
  `CapSet.env`. Provider credentials come from `CapSet.secrets`. A missing
  credential is a `DriverError`, not a silent fallback.
- Capability translation: a static table from `CapSet` to Hermes toolsets
  (`-t`). A capability with no translation produces one journal note and does
  not fail the run. `human` and `spawn` have no translation in v1.
- `ask`: one prompt, final text on stdout. When a schema is present, the driver
  returns the raw text and the runtime validates it with valijson and re-asks.
- Usage: read the `--usage-file` JSON after exit, including on failure. Map to
  `Usage` (input and output tokens, cost, model, api_calls). A missing or
  malformed file yields zero usage plus a journal note.
- Sessions: when the program asks to keep a session, read the id from
  `hermes sessions list` after the run and journal it. If no machine-readable
  form exists, journal null plus a note. Resume with `-r`.
- Cancellation: `asio::cancellation_signal` sends SIGTERM, waits 10 s, then
  SIGKILL. The event is journaled.
- No `DriverEvent::ToolCall` or turn events in v1. The final response, the
  usage blob and the post-check diff are the child journal entries.
- `leash doctor` gains a `hermes` row: version or missing.
- Tests use a scripted `hermes` shim on `PATH`, not the real CLI.

## Out of scope
ACP mode (`hermes acp`), per-run MCP injection, streaming tool events,
Claude Code and Codex drivers, provider-specific pricing changes, retries
beyond the shared driver policy.

## Interfaces
```cpp
class HermesDriver final : public Driver {
 public:
  explicit HermesDriver(HermesConfig cfg);

  std::string id() const override;      // "hermes/<version>/<model>/<config hash>"
  DriverCaps caps() const override;

  asio::awaitable<std::expected<AskResponse, DriverError>>
  ask(const AskRequest& req, DriverCtx& ctx) override;

  asio::awaitable<std::expected<RunResponse, DriverError>>
  run(const RunRequest& req, DriverCtx& ctx, EventSink& sink) override;
};

struct HermesConfig {
  std::filesystem::path binary;   // default: "hermes" resolved on PATH
  std::string model;              // empty = Hermes default
  std::string provider;           // empty = Hermes default
  std::string reasoning;          // empty = Hermes default
  bool ignore_user_config = true;
  bool ignore_rules = true;
};
```

The `Driver`, `AskRequest`, `RunRequest`, `DriverCtx` and `EventSink` types
belong to WP-05. Implement against them; do not redefine them here.

## Deliverables
- `src/drivers/hermes/` sources and the `leash_drivers` CMake target entries.
- `tests/drivers/hermes/` with the shim, fixtures and unit tests.
- A `Hermes` section in `docs/09-drivers-and-tools.md` matching ADR-0004,
  including the enforcement-gap table row.
- `leash doctor` probe row.
- Journal schema notes for the `driver_gap` note kind.

## Acceptance criteria
- `leash run examples/01_hello.leash --driver hermes` prints the final text and
  journals a `Usage` with token counts and cost.
- `leash replay` of that run performs no subprocess and produces the same
  journal hash. Verified by the shim writing a sentinel file on every launch
  and the replay asserting the sentinel is unchanged.
- A run with `fs_write` limited to `src/**`, whose shim writes to `docs/`,
  fails the post-check and journals the violating path.
- A capability with no translation (`human`) yields one journal note and the
  run continues.
- Cancellation: SIGTERM within 1 s, child gone within 10 s, journal note
  present.
- `ctest --preset dev -LE network` passes with no real Hermes installed.
- `LEASH_TEST_NETWORK=1 ctest --preset dev -L network` runs one real-Hermes
  test and is skipped without credentials.

## Notes and pitfalls
- `hermes -z` auto-bypasses approval prompts. Never treat that as enforcement;
  Leash enforces through toolsets, the environment, the working copy and the
  post-check.
- `--usage-file` is written even when the run fails. Parse it in the failure
  path before returning the error.
- Hermes has no per-run MCP flag. Leash `tool` declarations are unavailable
  through this driver. Journal the gap once per run, not once per tool call.
- `--pass-session-id` puts the id in the system prompt, not stdout.
- The driver id includes `hermes --version`. Probe it once at construction and
  reuse it; flags move between releases.
- Do not read or write the operator's Hermes config. `--ignore-user-config` and
  `--ignore-rules` are always passed.

## Dependencies
Hard: WP-04. Soft: WP-05 (Driver interface and mock driver), WP-06 (run
directory and CAS).

## Estimated size
M (2–4 days).

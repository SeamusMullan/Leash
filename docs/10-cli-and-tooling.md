# CLI and Tooling

Status: draft v0.1. One binary: `leash`. Exit codes: 0 ok, 1 program failure
(`fail`), 2 usage/compile error, 3 replay divergence, 4 budget/permission
failure, 130 interrupted.

## 1. Commands

| Command | Purpose |
|---|---|
| `leash new <name> [--template fix-bug\|triage\|review]` | scaffold a package: `leash.toml`, `flows/main.leash`, `prompts/`, `.gitignore` for `.leash/runs` |
| `leash check [paths] [--strict] [--explain-effects] [--json]` | parse, resolve, type, effect and capability check; prints diagnostics with spans through the hand-written renderer in `src/syntax` |
| `leash fmt [paths] [--check]` | format in place / verify formatting |
| `leash ir <file> [--flow X] [--pretty]` | dump LIR JSON |
| `leash run <file> [--flow X] [--input <json\|@file>] [--profile h0..h3] [--driver name=override] [--gate deny\|approve\|fail] [--reuse <run\|all>] [--seed N] [--budget key=value…] [--dry-run]` | execute; prints run id, progress, outcome; writes the episode package |
| `leash replay <run> [--until <site\|seq>] [--verbose]` | re-execute from the journal; no model, no processes |
| `leash resume <run>` | replay then continue live |
| `leash fork <run> --at <checkpoint> [--file <modified.leash>]` | new run from a checkpoint |
| `leash trace <run> [--kind action\|tool\|context\|verification\|attribution\|intervention\|entropy\|outcome] [--budget] [--json]` | inspect traces |
| `leash report <run> [--regenerate]` | print or regenerate `report.md` |
| `leash diff <run> [--from cp] [--to cp]`; `leash diff <run-a> <run-b>` | working-copy diff between snapshots; journal diff between runs |
| `leash runs [list\|show <id>\|rm <id>]` | manage `.leash/runs` |
| `leash gc [--keep-days N]` | prune CAS and snapshots |
| `leash test [paths] [--reuse] [--update-golden]` | run `test` declarations and golden journals with the mock driver |
| `leash mcp refresh [server]`; `leash mcp-serve --run <id>` | refresh cached MCP schemas; internal per-run tool server for CLI drivers |
| `leash eval <suite.toml> [--profiles h0,h3] [--models …] [--out dir]` | run the harness ladder over a task suite and emit metrics |
| `leash doc [--format md\|json] [--out dir]` | extract `///` doc comments from `pub` items of a package (or `std`) into a reference; used to build the stdlib reference on the docs site |
| `leash lsp` | start the language server (stdio) |
| `leash doctor` | check drivers (`claude`, `codex` versions, API key presence), sandbox prerequisites, price table freshness |

Commands for the implementation itself are CMake's, not `leash`'s:
`cmake --preset dev && cmake --build build/dev` builds, `ctest --preset dev`
runs the Catch2 cases registered with CTest, `clang-format --dry-run --Werror`
checks formatting, and `clang-tidy -p build/dev` runs static analysis over the
compile database. `just check` runs the format check, clang-tidy and ctest.
`leash fmt` formats Leash source and `leash test` runs Leash programs; neither
one formats or tests the C++ sources.

## 2. Package manifest `leash.toml`

```toml
[package]
name = "fix-bugs"
version = "0.1.0"
entry = "flows/main.leash"

[drivers.anthropic]
model = "claude-opus-5"
effort = "high"
compaction = true
fallbacks = "default"          # for claude-fable-5-1 only

[drivers.claude_code]
model = "claude-opus-5"
permission_mode = "acceptEdits"

[profile]
default = "h3"

[caps.default]
"fs.read" = "**"

[budget.default]
cost = "usd 25"
wall = "1h"

[secrets]
provider = "env"

[prices]                        # overrides std/prices.toml
"claude-opus-5" = { input = 5.0, output = 25.0, cache_read = 0.5, cache_write = 6.25 }

[runs]
dir = ".leash/runs"
keep_days = 30
```

## 3. Run directory

```
.leash/
  runs/<run_id>/
    manifest.json
    journal.jsonl
    traces/{action,tool,context,verification,attribution,intervention,entropy}.jsonl
    traces/outcome.json
    artifacts/{patch.diff,task_state.json,report.md,…}
    worktree/            # or a path to a git worktree elsewhere
  cas/<hash[0..2]>/<hash>
  mcp/<server>.json
```
`run_id` = `<UTC timestamp>_<flow>_<6 hex>`.

## 4. Terminal UX

- Progress lines: `[00:12] ask attributor → Attribution (1.2k in / 220 out, $0.02)`;
  `[03:41] run coder … 17 tool calls, $0.95`; `[03:42] verify ensure: 3 pass, 1 timeout(recorded)`.
- `--quiet`/`--json` for machines. `--log debug` for runtime logs (stderr).
- Gates prompt on the TTY with the prompt text and the effect that requested it.
- Ctrl-C: cancel in-flight effects, journal `cancelled`, print how to `resume`.

## 5. Editor support

- LSP (`leash_lsp`, hand-written JSON-RPC 2.0 over stdio): diagnostics (compile),
  hover (type + effect set + inferred caps), go-to-definition, document
  formatting, code lens "Run flow" that invokes the CLI.
- VS Code extension in `editors/vscode/`: TextMate grammar, LSP client,
  journal viewer (webview reading `traces/`).
- Tree-sitter grammar in `editors/tree-sitter-leash/` (post-v1).

## 6. Testing programs

```leash
test "attribution is typed" with { drivers: { anthropic: drivers.mock(file("tests/mock/attr.json")) } } {
  let a: Attribution = ask attributor("...")
  assert a.kind == .Verify
}
```
`leash test` discovers `test` declarations, runs them with the mock driver and
the journal in memory, then replays to assert determinism. Golden journals:
`tests/golden/<name>/{program.leash, input.json, journal.jsonl, expected_outcome.json}`.

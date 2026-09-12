# Budgets, Capabilities and Security

Status: draft v0.1.

## 1. Budgets

`Budget { tokens, cost, wall, tool_calls, llm_calls, turns }`; unset means unlimited.

### 1.1 Scopes
- The task `budget` is the root scope of a flow.
- `budget { ... } { block }` opens a child scope. `ask`/`run` call options open a
  call scope. Agents may carry a default call budget.
- Entering a scope whose limits exceed the parent's **remaining** amounts fails
  immediately with `Budget`. Consumption is charged to every enclosing scope.
- `parallel`/`map` branches share the enclosing scope; use
  `parallel(budget: {...})` or per-branch `budget {}` blocks to partition.

### 1.2 Accounting
| Field | Source |
|---|---|
| `tokens` | driver usage: `input_tokens + output_tokens + cache_read_input_tokens + cache_creation_input_tokens` per turn |
| `cost` | computed from a price table (`std/prices.toml`, overridable in `leash.toml`) by model and token class; Claude Code's `total_cost_usd` result field is used when present and reconciled against the table |
| `wall` | monotonic clock from scope entry; enforced by cancellation |
| `tool_calls` | every child tool effect inside runs plus direct tool/shell effects |
| `llm_calls` | every `ask` and every model turn inside `run` |
| `turns` | model turns inside a single `run` (maps to `--max-turns` / `max_turns`) |

Checks happen before an effect (counters) and after each model turn (tokens,
cost). A single turn can overshoot `tokens`/`cost`; the overshoot is charged and
the next effect fails. For `anthropic` runs the runtime also passes a task
budget hint to the model (`output_config.task_budget`, beta) so it paces
itself; for Claude Code it passes `--max-budget-usd`.

### 1.3 Reporting
Every journal entry records `budget_after`. The outcome record and the report
include totals per scope. `leash trace <run> --budget` prints a tree of scopes
with spend.

## 2. Capabilities

```leash
allow {
  fs.read:  "**",
  fs.write: ["src/**", "test/**"],
  shell:    ["npm test*", "node harness/probe.js *"],
  net:      ["api.github.com"],
  human:    true,
  spawn:    true,
  env:      ["CI"],
  secrets:  ["GITHUB_TOKEN"],
}
```

### 2.1 Semantics
- Globs are relative to the run working copy; `..` segments are rejected;
  symlinks are resolved before matching; `[]` means none; `"**"` means all.
- `fs.write` implies `fs.read` for the same paths.
- `shell` patterns match the **full command line** after tokenisation with
  POSIX shell-words rules. Commands run **without a shell** (direct `execve`)
  unless the pattern itself starts with `sh -c ` or `bash -c `, in which case
  the pattern must match the entire script string. Pipes and redirects
  therefore require an explicit `sh -c` allowance.
- `net` is a list of hostnames; `*.example.com` allowed; empty means no network.
- `human: true` permits `gate`/`ask_human`. `spawn: true` permits concurrency.
- `env` lists environment variables visible to subprocesses (everything else is
  scrubbed). `secrets` lists secrets obtainable through `secrets.get` and
  injected into subprocess environments on demand; their values are redacted in
  the journal.

### 2.2 Narrowing
Scopes narrow monotonically. `leash check` proves subset relations when both
sides are literals; otherwise the runtime checks at scope entry and fails with
`Permission`. `deny { ... }` in a task subtracts from `allow` and cannot be
re-allowed by inner scopes.

### 2.3 Enforcement points
1. **Runtime tool layer** (authoritative): every built-in, Leash and MCP tool
   call is checked. API and mock drivers route all tool calls here.
2. **Sandbox**: process execution with a scrubbed env, cwd inside the working
   copy, path policy for the fs tools, timeouts and output caps.
3. **Driver translation** (cooperative): the runtime translates the active
   `CapSet` to the driver's own controls:

| CapSet | Claude Code | Codex | Anthropic API |
|---|---|---|---|
| `fs.read`/`fs.write` | `--add-dir` for extra roots; `--allowedTools "Read" "Edit" "Write"` or `--disallowedTools`; `--permission-mode` | `--sandbox read-only\|workspace-write`, `--cd`, `--add-dir` | runtime executes fs tools |
| `shell` | `--allowedTools "Bash(npm test*)" "Bash(node harness/probe.js *)"`; everything else disallowed; `--permission-prompts` routed to the runtime's gate policy | `--sandbox`; approval routing via `-c` config where available | runtime executes shell tool |
| `net` | not enforceable by the CLI; runtime warns; container backend needed for hard enforcement | `--sandbox` network policy where supported | runtime executes `http` |
| custom/MCP tools | exposed through a per-run MCP server (`--mcp-config`) with an allowlist (`--strict-mcp-config`) | MCP config via `-c` | runtime |
| `human` | permission prompts answered by runtime gate policy | approval prompts | n/a |

4. **Post-hoc diff check**: after every CLI-driven `run`, the runtime diffs the
   working copy; any change outside `fs.write` globs is reverted, journaled as a
   `Permission` violation and, by default, fails the run (configurable to
   `revert-and-continue`).

### 2.4 Profiles and defaults
`leash.toml` can set package-wide defaults (`[caps.default]`) and a `strict`
switch that makes missing `allow` blocks an error. Without a task `allow`,
the default is read-only (`fs.read: "**"`, everything else empty).

## 3. Human gates as a capability

`gate(prompt) -> Bool` and `ask_human(prompt) -> String` require `human: true`.
Interactive runs prompt on the TTY. Headless runs use `--gate <policy>`:
`deny` (default: every gate answers false), `approve` (dangerous; logged),
`fail` (a gate raises `Permission`), or `webhook:<url>` (post-v1). Every gate
is journaled and appears in the intervention log with `avoidable` unset; the
program or a later reviewer may classify it (`trace.intervention(id,
avoidable: true, gap: "context")`), which feeds the outcome label.

## 4. Secrets

- Never in source. `secrets.get("NAME")` reads from the environment or from
  `leash.toml`'s `[secrets] provider = "env" | "file:<path>" | "command:<cmd>"`.
- Tagged values propagate through string interpolation; any string containing a
  secret is redacted in the journal and traces (`secret:NAME:<hash8>`).
- Secrets are never sent to a model unless the program interpolates them into a
  prompt, which `leash check` flags with a warning and `--strict` forbids.

## 5. What the runtime cannot enforce (v1)

Be explicit with users about these limits:
- Network egress by subprocesses launched through CLI drivers on the process
  sandbox backend. Mitigation: warn at run start; container backend on the
  roadmap (`docs/12-roadmap.md`).
- A CLI driver's own internal reads outside the working copy (e.g. reading
  `~/.claude`). Mitigation: `--settings` / `--strict-mcp-config` where
  available; container backend later.
- Model behaviour. Leash bounds actions, not intentions.

## 6. Threat model (summary)

Assets: the target repository, credentials, the developer's machine, money.
Adversaries: a confused or prompt-injected model; a malicious dependency or MCP
server; a buggy program.
Mitigations: capability sets with default-deny for writes, shell, net, human;
no-shell command execution; scrubbed environments; secret redaction; budget
caps; post-hoc diff reverts; journaled, replayable evidence for audits; MCP tool
allowlists and schema pinning (`.leash/mcp/` cache with hashes; a changed
schema requires `leash mcp refresh`).

# Leash Language Tour

Status: draft v0.1. Read `docs/00-vision.md` first. The formal grammar is in
`spec/grammar.ebnf`; the semantics are in `docs/02-language-spec.md`. Runnable
examples are in `examples/`.

Leash looks like a small modern scripting language (Rust/TypeScript/Kotlin
flavoured). Most of it is ordinary code. A handful of keywords give it its purpose:
`agent`, `tool`, `task`, `flow`, `ask`, `run`, `spawn`, `parallel`, `race`,
`retry`, `attempt`/`recover`, `fail`, `checkpoint`, `verify`, `complete`.

## 1. Values, types, functions

```leash
let n = 3                       // Int
let ratio = 0.25                // Float
let name = "leash"              // String, interpolation with {}
let msg = "hello {name}, {n * 2}"
let tags = ["a", "b"]           // [String]
let pair = (1, "x")             // (Int, String)
let user = { id: 7, email: "s@x.io" }   // record
let maybe: Int? = null          // optional
let d = 30m                     // Duration literal (ms, s, m, h)
let price = usd(2.50)           // Money

fn clamp(x: Int, lo: Int, hi: Int) -> Int {
  if x < lo { lo } else if x > hi { hi } else { x }
}

let short = tags.filter(t => len(t) < 2)   // lambdas and UFCS method calls
```

Blocks are expressions. The last expression of a block is its value. `if`,
`match` and blocks all yield values. There is no `;` except to separate
statements on one line.

Named types and enums:

```leash
type Issue = { id: Int, title: String, body: String }

enum Severity { Low, Medium, High, Critical }

enum Outcome {
  Merged(pr: Int),
  Rejected(reason: String),
}

let s: Severity = .High                 // variant shorthand when the type is known
match outcome {
  .Merged(pr) => "merged #{pr}",
  .Rejected(reason) => "no: {reason}",
}
```

Refinement clauses attach validation predicates to fields. They are checked
whenever a value crosses an effect boundary (for example, when a model returns
it):

```leash
type Triage = {
  severity: Severity,
  summary: String where len(summary) <= 200,
  duplicate_of: Int? where duplicate_of == null or duplicate_of > 0,
}
```

## 2. Effects: the deterministic boundary

Every function has an effect set. Pure functions have the empty set and are
guaranteed deterministic. Effects are inferred; you may annotate them after `!`:

```leash
fn score(t: Triage) -> Int { ... }                       // pure, inferred {}
fn probe(cmd: String) -> String ! {shell} { shell(cmd).stdout }
fn classify(text: String) -> Triage ! {llm} { ask triager(text) }
```

The effect names are: `llm`, `tool`, `shell`, `fs.read`, `fs.write`, `net`,
`spawn`, `human`, `clock`, `rand`, `env`. A function may only call functions
whose effects are a subset of its own. `leash check` reports the inferred set of
every top-level item.

Why this matters: the runtime journals every effect. Because code between effects
is pure, a run can be replayed exactly from its journal. See
`docs/04-determinism-and-replay.md`.

## 3. Agents and drivers

An agent is a value: a driver, a system prompt, tools and defaults.

```leash
use std.drivers

agent coder = Agent {
  driver: drivers.claude_code(model: "claude-opus-5", effort: .High, permission_mode: .AcceptEdits),
  system: file("prompts/coder.md"),      // a resource reference; content hash goes in the journal
  tools: [fs.read, fs.write, fs.edit, shell],
  budget: { tokens: 200_000 },           // optional per-call defaults
}

agent judge = Agent {
  driver: drivers.anthropic(model: "claude-opus-5", effort: .High),
  system: "Answer only with the schema.",
}
```

Drivers are configuration, not code. `drivers.anthropic` talks to the Messages
API. `drivers.claude_code` and `drivers.codex` run the respective CLIs headless.
`drivers.mock` replays scripted answers for tests. See
`docs/09-drivers-and-tools.md`.

## 4. Asking and running

`ask` is one typed model call. The expected type decides the JSON schema the
driver is asked to produce. The result is validated (including refinements) and
re-asked a bounded number of times on failure.

```leash
let t: Triage = ask triager("Triage:\n{issue.body}")
let text = ask judge("Summarise: {t.summary}")          // no annotation -> String
let t2: Triage = ask triager("...", retries: 1, effort: .Low)
```

`run` is an agentic session: the driver may call tools until it is done. It also
returns a typed value. The transcript is captured in the journal and the tool
trace.

```leash
let report: RunReport = run coder(
  task: "Fix the bug. Change only src/validator.js.",
  context: select(repo, files: ["src/validator.js", "test/login.test.js"], max_tokens: 40_000),
  tools: [fs.read, fs.edit, shell],     // narrows the agent's tools for this run
)
```

`spawn` starts an `ask` or `run` in the background and returns a `Handle<T>`;
`await` joins it. Handles are structured: a scope waits for or cancels its
handles before it exits.

```leash
let h = spawn run coder(task: "...")
let other = ask judge("...")
let r = await h
```

## 5. Concurrency that replays

```leash
let (a, b) = parallel {                // tuple, branch order = syntactic order
  ask judge("A?"),
  ask judge("B?"),
}

let first = race {                     // first to finish; the journal records the winner
  ask fast("..."),
  ask slow("..."),
}

let triaged = map(issues, concurrency: 8) { issue =>   // list, input order preserved
  ask triager(issue.body)
}
```

All three forms are deterministic on replay: results are keyed by branch, not
by completion time.

## 6. Failures with kinds

Failures are values with a kind from a fixed taxonomy (see
`docs/05-harness-model.md`): `Context`, `Tool`, `Feedback`, `Verify`,
`Recovery`, `Entropy`, `Model`, `Budget`, `Permission`, `Replay`, `Unknown`.

```leash
if repro.stdout.contains("expected") { fail Verify("bug does not reproduce") }

let patch = attempt {
  run coder(task: "...")
  verify ensure
  repo.diff(since: "before-fix")
} recover Verify as e {
  trace.note("verification failed: {e.message}")
  fail e                                 // re-raise
} recover Budget as e {
  repo.diff(since: "before-fix")         // return what we have
}

let result = retry(3, on: Verify, backoff: .Exponential(2s)) {
  checkpoint "attempt"
  run coder(task: "...")
  verify ensure
  repo.diff(since: "attempt")
}
```

## 7. Tasks: the contract

A `task` is the contract. A `flow` with the same name implements it. Separating
the two lets several flows (say, an H0 and an H3 harness) implement one task, and
lets a meta-agent rewrite the flow while the contract, and the verifier, stay
fixed.

```leash
task FixEmptyPassword {
  input  { repo: Repo, issue: String }
  output Patch

  require { repo.is_clean() }

  ensure {
    check("empty password rejected")
      by shell("node harness/probe.js empty")
      expect stdout.contains("Password is required."),
    check("targeted tests pass") by test("npm test -- login") within 5m,
    check("full regression") by test("npm test") within 10m on_timeout .Record,
  }

  budget { tokens: 400_000, cost: usd(10), wall: 30m, tool_calls: 200 }
  allow  { fs.read: "**", fs.write: ["src/**", "test/**"], shell: ["npm test*", "node harness/probe.js *"], net: [] }
}

flow FixEmptyPassword(input) -> Patch {
  ...
  complete patch     // fails with Verify unless every ensure check has passing evidence
}
```

- `require` runs before the flow. A false predicate fails with `Context`.
- `ensure` declares named checks. `verify ensure` runs them and records
  `Evidence`. `complete` refuses to finish without evidence for every check.
- `budget` and `allow` bound the whole flow. Inner `budget {...} {...}` and
  `allow {...} {...}` blocks may only narrow them (write `allow (expr) {...}`
  when the set is not a literal).

## 8. Context and memory

Models only see what the program selects. `select` builds a `Context` value with
a deterministic, sorted, hashed layout (good for prompt caching) and records a
context trace entry.

```leash
memory guide = file("AGENT_GUIDE.md")
memory arch  = file("docs/ARCHITECTURE.md")

let ctx = select(repo,
  files: attr.evidence,
  globs: ["src/auth/**"],
  memory: [guide, arch],
  max_tokens: 60_000,
)
```

A flow may declare `state { ... }`: typed mutable task state that is snapshotted
at every checkpoint and written to the episode package.

## 9. Tools

Built-in tools (`fs.*`, `shell`, `git`, `test`, `http`) run inside the runtime
sandbox under the active capability set. Custom tools are Leash functions with a
doc comment (the description) and typed parameters (the schema):

```leash
/// Run the project's linter on the given paths and return findings.
tool lint(paths: [String]) -> [String] ! {shell} {
  shell("npm run lint -- {paths.join(" ")}").stdout.lines()
}
```

MCP tools are imported with their schemas:

```leash
let gh = mcp.connect(stdio("npx -y @modelcontextprotocol/server-github"))
agent scout = Agent { driver: ..., tools: [gh.tool("search_code"), lint] }
```

## 10. Human gates and interventions

```leash
if not gate("Push branch fix/empty-password to origin?") {
  fail Permission("operator declined push")
}
```

`gate` is a `human` effect. Interactive runs prompt in the terminal; headless
runs consult the gate policy (`--gate deny|approve|fail`). Every gate is recorded
in the intervention log with its answer.

## 11. What a run produces

`leash run examples/02_fix_bug.leash --input '{...}'` creates
`.leash/runs/<id>/` with the journal, eight trace files, artifacts and a
verification report. `leash replay <id>` re-executes the program from the journal
without a model. `leash resume <id>` continues a crashed run. `leash fork <id>
--at before-fix` starts a new run from a checkpoint. See
`docs/11-episode-package-and-eval.md`.

## 12. A complete program

See `examples/02_fix_bug.leash`. It is the reproduce → attribute → fix → verify →
report workflow from the AI Harness Engineering paper, written as ~90 lines of
Leash. Everything a reviewer needs to know about what the model was allowed to do,
how much it could spend and what "done" meant is visible in the `task` block.

# Verification and Evidence

Status: draft v0.1. Source principles: "AI Harness Engineering" (P3
requirement-level verification, P4 attribution before recovery, the H3
reproduce → attribute → fix → verify → report discipline).

## 1. Principle

Completion is an evidentiary object, not an assertion. A Leash flow ends with
`complete value`, and `complete` succeeds only if every requirement declared in
the task's `ensure` block has fresh, passing evidence produced by the runtime.
The model may claim anything; the runtime decides whether the claim is backed.

## 2. Checks

A check binds a human-readable requirement to a method that produces
observable output and a predicate over that output.

```leash
ensure {
  check("empty password rejected")
    by shell("node harness/probe.js empty")
    expect stdout.contains("Password is required."),
  check("targeted tests pass")   by test("npm test -- login") within 5m,
  check("full regression")       by test("npm test") within 10m on_timeout .Record,
  check("no new dependencies")   by no_dependency_churn(repo),          // a pure/effectful fn returning Bool
  check("patch is minimal")      by judge(reviewer, "Is the diff minimal?\n{repo.diff()}"),   // LLM judge
}
```

Method kinds and the evidence they produce:

| Method | Deterministic | Result fields bound in `expect` | Default predicate |
|---|---|---|---|
| `shell(cmd)` | yes (modulo the command) | `stdout stderr exit_code duration` | `exit_code == 0` |
| `test(cmd)` | yes | `passed failed skipped output exit_code duration` | `failed == 0 and exit_code == 0` |
| `lint(cmd?)` | yes | `findings exit_code` | `len(findings) == 0` |
| `fn(...)` returning `Bool` | as the fn's effects | `value` | `value` |
| `fn(...)` returning a record | as the fn's effects | the record's fields | must be given |
| `judge(agent, prompt)` | **no** | `verdict: Bool, rationale` | `verdict` |

`test(cmd)` parses common runner outputs (Jest, Vitest, pytest, cargo test, go
test, JUnit XML) into counts; unknown runners fall back to exit code with a
warning in the evidence entry.

## 3. Evidence

```json
{ "check": "targeted tests pass", "requirement": "targeted tests pass",
  "method": {"kind": "test", "cmd": "npm test -- login"},
  "result": "pass",                    // pass | fail | timeout | skipped | error
  "deterministic": true,
  "confidence": "high",                // high (deterministic sensor) | medium (heuristic) | low (judge)
  "scope": "login unit tests only; no end-to-end coverage",   // what this evidence does NOT establish
  "output_ref": "b3:…",                // CAS pointer to full output
  "excerpt": "Tests: 12 passed, 12 total",
  "duration_ms": 8420,
  "effect_id": "b3:…",                 // journal entry that produced it
  "repo_state": "refs/leash/<run>/<seq>",  // snapshot the check ran against
  "at_seq": 118 }
```

Evidence is appended to the verification trace and to the flow's evidence set.

## 4. Freshness

Evidence is **stale** if the repository changed after it was produced. The
runtime tracks the sequence number of the last mutating effect (`fs.write`,
`run` with write capability, `git` mutations). `complete` only counts evidence
with `at_seq` greater than that number. Practical consequence: `verify ensure`
is the last effect before `complete` in almost every flow; the `retry` pattern
in `examples/02_fix_bug.leash` shows the shape.

## 5. The `complete` rule

`complete value` succeeds iff:
1. `value` matches the task output type (and refinements);
2. every `ensure` check has fresh evidence;
3. each such evidence is `pass`, or `timeout` under `on_timeout .Record`, or
   `skipped` under `.Skip`;
4. all spawned handles are joined;
5. no `deny`-listed capability was exercised (belt and braces; the runtime
   already blocks these).

Otherwise `complete` fails with `Verify` and a report of which requirements are
unmet, stale, or non-deterministic. The flow may `attempt`/`recover` around
`complete` to try again.

## 6. Outcome classification

Computed by the trace layer from the journal (paper's five-label taxonomy):

| Label | Rule |
|---|---|
| `autonomous_verified_success` | `complete` succeeded; all evidence deterministic and passing (timeouts recorded allowed); no human intervention marked `avoidable` |
| `assisted_verified_success` | as above, but ≥1 intervention marked `avoidable` (a gate answer or `ask_human` that supplied context, a diagnosis or a decision the harness should have provided) |
| `unverified_success` | the flow completed with `judge`-only or skipped evidence for ≥1 requirement, or `complete` was bypassed with `--allow-unverified` |
| `failed` | the flow ended with `fail` (any kind except Permission/Entropy on unsafe grounds) |
| `unsafe_invalid` | the entropy audit found test weakening or out-of-scope destructive edits; or a `Permission` failure occurred after the model attempted a denied destructive action |

The label and its justification go to `traces/outcome.json`.

## 7. Verification report

`report.md` is generated from the verification trace:

```
# Verification report — run 2026-09-12T10-00-00_fix_bug

Task: FixEmptyPassword     Flow: examples.fix_bug.FixEmptyPassword     Profile: h3
Outcome: autonomous_verified_success

| Requirement | Method | Result | Deterministic | Duration | Evidence |
|---|---|---|---|---|---|
| empty password rejected | shell node harness/probe.js empty | pass | yes | 0.4s | b3:… |
| targeted tests pass | test npm test -- login | pass | yes | 8.4s | b3:… |
| full regression | test npm test | timeout (recorded) | yes | 600s | b3:… |

Limitations: full regression timed out at 10m; recorded per policy, not treated as success.
Attribution: kind=Verify, layer=validator, evidence=[src/validator.js:41]
Interventions: none
Entropy: 0 items
Budget: tokens 143,210 / 400,000 · cost $3.12 / $10 · tool calls 41 / 200 · wall 11m / 30m
```

The template is `std/templates/report.md` and can be overridden per package.

## 8. Attribution before recovery

Leash does not force a program structure, but the standard pattern and the H3
profile enforce this ordering: observe failure → record `Attribution` (via
`trace.attribution` or the typed `Attribution` result of an `ask`) → edit. The
attribution trace lets an evaluator judge the diagnosis independently of the
patch. `std.patterns.fix_verify_report` encodes the loop with a back-edge from
verification to re-attribution when a check fails.

## 9. Judges

`judge(agent, prompt)` is allowed as a check method because some requirements
are not mechanically checkable. Its evidence is marked non-deterministic and
caps the outcome at `unverified_success` unless the task declares
`judge_policy: .AcceptAsEvidence` (documented as a lower assurance level in the
report). Judge prompts and verdicts are journaled like any `ask`.

## 10. Entropy audit

`audit.entropy(repo, since: checkpoint) -> EntropyReport` runs deterministic
heuristics over the diff: new untracked files not referenced anywhere; deleted
or skipped tests (`it.skip`, `@pytest.mark.skip`, `#[ignore]`); assertions
removed; dependency manifest changes; debug prints; TODO/FIXME added;
documentation referencing removed symbols. Each item has a category (`code
docs dependency test residue architecture workflow`) and severity 0–3. The
audit runs automatically before `complete` (configurable), writes the entropy
trace, and can be bound to a check: `check("no entropy") by audit.entropy(repo,
since: "start") expect max_severity <= 1`.

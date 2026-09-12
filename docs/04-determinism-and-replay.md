# Determinism, Journal and Replay

Status: draft v0.1. This is the most important runtime document. Read it before
touching `leash-runtime`.

## 1. Definitions

- **Deterministic program**: a Leash program whose only sources of
  non-determinism are effects. The effect system (spec §5) guarantees this
  statically: pure code cannot observe time, randomness, files, network,
  models or environment.
- **Journal**: the ordered log of every effect performed in a run, with inputs,
  outputs and metadata.
- **Replay**: re-executing a program while serving every effect from the
  journal instead of the world. No model is called, no command runs.
- **Resume**: replay up to the end of the journal, then continue live.
- **Fork**: replay up to a checkpoint, discard the rest, continue live, possibly
  with a modified program.
- **Divergence**: during replay, the program asks for an effect that does not
  match the journal. This is always a bug or an intentional program change.

The model is the one used by durable-workflow engines (Temporal, Azure Durable
Functions): re-execution plus a log of effect results. Leash adds a static
guarantee that the code between effects is pure, so divergence cannot come from
accidental non-determinism in user code.

## 2. Effect identity

Every effectful LIR node has a `site_id` (stable across runs and formatting:
derived from module path, item name, and the node's path inside the item).

At run time each effect gets an **effect path**:

```
effect_path = [ (site_id, occurrence) for each enclosing activation ] + [(site_id, occurrence)]
```

- `occurrence` counts how many times the same site has been reached within the
  same enclosing activation (loop iterations, recursion).
- An **activation** is a flow call, a function call, a lambda invocation, or a
  branch of `parallel`/`race`/`map`/`spawn`. Branch activations contribute
  `(site_id_of_construct, branch_index)`, never completion order.

```
effect_id = blake3(canonical_json(effect_path))
```

Example: in `retry(3) { run coder(...) }` the three attempts have paths ending
`(retry_site, 0)/(run_site, 0)`, `(retry_site, 1)/(run_site, 0)`,
`(retry_site, 2)/(run_site, 0)`. In `map(issues, concurrency: 8) { ask ... }`
item `i` has `(map_site, i)/(ask_site, 0)` regardless of which finishes first.

Property: for a deterministic program and identical inputs, the multiset of
effect ids is identical on every execution, and the **order of the journal is
irrelevant for replay** (lookup is by id). Order is kept for humans and traces.

## 3. Journal entry

One JSON object per line. `spec/journal.schema.json` is normative.

```json
{
  "seq": 42,
  "id": "b3:9f2c…",                       // effect_id
  "path": [["examples.fix_bug::FixEmptyPassword#retry@L61", 1], ["…#run@L66", 0]],
  "kind": "llm.run",                       // llm.ask | llm.run | tool | shell | fs.read | fs.write | net | human | clock | rand | env | checkpoint | spawn | race | complete | fail
  "site": {"file": "examples/02_fix_bug.leash", "line": 66, "col": 5},
  "input_hash": "b3:…",                    // hash of canonical input (prompt, schema, driver config, context hash…)
  "input": { "…": "small inputs inline; large ones as {\"cas\": \"b3:…\"}" },
  "output": { "…": "same rule" },
  "status": "ok",                          // ok | failed | cancelled | timeout
  "failure": null,                         // Failure object when status != ok
  "started_at": "2026-09-12T10:00:00.123Z",
  "ended_at":   "2026-09-12T10:03:41.009Z",
  "usage": {"input_tokens": 12000, "output_tokens": 2400, "cache_read_input_tokens": 9000, "cost_usd": "0.2450", "tool_calls": 17},
  "driver": {"id": "anthropic", "model": "claude-opus-5", "config_hash": "b3:…", "version": "…"},
  "caps": {"fs.read": "**", "fs.write": ["src/**"], "shell": ["npm test*"]},
  "budget_after": {"tokens": 388000, "cost_usd": "9.7550", "tool_calls": 183},
  "children": 17,                          // number of child effects (tool calls inside a run)
  "parent": "b3:…",                        // for child effects
  "note": "temperature ignored by driver"  // free-form, driver omissions etc.
}
```

Rules:
- The entry is written with `status: "started"` before performing, then
  rewritten as a new line with the final status. Replay uses the last line for
  an id. This makes crashes during long effects visible and resumable.
- `input_hash` covers everything that could change the result: prompt text,
  rendered context hash, schema hash, system prompt hash, driver config hash,
  tool set hash, capability set. It does not cover budget state.
- Payloads above 8 KiB go to the CAS. CAS objects are immutable and shared
  across runs (`.leash/cas/`), so repeated contexts cost nothing.
- Secrets are replaced by `secret:<name>:<hash8>` before hashing and writing.

## 4. Replay algorithm

```
mode ∈ {live, replay, resume, fork(cp)}
index := load journal → map id → last entry
for each effect request e (id, input_hash):
  match index.get(id):
    Some(entry) if entry.status == ok:
        if entry.input_hash != e.input_hash:
            if mode == fork and e.path is after cp: perform live
            else: fail Replay("input changed at <site>: <diff of inputs>")
        return entry.output
    Some(entry) if entry.status in {failed, cancelled, timeout}:
        if mode == replay: re-raise entry.failure
        else: perform live (resume retries the failed effect)
    None:
        if mode == replay: fail Replay("journal has no entry for <site>; program diverged")
        else: perform live, append
```

"After cp" is defined by journal `seq` of the checkpoint entry: effects whose
id was first journaled after that `seq` are discarded on fork.

Replay never contacts a driver, never spawns a process, never prompts a human.
`leash replay` therefore runs anywhere, including CI, with no credentials.

## 5. What is captured, and how

| Source of non-determinism | Capture |
|---|---|
| Model output (`ask`, `run`) | full response (+ transcript in CAS), usage, model id, stop reason |
| Tool calls inside a `run` | child entries: name, input, output, duration, exit code |
| Shell/test commands | argv, cwd, env allowlist, stdout/stderr (CAS), exit code, duration, timeout flag |
| File reads (`fs.read`, `file(...)`, `select`) | content hash; content in CAS |
| File writes | path, content hash after write; the diff is captured at the next checkpoint or at `complete` |
| Time | `clock.now()` value; `sleep` records requested and actual duration; replay does not sleep |
| Randomness | seed per run (`manifest.seed`); `random()` values journaled anyway for clarity |
| Environment | values of allowed vars (secrets redacted) |
| `race` | winner branch index; losers' cancellation entries |
| Cancellation / timeouts | status field |
| Human gates | prompt, answer, who answered (tty / policy / api), latency |
| Driver behaviour differences | `note` field; driver version in manifest |

What is **not** journaled: the state of the outside world between effects.
Leash journals the *observations* the program made, which is all a
deterministic program can depend on. For `resume` and `fork`, the working copy
must also be restored: the sandbox snapshots it at every checkpoint (§7).

## 6. Divergence handling

Divergence means the program requested an effect the journal cannot serve. The
runtime fails with `Replay` and prints: the site, the expected vs actual input
diff (for changed inputs), and the nearest preceding checkpoint. Users can then
`leash fork <run> --at <checkpoint>` to continue from the last good point with
the changed program. `leash replay --until <site>` supports bisecting.

Anti-patterns that cause divergence are compile errors or lints:
- iterating a `Map` in insertion order (Leash sorts keys);
- reading `clock.now()` in pure code (impossible: it is an effect);
- using a `Handle` across an effect boundary (type error).

## 7. Working-copy snapshots

Each run gets a sandbox working copy (git worktree from the input repository's
commit). At every `checkpoint`, at every `complete`/`fail`, and after every
`run`, the sandbox commits the working tree to `refs/leash/<run_id>/<seq>`
(untracked files included, `.leash/` excluded). `fork --at cp` checks out the
snapshot for `cp`. `leash diff <run> --from cp1 --to cp2` shows what a phase
changed. Snapshots are cheap (git objects are deduplicated) and are garbage
collected by `leash gc`.

## 8. Memoisation across runs

Independent of replay: `leash run --reuse <run_id | all>` serves an effect from
an earlier run when `(kind, input_hash)` matches and the effect is marked
reusable. `ask` and `fs.read` are reusable by default; `run`, `shell`, `test`,
`human` are not (their results depend on world state) unless annotated at the
call site with `reuse: true`. Reused entries are journaled with
`"reused_from": "<run_id>"`. This turns repeated experiments over the same
inputs into near-free operations and is the basis for `leash test` fixtures.

## 9. Manifest and versioning

`.leash/runs/<id>/manifest.json`:
```json
{ "run_id": "…", "created_at": "…", "leash_version": "0.1.0", "journal_version": 1, "lir_version": 1,
  "program": {"entry": "examples/02_fix_bug.leash", "flow": "FixEmptyPassword", "lir_hash": "b3:…", "source_hashes": {…}},
  "inputs_hash": "b3:…", "seed": 1234, "profile": "h3", "mode": "live",
  "drivers": {"anthropic": {"version": "api-2023-06-01", "models": ["claude-opus-5"]}, "claude_code": {"version": "2.1.269"}},
  "repo": {"path": "…", "head": "…", "worktree": "…"},
  "parent_run": null, "forked_at": null }
```
A journal is replayable only by a runtime with the same `journal_version` and a
program with the same `lir_hash` (or a fork). `leash replay` refuses otherwise
unless `--force`.

## 10. Testing the guarantee

- **Round-trip test** (every integration test): run live with the mock driver →
  replay → assert identical outcome, evidence and derived traces.
- **Golden journals** under `tests/golden/`: the checked-in journal must replay
  against the checked-in program; CI fails on divergence, which is how
  accidental semantic changes to the interpreter are caught.
- **Chaos test**: kill the runtime at random points during a mock run, `resume`,
  and assert the final outcome equals an uninterrupted run.
- **Property**: shuffling journal lines does not change replay results (lookup
  is by id).

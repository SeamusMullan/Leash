# ADR-0002 Event-sourced execution with replay-by-re-execution

Status: accepted.

## Context
Runs must be reproducible, resumable after crashes, forkable at checkpoints and
auditable. Two designs exist: (a) serialise interpreter state at every effect
(continuations/snapshots); (b) journal effect results and re-execute the
deterministic program to rebuild state (Temporal-style).

## Decision
(b). The journal of effect results is the only persisted execution state.
Replay re-runs the program and serves effects from the journal keyed by a
structural effect id. The effect system (ADR-0003) guarantees the code between
effects is deterministic, which makes (b) sound.

## Alternatives
(a) requires a serialisable interpreter state including closures and pending
tasks; complex and version-fragile. Hybrid (snapshot + journal) can come later
as an optimisation for very long runs.

## Consequences
- Programs must be deterministic between effects: enforced by the type system,
  sorted map iteration, seeded randomness as an effect, no wall-clock in pure code.
- Journal keys must be structural (branch and iteration indices), never
  time-ordered. See `docs/04-determinism-and-replay.md`.
- Replay cost is proportional to pure computation, which is small.
- The working copy is not part of the journal; snapshots at checkpoints cover
  `fork`/`resume`.

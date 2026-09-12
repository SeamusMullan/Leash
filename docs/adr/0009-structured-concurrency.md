# ADR-0009 Structured concurrency with branch-indexed journaling

Status: accepted.

## Decision
`parallel`, `race`, `map(concurrency:)` and `spawn/await` are the only
concurrency forms. Branches are isolated (no shared mutable state), joined or
cancelled at block exit, and journaled under paths that include the syntactic
branch index. `race` journals its winner. Failure propagation is by branch
order after settlement.

## Alternatives
Actors/channels (powerful, hard to replay); shared mutable state with locks
(non-deterministic); unstructured tasks (leaks, cancellation bugs).

## Consequences
Deterministic replay of concurrent programs falls out of the journal key
design; some coordination patterns (blackboards) wait for the transactional
state milestone.

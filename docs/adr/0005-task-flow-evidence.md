# ADR-0005 Task/flow split and evidence-bound completion

Status: accepted.

## Context
The papers argue completion must be evidence, and that harnesses should be
evaluable and evolvable against a fixed verifier.

## Decision
`task` declares the contract (input, output, require, ensure, budget, allow).
`flow` implements it. `complete` succeeds only when every `ensure` check has
fresh, passing (or policy-accepted) evidence. The outcome label is computed by
the runtime, never asserted by the program or the model.

## Alternatives
A single `task` construct with a body (simpler, but conflates verifier and
harness); postconditions as plain assertions (no evidence records, no
freshness, no report).

## Consequences
- Several flows can implement one task (ladder experiments, evolved flows).
- Some ceremony for trivial programs; `output Unit` with no `ensure` keeps
  `complete` cheap.
- The freshness rule forces `verify` to be the last effect before `complete`.

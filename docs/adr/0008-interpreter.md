# ADR-0008 Tree-walking interpreter first

Status: accepted.

## Decision
The v1 runtime is an async tree-walking evaluator over LIR. No bytecode, no
JIT. Effects dominate run time; interpretation cost is negligible. Replay of a
10k-effect journal in under 2 s is the only performance target.

## Consequences
Simple to implement and debug by agents; leaves room for a VM if pure
computation ever matters (unlikely for harness programs).

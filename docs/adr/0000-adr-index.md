# Architecture Decision Records

Format: context, decision, alternatives, consequences. Status is one of
proposed / accepted / superseded. New ADRs get the next number.

| # | Title | Status |
|---|---|---|
| 0001 | Implementation language: Rust | superseded by 0011 |
| 0002 | Event-sourced execution with replay-by-re-execution | accepted |
| 0003 | Static effect system for determinism | accepted |
| 0004 | Drivers as configuration; runtime owns the tool loop where possible | accepted (revised 2026-10-19: Hermes driver, priority order) |
| 0005 | Task/flow split and evidence-bound completion | accepted |
| 0006 | LIR as a canonical JSON representation | accepted |
| 0007 | Capability model: default-deny, no-shell execution, cooperative driver translation | accepted |
| 0008 | Tree-walking interpreter before any bytecode VM | accepted |
| 0009 | Structural concurrency with branch-indexed journaling | accepted |
| 0010 | Licence: Apache-2.0, with headers as policy | accepted |
| 0011 | Implementation language: C++23 | accepted (Sarah: C++23, Boost.Asio) |

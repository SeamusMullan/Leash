# Leash

A programming language for harnessing coding agents.

Leash code decides **control flow, budgets, permissions and verification**.
Models decide **content**. Every model or tool interaction is a typed,
journaled effect, so a Leash run is deterministic, replayable, resumable and
auditable.

```leash
task FixEmptyPassword {
  input  { repo: Repo, issue: String }
  output Patch
  ensure {
    check("empty password rejected") by shell("node harness/probe.js empty") expect stdout.contains("Password is required."),
    check("targeted tests pass")     by test("npm test -- login") within 5m,
  }
  budget { cost: usd(10), wall: 30m, tool_calls: 200 }
  allow  { fs.read: "**", fs.write: ["src/**", "test/**"], shell: ["npm test*", "node harness/probe.js *"] }
}

flow FixEmptyPassword(input) -> Patch {
  let repro = shell("node harness/probe.js empty")
  let attr: Attribution = ask attributor("Observed: {repro.stdout}\nExpected: 'Password is required.'")
  let patch = retry(3, on: Verify) {
    checkpoint "before-fix"
    run coder(task: "Fix: {input.issue}. Root cause: {attr.next_action}", context: select(input.repo, files: attr.evidence))
    verify ensure
    input.repo.diff(since: "before-fix")
  }
  complete patch
}
```

## Status

Design phase (September 2026). No code yet. The language specification, runtime
architecture, grammar, ten architecture decision records and 26 work-package
briefs are in `docs/`. Every design decision is settled and the first work
package is ready to implement; `docs/status.md` tracks progress.

Implementation language is Rust, chosen over Go and TypeScript because its only
gap, a hand-written Anthropic HTTP client, is a one-time cost rather than a
recurring one (`docs/adr/0001-implementation-language.md`).

Start with `docs/00-vision.md`, then `docs/01-language-tour.md`, then
`docs/12-roadmap.md`.

## Documents

| | |
|---|---|
| `docs/00-vision.md` | problem, thesis, goals, non-goals |
| `docs/01-language-tour.md` | the language by example |
| `docs/02-language-spec.md` · `spec/grammar.ebnf` | semantics and grammar |
| `docs/03-runtime-architecture.md` | compiler pipeline, interpreter, effect gateway, crates |
| `docs/04-determinism-and-replay.md` | journal, effect ids, replay/resume/fork |
| `docs/05-harness-model.md` | the eleven harness responsibilities, profiles H0–H3 |
| `docs/06-verification-and-evidence.md` | checks, evidence, `complete`, outcome labels |
| `docs/07-budgets-and-capabilities.md` | budgets, capability sets, sandbox, threat model |
| `docs/08-multi-agent.md` | roles, patterns, topologies, shared state |
| `docs/09-drivers-and-tools.md` | Anthropic API, Claude Code, Codex, mock, MCP, built-in tools |
| `docs/10-cli-and-tooling.md` | `leash` commands, manifest, run directory, editor support |
| `docs/11-episode-package-and-eval.md` | traces, metrics, suites |
| `docs/12-roadmap.md` | milestones, work packages, risks, open decisions |
| `docs/adr/` | architecture decision records |
| `docs/research/` | notes on the three reference papers |
| `docs/subagents/` | work-package briefs for implementers |

## Licence

Apache License 2.0, SPDX identifier `Apache-2.0`. See `LICENSE`, and
`docs/14-licensing.md` for the file
header policy and the dependency licence allowlist. Contributions are accepted
under section 5 of the licence with a Developer Certificate of Origin sign-off,
`git commit -s`. Why this licence and not a copyleft or source-available one is
recorded in `docs/adr/0010-licensing.md`.

## Grounding

The design follows three papers, listed with arXiv links in
`reference_paper/README.md`: *AI Harness Engineering*
(Zhong & Zhu, 2026), *Code as Agent Harness* (Ning et al., 2026) and *SIA:
Self-Improving AI with Harness & Weight Updates* (Hebbar et al., 2026). See
`docs/research/paper-notes.md`.

# Research Notes: What the Reference Papers Say and How Leash Uses Them

Status: v0.1. The three papers are listed with arXiv links in
`reference_paper/README.md`; the PDFs are not redistributed here. A full digest of the
102-page survey is in `docs/research/digest-code-as-agent-harness.md`.

## 1. AI Harness Engineering: A Runtime Substrate for Foundation-Model Software Agents
Zhong & Zhu, arXiv 2605.13357 (May 2026), 16 pages.

**Claim.** Software-engineering capability is a property of the
model–harness–environment system, `C_system = F(C_model, C_harness, C_env, T)`.
The harness is a *runtime substrate*, a research object distinct from prompts,
agent frameworks, agent–computer interfaces and agent operating systems.

**Contributions used by Leash.**
- Eleven component responsibilities (task interface, context manager, tool
  registry, project memory, task state, observability, failure attribution,
  verification protocol, permission boundary, entropy auditor, intervention
  logger) → `docs/05-harness-model.md` §1 maps each to a construct.
- Five design principles: explicit runtime resources (P1), traceable mediation
  (P2), requirement-level verification (P3), attribution before recovery (P4),
  entropy awareness (P5). Leash's tie-breakers in `docs/00-vision.md` are these
  principles restated.
- Failure taxonomy F_context, F_tool, F_feedback, F_verify, F_recovery,
  F_entropy, F_model, F_unknown → `FailureKind`.
- H0–H3 harness ladder with a visibility matrix → profiles in
  `docs/05-harness-model.md` §3.
- Eight trace types (action, tool, context, verification, failure attribution,
  intervention, entropy, outcome) and the episode package → `docs/11-episode-package-and-eval.md`.
- Five outcome labels (autonomous_verified_success, assisted_verified_success,
  unverified_success, failed, unsafe_invalid) → computed by the trace layer.
- Metrics: AVSR, M-HIR, verification autonomy, context-trace meaningfulness,
  tool recovery rate, failure attribution completeness, entropy delta → `leash eval`.
- H3 workflow reproduce → attribute → fix → verify → report with a back-edge →
  `examples/02_fix_bug.leash` and `std.patterns.fix_verify_report`.
- "A full-regression timeout does not by itself prevent
  autonomous_verified_success when deterministic requirement coverage is
  complete and the limitation is reported" → `on_timeout .Record`.

**Where Leash goes further.** The paper instantiates the harness as files in a
`harness/` directory plus protocols in prompts. Leash makes the same
responsibilities executable and enforced: `complete` cannot succeed without
evidence, budgets and permissions are enforced at effect boundaries, and every
trace is derived from a replayable journal rather than recorded by convention.

## 2. Code as Agent Harness: Toward Executable, Verifiable, and Stateful Agent Systems
Ning et al. (UIUC/Meta/Stanford), arXiv 2605.18747 (May 2026), 102 pages, survey.

**Claim.** Code is the operational substrate for agent reasoning, acting,
environment modelling and verification. A harness is "the software layer that
surrounds an LLM with tools, APIs, sandboxes, memory, validators, permission
boundaries, execution loops, and feedback channels".

**Ideas used by Leash.**
- The model/harness split: "The model provides judgment … the harness …
  constrains actions through policies and permission tiers, routes feedback,
  and verifies whether each state transition is acceptable." This is the
  `ask`/`run` (judgment) versus code (control) split.
- Plan–Execute–Verify with planning as contract formation → the `task` block
  is the contract; `docs/05-harness-model.md` §2.
- Permission tiers keyed on arguments and side effects → `allow.shell` patterns
  over full command lines; dedicated tools over bash for gating.
- Tool lifecycle hooks (pre/post) → tool middleware in the runtime.
- "Termination should be governed by verification rather than by model
  confidence" → the `complete` rule.
- Reproducibility via sandboxes that replay "the same patch, command, seed,
  dependency lockfile, or test configuration" → journal + working-copy snapshots.
- Deep telemetry (prompts, context, cost, latency, tool args, permissions,
  edits, snapshots, test results, branch decisions, rejected alternatives,
  human interventions) → the journal schema.
- Failure modes: mode collapse (agent's own tests pass its own bug) →
  independent test design pattern; oracle inadequacy → Evidence `scope` and
  `confidence` fields; implicit convergence → every stdlib pattern has an
  explicit criterion; state divergence → single shared working copy with
  serialised writers, worktrees for parallel edits.
- Change contracts for harness mutation and the AHE loop → `docs/05-harness-model.md` §6, roadmap M10.
- Transactional shared program state (read/write sets, verifier obligations,
  conflict policy) → roadmap M11.
- Harness-level metrics (trajectory efficiency, verification strength,
  recovery ability, state consistency, safety compliance, replayability) →
  `leash eval`.
- Interfaces: MCP as the tool protocol; AGENTS.md-style repository guidance →
  `memory` resources; CodeAct-style code actions are the domain of drivers,
  not of Leash.

**Positioning against systems the survey names.** Claude Code, Codex, OpenHands,
SWE-agent are *agents* (inner loops); Leash drives them as drivers. LangChain
DeepAgents, LiteLLM, Langfuse provide harness components (state, gateways,
observability) as libraries; Leash provides them as language semantics with a
replay guarantee. Meta-Harness, AutoHarness, Live-SWE-agent, EvoMAC evolve
harnesses; Leash aims to be the representation such systems evolve.

## 3. SIA: Self-Improving AI with Harness & Weight Updates
Hebbar et al. (Hexo Labs), arXiv 2605.27276 (May 2026), 15 pages.

**Claim.** Two silos, harness/scaffold self-improvement (weights fixed) and
test-time training (harness fixed), can be combined: a Feedback-Agent reads the
full trajectory τ_g, metrics E_g and scaffold A_g and chooses between a harness
update and a weight update. Results: +25.1pp on LawBench, 12.4% faster TriMul
kernels, +20.4% on scRNA denoising versus prior SOTA; harness updates
concentrate on "software-engineering hygiene: parsing, retries, dispatch".

**Ideas used by Leash.**
- Scaffold = "every part of the agent that is fixed code rather than model
  output": system prompt, tool-dispatch logic, answer extraction, retry logic,
  search procedure. Leash is a language for exactly this artifact.
- The loop needs (a) the full structured trajectory, (b) a deterministic
  verifier, (c) a machine-editable scaffold. Leash provides (a) the journal,
  (b) `ensure` checks, (c) LIR. `A_{g+1} = F(A_g, τ_g, E_g, U)` is a Leash flow
  over episode packages (roadmap M10).
- The observation that scaffold edits rarely add domain reasoning tells us
  what Leash should optimise: parsing (typed outputs), retries (`retry`),
  dispatch (tools with schemas), search (patterns) — the boring parts, done
  correctly and deterministically.
- Limitation noted by SIA, coupled co-evolutionary Goodhart against a fixed
  verifier, motivates Leash's held-out evaluation and change contracts before
  promoting an evolved flow.

**Not in scope.** Weight updates. Leash may later expose a `driver` whose model
is an adapter checkpoint, which is enough for a SIA-W+H loop to be scripted
around Leash.

## 4. Design decisions traced to the papers

| Decision | Source |
|---|---|
| `task` contract with `ensure` and evidence-bound `complete` | AHE P3; Code-as-Harness "termination governed by verification" |
| Failure kinds on every failure; `attempt/recover` by kind | AHE taxonomy and P4 |
| Journal + replay | Code-as-Harness reproducibility and telemetry; AHE trace protocol |
| Eight traces, five outcome labels, ladder profiles | AHE |
| Capability sets with shell patterns and gates; permission tiers | Code-as-Harness §3.4, §5.2.5 |
| Explicit convergence in all patterns | Code-as-Harness §4 |
| Evidence `scope`/`confidence` | Code-as-Harness oracle inadequacy |
| LIR as data; change contracts; `leash evolve` | SIA; Code-as-Harness AHE |
| Entropy audit before `complete` | AHE P5 |

## 5. Ten lines worth quoting in talks and docs

1. "Autonomous software-engineering capability is an emergent property of a model–harness–environment system, not of the model alone." (AHE)
2. "Verification is a runtime capability." (AHE)
3. "Memory is auditable only when its use is traced." (AHE)
4. "Failure attribution separates diagnosis from action." (AHE)
5. "The model provides judgment … the harness … verifies whether each state transition is acceptable." (Code as Harness)
6. "Termination should likewise be governed by verification rather than by model confidence." (Code as Harness)
7. "Reliability comes from governed state transitions, not simply from better repair prompts." (Code as Harness)
8. "A harness mutation should be treated like a code change to a safety-critical runtime." (Code as Harness)
9. "The harness shapes how the agent searches; weight updates change what the model knows." (SIA)
10. "Scaffold edits concentrate on software-engineering hygiene: parsing, retries, dispatch." (SIA)

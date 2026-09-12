# ADR-0010 Licence: Apache-2.0, with headers as policy

Status: accepted (2026-09-12).

## Context
Leash needs a licence before WP-01 can create `LICENSE` and crate metadata. Two
goals were considered. The first was preventing companies from using Leash
internally without disclosing it. The second was patent hygiene: an explicit
patent grant from contributors and a defence against patent aggression.

No licence that satisfies the Open Source Definition can meet the first goal.
Permission to run the work for any purpose, including private and internal
purposes, is fundamental to every open-source licence, and copyleft obligations
trigger on distribution rather than use. Copies made and used inside a single
organisation are not distribution. The AGPL network clause reaches users who
interact with a modified version remotely, but when every such user is an
employee, the source stays inside that company. Meeting the first goal requires
a source-available licence such as the Business Source Licence, which is not
open source and which carries no patent grant at all.

## Decision
Apache-2.0 for the entire repository: runtime, compiler, standard library,
specification, examples and documentation. The first goal is abandoned
deliberately. The second is met.

Per-file `SPDX-License-Identifier` headers are required by project policy, not
by the licence. The policy exists because requirements 4(b) and 4(c) of the
licence, which concern notices on modified and derived files, are much easier to
satisfy when every file declares itself, and because dependency-audit tooling
reads SPDX identifiers. Copyright is attributed to `The Leash Authors`, listed
in `AUTHORS`, so headers do not churn as contributors arrive. Contributions come
in under section 5 of the licence with a Developer Certificate of Origin
sign-off rather than a contributor licence agreement.

## Alternatives
- **MIT**: shorter and more popular, but contains no express patent grant and no
  defensive termination. For a project about executing model-generated code and
  driving vendor toolchains, an express grant is worth the extra length.
- **BUSL-1.1 for the runtime with Apache-2.0 for the spec and standard library**:
  the only combination that blocks unlicensed internal production use. Rejected
  because a new language needs adoption more than it needs protection, and
  because source-available terms get projects banned by corporate legal review
  before an engineer can try them.
- **AGPL-3.0 with a contributor licence agreement and commercial dual licensing**:
  would create commercial leverage, but does not prevent silent internal use, and
  copyleft on a runtime that executes user programs raises derivative-work
  questions that scare adopters away from a tool with no track record.

## Consequences
- Anyone, including well-funded companies, may use Leash internally and never
  say so. This is the accepted cost.
- Contributors grant patent rights over their contributions to all users, and
  anyone suing over the work loses their own patent licences.
- Without a contributor licence agreement the project cannot relicense or offer
  commercial terms later without the agreement of every copyright holder.
  Revisit before accepting substantial outside contributions.
- Protection against others patenting these ideas comes from publication as
  prior art, not from the licence. Publish the specification with real dates and
  consider a formal defensive publication.
- The dependency licence allowlist in `deny.toml` must exclude copyleft terms,
  or Leash could not be distributed under Apache-2.0.
- Trademark is unaddressed. Section 6 grants no trademark rights; a
  `TRADEMARK.md` follows if the name becomes worth protecting.

Policy details live in `docs/14-licensing.md`.

# ADR-0007 Capability model

Status: accepted.

## Decision
Default-deny for writes, shell, network, human, env and secrets. File
capabilities as globs relative to the working copy; shell capabilities as
patterns over the full tokenised command line, executed without a shell unless
`sh -c` is explicitly allowed; network as host allowlists. Scopes narrow
monotonically; the runtime is the authority and drivers are translated
cooperatively; CLI-driven runs are post-checked by diff and reverted on
violation.

## Alternatives
Trusting driver sandboxes (inconsistent across drivers); a full container per
run from day one (heavy; planned as an optional backend); seccomp/landlock
(Linux-only; a candidate for the container backend).

## Consequences
Clear, reviewable `allow` blocks in every task; documented gaps for subprocess
network egress until the container backend lands.

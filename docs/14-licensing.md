# Licensing and File Headers

Status: accepted (2026-09-12). Decision recorded in
`docs/adr/0010-licensing.md`. This document is the policy every contributor and
every work package follows.

Leash is licensed under the Apache License, Version 2.0. The full text is in
`LICENSE`. The SPDX identifier is `Apache-2.0`.

## 1. What the licence actually requires

Apache 2.0 imposes obligations only on **distribution**. Running Leash
privately, including inside a company, carries no obligations at all. When you
do distribute the work or a derivative of it, section 4 requires four things:

| | Requirement |
|---|---|
| 4(a) | Give recipients a copy of the licence. |
| 4(b) | Make modified files carry prominent notices saying you changed them. |
| 4(c) | Keep all copyright, patent, trademark and attribution notices found in the source you started from. |
| 4(d) | If the work ships a `NOTICE` file, reproduce its attribution notices in your distribution. |

Per-file headers are **not** required by the licence. The appendix to the
licence recommends attaching a boilerplate notice, and 4(b) and 4(c) are far
easier to satisfy when every file says what it is. That is why this project
requires headers as policy, not because the licence compels them.

`SPDX-License-Identifier` is likewise not an Apache requirement. It comes from
the SPDX specification and is the standard machine-readable shorthand. Tools
that audit dependency licences read it; we use it so that Leash is easy to
consume.

## 2. Header policy

Every source file that supports comments starts with this header, in the file's
own comment syntax, before any code and after any mandatory first line such as
a shebang.

```
SPDX-License-Identifier: Apache-2.0
Copyright 2026 The Leash Authors
```

Concrete forms:

```cpp
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <filesystem>
```

```leash
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

module std.patterns
```

```toml
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors

[drivers.anthropic]
model = "claude-opus-5"
```

```sh
#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
```

```typescript
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
```

```html
<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 The Leash Authors -->
```

Rules:

- The year is the year the file was created. Do not update it on edit and do not
  write year ranges. Git history is the authoritative record of when what
  changed, and churning headers produces noise in every diff.
- The copyright line names `The Leash Authors`, not an individual. `AUTHORS`
  lists who that is. This keeps headers stable as contributors arrive.
- A contributor who wants their own name in a file they wrote may add a second
  line below the project line. Never replace the project line.
- Do not paste the full Apache boilerplate paragraph into source files. The two
  short lines above carry the same information, and the appendix form exists for
  projects that cannot rely on SPDX tooling.

## 3. Files that do not get a header

| Kind | Reason and handling |
|---|---|
| JSON, JSON Schema, lockfiles | The format has no comments. Covered by the repository `LICENSE`. If a file must be self-describing, add a sibling `<file>.license` holding the two header lines. |
| Test fixtures whose bytes are the test | A header would change what is under test. Put one `README.md` in the fixture directory stating the licence. |
| Generated files | The generator emits the header into its output. Generated files also carry a `@generated` marker and are excluded from the header check by path. |
| Vendored third-party code | Keep the upstream headers untouched. See section 5. |
| Binary assets | Covered by the repository `LICENSE`; record provenance in `THIRD-PARTY-NOTICES.md` if not original. |

Documentation under `docs/` does not carry per-file headers. The repository
`LICENSE` covers it, and Markdown headers would appear in rendered output.

## 4. Build and package metadata

The implementation is one CMake project, so the licence is declared once rather
than per module. The root `CMakeLists.txt` carries it:

```cmake
project(leash VERSION 0.1.0 LANGUAGES CXX)
set(LEASH_LICENSE "Apache-2.0")
set(LEASH_AUTHORS "The Leash Authors")
set(LEASH_REPOSITORY "https://github.com/SeamusMullan/Leash")
```

CMake has no per-target licence field, so declare these once in the root file
and read them where a target needs them; do not repeat the identifier per
module. Use the SPDX identifier, never a path to the licence file: identifiers
are what audit tools and package indexes read, and a path is not
machine-readable.

`vcpkg.json` states the licence too, because anything that reads the dependency
graph reads the manifest and not the build files:

```json
{
  "name": "leash",
  "version-string": "0.1.0",
  "license": "Apache-2.0"
}
```

The field takes an SPDX licence expression at licence-list version 3.9, so the
same syntax covers disjunctions such as `CC0-1.0 OR Apache-2.0`. The
`editors/vscode` package declares `"license": "Apache-2.0"` in `package.json`.

## 5. Third-party code and dependencies

- **Vendored source**: copy it under `third_party/<name>/`, keep its own licence
  file and every existing notice, and record it in `THIRD-PARTY-NOTICES.md`. If
  you modify a vendored file, add a line to its header saying what you changed
  and when. That is requirement 4(b), and it is the one most projects breach.
- **Dependencies**: allowed licences are `Apache-2.0`, `BSL-1.0`, `MIT`,
  `BSD-2-Clause`, `BSD-3-Clause`, `ISC`, `Zlib`, `CC0-1.0`, `Unicode-3.0` and
  `Apache-2.0 WITH LLVM-exception`. `MPL-2.0` is allowed for unmodified
  dependencies only. `GPL-*`, `AGPL-*`, `LGPL-*`, `SSPL`, `BUSL` and proprietary
  terms are refused, because they would restrict how users may distribute
  Leash. Enforced by `scripts/check-licences.sh` in CI; its allowlist is the
  generic list in this paragraph plus the table below.
- **Model-generated code** is treated as your own contribution. You are
  responsible for it, exactly as if you had typed it.

Every vcpkg port states its licence as an SPDX expression in its own
`vcpkg.json`, and vcpkg records those expressions in the SPDX bill of materials
it generates for an install. The check reads that bill of materials instead of
scraping `LICENSE` files, so a port that changes its licence fails on the next
baseline bump. The current set:

| Dependency | vcpkg port | SPDX identifier |
|---|---|---|
| Boost.Asio, Boost.Beast, Boost.Process, Boost.UUID | `boost-asio`, `boost-beast`, `boost-process`, `boost-uuid` | `BSL-1.0` |
| OpenSSL | `openssl` | `Apache-2.0` |
| libblake3 | `blake3` | `CC0-1.0 OR Apache-2.0` |
| valijson | `valijson` | `BSD-2-Clause` |
| nlohmann/json | `nlohmann-json` | `MIT` |
| CLI11 | `cli11` | `BSD-3-Clause` |
| Catch2 v3 | `catch2` | `BSL-1.0` |
| spdlog | `spdlog` | `MIT` |
| toml++ | `tomlplusplus` | `MIT` |
| RapidCheck | `rapidcheck` | `BSD-2-Clause` |

`blake3` is offered under either licence and its port declares the disjunction,
so both identifiers are accepted for that port. `rapidcheck` declares
`BSD-2-Clause` in its port manifest, which matches the two-clause text in its
upstream `LICENSE.md`. Catch2 and RapidCheck are test-only and are never linked
into the shipped `leash` binary.

## 6. The NOTICE file

`NOTICE` is deliberately minimal. Everything it contains propagates into every
downstream distribution forever, under requirement 4(d). Add to it only
attributions that are legally required by code we actually ship. Do not use it
for credits, sponsors or acknowledgements; those belong in `README.md`.

## 7. Contributions

Contributions are accepted under section 5 of the licence: anything
intentionally submitted for inclusion is licensed under Apache 2.0, including
its patent grant, unless the submitter conspicuously marks it otherwise. The
project uses the Developer Certificate of Origin rather than a contributor
licence agreement. Sign commits off with `git commit -s`, which appends the
`Signed-off-by` line that the DCO check requires.

No contributor licence agreement is collected, which means the project cannot
relicense or offer commercial licences later without the agreement of every
copyright holder. That is a deliberate consequence of choosing a permissive
licence. Revisit it before accepting outside contributions if a dual-licensing
model ever becomes interesting.

## 8. Identity, AUTHORS and .mailmap

`AUTHORS` lists one line per copyright holder. It is the answer to the question
"who are The Leash Authors", nothing more. Apache 2.0 does not require the file
and does not care what is written in it: copyright vests in the person who wrote
the code, and the name recorded is a label pointing at them.

Alternate spellings of an existing author do not belong in `AUTHORS`. They
belong in `.mailmap` at the repository root, which is git's mechanism for
collapsing several name and address combinations onto one contributor. Git
applies it in `git shortlog`, `git log --use-mailmap`, `git blame` and most
forge interfaces, so a person who has committed under more than one name or
address appears once.

```
Canonical Name <canonical@email> Commit Name <commit@email>
Canonical Name <canonical@email> <commit@email>        # any name, that address
Canonical Name <canonical@email> Commit Name <>        # that name, any address
```

Two limits are worth knowing. `.mailmap` changes display only; the raw commit
objects keep whatever name and address were used at commit time, and anyone can
read them with `git log --format='%an <%ae>'`. And it is per-repository, so it
travels with a clone but not to a different project.

A name recorded in git history can only be removed by rewriting history, with
`git filter-repo` or an equivalent. That is cheap before the first push and
disruptive afterwards, because every commit hash changes and every clone
diverges. If the history of this repository should contain only one name, do it
before publishing.

Handles are acceptable everywhere a name is asked for, in `AUTHORS`, in
`.mailmap` and in Developer Certificate of Origin sign-offs. The DCO asks for a
name you are known by. The stricter rule that sign-offs must carry a legal name
comes from the Linux kernel's own additional policy and is not inherited here.

## 9. Trademarks

Section 6 of the licence grants no trademark rights. The name Leash and any
logo are separate from the copyright licence. If the project gains users, add a
`TRADEMARK.md` setting out permitted nominative use, so that forks can say
"compatible with Leash" without implying endorsement.

## 10. Enforcement

`just lint` and CI run `scripts/check-headers.sh` over every tracked file whose
extension is in the header list, excluding the paths in section 3. A missing or
malformed header fails the build: the script exits non-zero and prints each
offending path with the text it expected. It is a small script rather than a
dependency; keep it under fifty lines and give it a `--fix` mode that inserts a
missing header at the right place, after any shebang, and leaves a correct
header untouched.

Adopting the REUSE specification in full, with a `LICENSES/` directory and
`.license` sidecars everywhere, is optional and can come later. `reuse lint`
would then replace the bespoke script. The header format above is already REUSE
compatible, so nothing needs rewriting if that day comes.

## 11. New file checklist

1. Header in the first lines, correct comment syntax, current year.
2. `LEASH_LICENSE` in the root `CMakeLists.txt` and `license` in `vcpkg.json`
   stay `Apache-2.0`; a new module adds no licence metadata of its own.
3. New dependency licences are on the allowlist in `scripts/check-licences.sh`.
4. Vendored or adapted code keeps its upstream notices, is listed in
   `THIRD-PARTY-NOTICES.md`, and says what you changed.
5. `just lint` passes.

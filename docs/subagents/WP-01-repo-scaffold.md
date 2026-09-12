# WP-01: Repository scaffold, workspace, CI

Milestone: M0 · Size: S · Status: see `docs/status.md`

## Goal
After this WP the repository is a buildable Rust workspace with every crate the
architecture names, a green CI pipeline, a task runner, contribution rules and
a `leash` binary that prints its version and runs a `doctor` stub. Every later
WP starts from this skeleton instead of inventing layout, lints or tooling.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/03-runtime-architecture.md` §2 (workspace layout, dependency direction)
- `docs/adr/0001-implementation-language.md` (crate allowlist, no `unsafe`)
- `docs/10-cli-and-tooling.md` §1 (command list; only `--version` and `doctor` are built here)
- `docs/subagents/README.md` and `CLAUDE.md` (conventions, definition of done)
- `docs/14-licensing.md` (header policy, crate metadata, dependency allowlist)
- `docs/adr/0010-licensing.md` (why Apache-2.0)

## Scope
- `Cargo.toml` workspace (resolver 2, `edition = "2024"`) with members:
  `crates/leash-syntax`, `leash-types`, `leash-ir`, `leash-runtime`,
  `leash-effects`, `leash-drivers`, `leash-trace`, `leash-std`, `leash-cli`,
  `leash-lsp`, `leash-eval`, plus two reserved crates created empty here and
  filled later: `leash-spec` (WP-02) and `leash-store` (WP-06).
- Each library crate: `src/lib.rs` with a crate-level doc comment stating its
  responsibility (copy the one-liner from docs/03 §2) and one passing unit test.
- `[workspace.package]` shared `version = "0.1.0"`, `license`, `repository`,
  `rust-version`; `[workspace.dependencies]` pinning the allowlist below;
  `[workspace.lints.rust] unsafe_code = "forbid"`, `[workspace.lints.clippy]`
  `all = "deny"`, `pedantic = "warn"`; every crate sets `lints.workspace = true`.
- `rust-toolchain.toml` (`channel = "stable"`, components `rustfmt`, `clippy`).
- `.github/workflows/ci.yml`: matrix `ubuntu-latest`, `macos-latest`; steps
  `cargo fmt --all --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo nextest run --workspace` (install via `taiki-e/install-action@nextest`),
  `cargo doc --no-deps`; caching with `Swatinem/rust-cache`.
- `justfile` with recipes `check` (fmt-check + clippy + nextest), `test`,
  `fmt`, `lint`, `doc`, `run-example FILE` (`cargo run -p leash-cli -- run {{FILE}}`),
  `ci` (what CI runs, so it can be reproduced locally).
- `CONTRIBUTING.md`: branch naming `wp/NN-slug`, conventional commits, PR
  template contents, how to run `just ci`, where briefs live, and a licensing
  section stating that contributions are accepted under Apache-2.0 section 5
  with a Developer Certificate of Origin sign-off (`git commit -s`), linking
  `docs/14-licensing.md`.
- `.gitignore`: `target/`, `.leash/runs/`, `.leash/cas/`, `.leash/mcp/`, `*.log`, editor files.
- Licensing, per `docs/14-licensing.md`. `LICENSE`, `NOTICE` and `AUTHORS`
  already exist at the repository root; do not rewrite them. Set
  `[workspace.package] license = "Apache-2.0"` and `authors = ["The Leash
  Authors"]`, inherited by every crate with `license.workspace = true`. Add the
  two-line SPDX header to every file this WP creates that supports comments,
  including `justfile`, `build.rs` files, shell scripts and the CI workflow.
- `deny.toml` plus `cargo deny check licenses` wired into `just lint` and CI,
  with the allowlist of `docs/14-licensing.md` §5: `Apache-2.0`, `MIT`,
  `BSD-2-Clause`, `BSD-3-Clause`, `ISC`, `Zlib`, `Unicode-3.0`,
  `Apache-2.0 WITH LLVM-exception`, and `MPL-2.0` for unmodified dependencies.
  Copyleft and proprietary terms fail the build.
- `scripts/check-headers.sh` (under fifty lines, no dependencies) implementing
  `docs/14-licensing.md` §10: walk tracked files, skip the exclusions of §3, fail
  on a missing or malformed header, and support `--fix` to insert one. Wire it
  into `just lint` and CI.
- `THIRD-PARTY-NOTICES.md` with a header row and no entries yet.
- Leave `LICENSE`, `NOTICE`, `AUTHORS` and `.mailmap` exactly as they are; they
  are already correct (`docs/14-licensing.md` §8).
- `crates/leash-cli/src/main.rs`: `clap` derive with subcommands `doctor` and
  a hidden `__unimplemented` catch; `leash --version` prints `leash 0.1.0`.
  `doctor` prints a table: `claude` (found/version or missing), `codex`
  (same), `ANTHROPIC_API_KEY` (set/unset), `git` version, `cargo-nextest`
  presence; exits 0 always in this WP.
- `README.md` "Building" section: toolchain, `just ci`, crate map.

## Out of scope
Any language, runtime or driver code. Dependency versions beyond pinning.
Release/packaging workflows. Benchmarks. Adopting the full REUSE specification with a `LICENSES/` directory; the header format is already REUSE compatible and the migration can happen later.

## Interfaces
Workspace dependency allowlist (pin current stable versions):
`logos`, `serde` (derive), `serde_json`, `tokio` (full), `reqwest` (json,
stream, rustls-tls), `rmcp`, `clap` (derive), `miette` (fancy), `thiserror`,
`anyhow`, `tracing`, `tracing-subscriber`, `blake3`, `jsonschema`, `globset`,
`shell-words`, `toml`, `uuid` (v4), `time` (formatting, parsing, serde),
`async-trait`, `futures`, `tokio-util` (cancellation token), `bytes`,
`camino` (UTF-8 paths), `tower-lsp`. Dev-only: `insta`, `proptest`, `tempfile`,
`wiremock`. Anything else needs a one-line justification in the PR.

`leash-cli` layout other WPs extend: `src/main.rs` (clap root), `src/cmd/<name>.rs`
one file per subcommand, `src/exit.rs` mapping outcomes to the exit codes in
docs/10 §1.

## Deliverables
- Workspace files listed in Scope; 13 crates that build.
- `.github/workflows/ci.yml`, `justfile`, `CONTRIBUTING.md`, `.gitignore`, `deny.toml`, `scripts/check-headers.sh`, `THIRD-PARTY-NOTICES.md`.
- `README.md` Building section.

## Acceptance criteria
- `cargo build --workspace` succeeds on a clean checkout with the pinned toolchain.
- `cargo nextest run --workspace` reports at least 13 passed tests, 0 failed.
- `cargo clippy --workspace --all-targets -- -D warnings` exits 0.
- `cargo fmt --all --check` exits 0.
- `just ci` runs the same four steps and exits 0.
- `cargo run -p leash-cli -- --version` prints `leash 0.1.0`.
- `just lint` fails when a header is removed from any tracked source file, and
  `scripts/check-headers.sh --fix` restores it so that `just lint` passes again.
- `cargo deny check licenses` exits 0, and adding a GPL-licensed dependency
  makes it exit non-zero.
- `cargo run -p leash-cli -- doctor` prints one row per probe and exits 0
  even when `claude`/`codex` are missing.
- `grep -R "unsafe" crates/ --include=*.rs` finds nothing outside comments.
- CI workflow passes on a push to a branch (link the run in the PR).

## Notes and pitfalls
- Keep `Cargo.lock` committed (binary project).
- Do not enable `serde_json`'s `preserve_order` feature globally; WP-04 needs
  canonical (sorted) serialisation and will handle ordering explicitly.
- `tokio` with `rt-multi-thread`; `reqwest` with `rustls-tls`, never
  `native-tls`, so the binary is portable.
- `doctor` must not spawn `claude`/`codex` with prompts; `--version` only,
  with a 5 s timeout.
- The dependency direction in docs/03 §2 is `syntax ← types ← ir ← runtime ← {effects, trace} ← cli/lsp/eval`;
  note that `leash-drivers` will be independent of `leash-runtime`
  (runtime depends on drivers) per WP-05; add no dependencies between crates
  in this WP so later WPs choose their edges.

## Dependencies
Hard: none. The licence decision is made; see `docs/adr/0010-licensing.md`.

## Estimated size
S (≤ 1 day).

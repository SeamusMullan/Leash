# WP-24: Documentation site, tutorials and `leash doc`

Milestone: M8 · Size: M · Status: see `docs/status.md`

## Goal
A browsable documentation site is built from the repository's existing sources with no hand-maintained second copy: `docs/` supplies the design and reference chapters, `examples/` supplies a gallery, `spec/grammar.ebnf` plus `docs/02-language-spec.md` supply the language reference, and a new `leash doc` command generates the standard-library reference from the doc comments in `std/*.leash`. CI builds the site and fails on a broken internal link, so a renamed section cannot rot the site silently.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/README.md` (document index and audiences) and `docs/00-vision.md`
- `docs/01-language-tour.md` (the tutorial's source material)
- `docs/02-language-spec.md` §1 (items, `pub`, module naming), §2.5 (doc comments), §12 (stdlib surface)
- `docs/10-cli-and-tooling.md` §1 (command table, where `doc` is added), §5 (editor support)
- `docs/subagents/WP-21-stdlib-patterns.md` (std file list; every public item carries `///`)
- `docs/subagents/WP-22-cli-ux.md` (argument parsing, help text and exit codes for `leash doc` live there)
- `spec/grammar.ebnf` (rendered into the language reference)

## Scope
- `site/` mdBook project: `book.toml`, `src/SUMMARY.md`, theme overrides, `mdbook-linkcheck` backend configured to fail the build on broken internal links (external links warn only, so CI does not depend on the network).
- Single source of truth. `SUMMARY.md` points at files under `site/src/`, and the build step materialises `site/src/docs`, `site/src/spec` and `site/src/examples` as symlinks to `../../docs`, `../../spec`, `../../examples` (falling back to a copy where symlinks are unavailable). No chapter file may contain prose that also exists in `docs/`; a test enforces this (see Acceptance criteria).
- `leash doc` (implemented in `crates/leash-cli/src/docgen/`, surfaced by WP-22's argument parser):
  - `leash doc [package|path]…` extracts every `pub` item (`let type enum fn tool agent memory task flow`, per `docs/02` §1) with its `///` doc comment, rendered signature, module path and source location. Non-`pub` items are included only with `--private`.
  - Signatures are rendered from the typed program (`leash-types`, WP-08) so inferred effect rows appear: `pub fn map<T, U>(xs: [T], f: fn(T) -> U ! e, concurrency: Int = 1) -> [U] ! e + {spawn}`. When type checking fails, `leash doc` exits 2 with the diagnostics rather than emitting a partial page.
  - `--format md` (default) writes one page per module with an index; `--format json` emits `{"version": 1, "modules": [{"path", "doc", "items": [{"kind", "name", "signature", "doc", "effects", "file", "line", "stability"}]}]}` for editors and for the site build.
  - `--check` exits 1 and lists every `pub` item without a doc comment; used by CI to keep the stdlib documented.
  - `--out <dir>` writes files; without it the markdown goes to stdout.
- Generated chapters, produced by an `xtask` (or `just` recipe) into `site/src/generated/` and ignored by git:
  - `stdlib.md` from `leash doc std --format md`, replacing the `docs/generated/stdlib.md` snapshot WP-21 ships as a placeholder.
  - `language-reference.md`: `docs/02-language-spec.md` followed by `spec/grammar.ebnf` in a fenced block with per-production anchors, plus a keyword index built from the grammar's keyword comment block.
  - `examples.md`: the gallery. For each file in `examples/`, the leading `//` comment block becomes the description, the module path becomes the title, and the source is included verbatim with a `leash` language tag. Ordering is filename order.
  - `cli.md`: `leash --help` and each subcommand's `--help`, reusing WP-22's `docs/generated/cli-help.md` producer.
- `site/src/tutorial/`: "Leash in 10 minutes" (hand-written, the only substantial new prose in this WP), taking the reader from `leash new demo --template fix-bug` through `leash check`, `leash run` with the mock driver, reading the verification report, `leash replay`, and one budget or capability violation so the reader sees enforcement. Every command block in the tutorial is executed by a `trycmd` case so it cannot drift.
- Syntax highlighting for `leash` code blocks: a highlight.js language definition in `site/theme/leash.js` covering the keyword lists in `spec/grammar.ebnf`, loaded via `book.toml`'s `additional-js`.
- CI: a `site` job that installs `mdbook` and `mdbook-linkcheck` (pinned versions), runs the generation xtask, builds the book, and uploads the result as an artifact. Optional deploy to GitHub Pages on `main` behind a repository variable, disabled by default.

## Out of scope
- Rewriting or restructuring the design documents. Fix a heading only when it breaks a link, and say so in the PR.
- A documentation theme beyond mdBook defaults plus the highlight definition.
- API documentation for the Rust crates (`cargo doc` is enough; link to it from the site index).
- Versioned documentation for multiple releases; the site tracks `main`.
- Translating `docs/` into a different format (no Docusaurus, no MkDocs).

## Interfaces
```rust
// crates/leash-cli/src/docgen/mod.rs
pub struct DocOptions { pub include_private: bool, pub format: DocFormat, pub out: Option<PathBuf>, pub check: bool }
pub enum DocFormat { Markdown, Json }

pub struct DocItem {
    pub kind: ItemKind,            // Let | Type | Enum | Fn | Tool | Agent | Memory | Task | Flow
    pub module: String,            // "std.core"
    pub name: String,
    pub signature: String,         // rendered from leash_types::TypedProgram, effects included
    pub doc: Option<String>,       // `///` block, dedented, markdown
    pub effects: Vec<String>,
    pub file: PathBuf, pub line: u32,
}
pub struct DocModule { pub path: String, pub doc: Option<String>, pub items: Vec<DocItem> }

pub fn extract(program: &leash_types::TypedProgram, opts: &DocOptions) -> Vec<DocModule>;
pub fn render_markdown(mods: &[DocModule]) -> BTreeMap<PathBuf, String>;   // one file per module + index.md
pub fn missing_docs(mods: &[DocModule]) -> Vec<DocItem>;                   // drives --check
```
Exit codes follow `docs/10` §1: 0 success, 1 `--check` found undocumented items, 2 compile error in the sources being documented.

Gallery and language-reference generation live in `xtask/src/site.rs` with the entry points `fn gallery(examples: &Path, out: &Path)`, `fn language_reference(spec: &Path, docs: &Path, out: &Path)`, `fn cli_reference(out: &Path)`.

## Deliverables
- `site/{book.toml, src/SUMMARY.md, src/index.md, src/tutorial/*.md, theme/leash.js}`; `site/src/generated/` and the `docs`/`spec`/`examples` links created by the build and git-ignored.
- `crates/leash-cli/src/docgen/{mod.rs, markdown.rs, json.rs}` and `commands/doc.rs`.
- `xtask/src/site.rs` (or `just site`, `just site-serve`, `just site-check` recipes calling it).
- `.github/workflows/ci.yml`: `site` job.
- `tests/site/tutorial.trycmd`, `tests/site/no_duplicate_prose.rs`, `crates/leash-cli/tests/docgen.rs`.
- `docs/10-cli-and-tooling.md` §1 row for `leash doc` with its flags; `docs/README.md` gains a line pointing at the site build.

## Acceptance criteria
- `just site` from a clean checkout produces `site/book/` and exits 0; `mdbook-linkcheck` reports `0 broken links` over all internal targets.
- `leash doc std --format json | jq '[.modules[].items[]] | length'` returns a number matching the count in `crates/leash-cli/tests/docgen.rs`, and `jq -r '.modules[] | select(.path=="std.core") | .items[] | select(.name=="map") | .signature'` prints a signature containing `! e + {spawn}`.
- `leash doc std --check` exits 0 on `main`; removing one `///` from a `pub` item in `std/core.leash` makes it exit 1 and name that item.
- `cargo nextest run -p leash-cli docgen` passes, including a case asserting that a type error in the documented sources yields exit code 2 and no output files.
- `cargo nextest run --test tutorial` (trycmd) executes every command block in `site/src/tutorial/` against the mock driver and matches the recorded output.
- `cargo nextest run --test no_duplicate_prose` fails if any file under `site/src/` outside `generated/` and `tutorial/` contains a paragraph that also appears in `docs/`, and passes on `main`.
- The gallery page lists all five files in `examples/`; adding a sixth example and re-running `just site` adds it without editing `SUMMARY.md`.
- `git status --porcelain site/` is empty after `just site` (all generated output is ignored).
- `cargo clippy --all-targets -- -D warnings` and `cargo fmt --check` clean.

## Notes and pitfalls
- mdBook resolves `SUMMARY.md` paths relative to `src/`; the symlinks must be created before `mdbook build`, and `mdbook serve`'s watcher follows them, so editing `docs/*.md` live-reloads. Recreate links idempotently, and delete stale ones when a document is removed.
- Keep chapter titles in `SUMMARY.md` identical to the `# ` heading of the linked file; the duplicate-prose test ignores headings, not titles.
- `leash doc` must not execute the documented program. It type-checks and reads; `agent` and `memory` items are documented by their declared shape, never by connecting to anything.
- Doc comments are markdown; dedent them by the common leading indentation and escape nothing else. Code fences inside `///` must survive.
- Keep the tutorial free of API keys and network access: everything runs on `drivers.mock`.
- Pin `mdbook` and `mdbook-linkcheck` versions in CI and in `just`; unpinned toolchains break the link checker across releases.
- If a chapter needs to exist only in the site (a landing page), keep it under 20 lines and link into `docs/` rather than restating it.

## Dependencies
Hard: WP-21 (std exists with doc comments), WP-22 (CLI argument parsing, `docs/generated/cli-help.md`).  Soft: WP-08 (typed signatures; without it `leash doc` can fall back to syntactic signatures and must say so in the page header), WP-25 (trycmd harness conventions).

## Estimated size
M (2–4 days).

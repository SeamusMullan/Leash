# WP-23: LSP and VS Code extension

Milestone: M8 · Size: M · Status: see `docs/status.md`

## Goal
Editing Leash is pleasant: a language server provides diagnostics, hover with types and effects, go-to-definition and formatting; a VS Code extension provides syntax highlighting, the LSP client, a "Run flow" lens and a journal viewer. Both reuse the front end (`leash_syntax`, `leash_types`) rather than re-implementing analysis.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/03-runtime-architecture.md` §3 (front end), §4 (semantic analysis), §11 (CLI and LSP)
- `docs/10-cli-and-tooling.md` §5 (editor support)
- `docs/02-language-spec.md` §2 (lexical: keywords, strings with interpolation, doc comments), §5 (effects)
- `spec/grammar.ebnf`
- `docs/11-episode-package-and-eval.md` §1 (trace files the viewer reads)

## Scope
- `src/lsp` (hand-written JSON-RPC 2.0 over stdio, sharing framing and the message loop with the MCP client from WP-14; no server framework): `initialize` capabilities (incremental text sync, hover, definition, formatting, code lens, push diagnostics); `didOpen|didChange|didSave` re-check the containing package (debounced 150 ms on an `asio::steady_timer`, off the main strand, cancellable through `asio::cancellation_signal`); `publishDiagnostics` from `leash_types` diagnostics (code, severity, span, related info) for every file of the package, including files not open; `hover` shows the type, the inferred effect set and, for `run`/`ask`/tool calls, the capabilities required (`docs/03` §4.4) plus the doc comment; `definition` for items, locals, enum variants and std names (std sources served as read-only virtual documents under the `leash-std:` scheme); `formatting` via the `leash_syntax` formatter, only when the file parses; `codeLens` "▶ Run flow <Name>" on every `flow`, resolving to command `leash.runFlow` with file and flow name.
- Package awareness: find the nearest `leash.toml`, analyse all modules in the package; single-file mode when there is no manifest.
- `editors/vscode/`: `package.json` (language id `leash`, extension `.leash`, activation on language), `syntaxes/leash.tmLanguage.json` covering hard and contextual keywords (contextual ones highlighted only at line start followed by `{` or `.`), literals (`INT`, `FLOAT`, `DURATION`, strings with nested `{…}` interpolation, `{{`/`}}`, raw strings, `"""` blocks), comments and `///` doc comments, enum shorthand `.Variant`, failure kinds, effect clauses `! {…}`; `language-configuration.json` (comments, brackets, auto-close, indentation); LSP client via `vscode-languageclient` launching `leash lsp`; command `leash.runFlow` opening an integrated terminal running `leash run <file> --flow <name>`; a journal viewer webview (`leash.openRun`) that lists `.leash/runs/` (via `leash runs list --json`) and renders `traces/action.jsonl`, `tool.jsonl`, `verification.jsonl` and `outcome.json` as tables with links to CAS blobs (read-only, no network); a `TextDocumentContentProvider` for `leash-std:`.
- Optional: `editors/tree-sitter-leash/` grammar with `corpus/` tests, only after everything above is done; otherwise a follow-up in `docs/status.md`.
- Tests: LSP integration test driving the server over an in-process pipe pair with a fake JSON-RPC client (open a document with a type error → diagnostic received; hover on an identifier → type and effects; format → identical to `leash fmt`; definition → correct range); TextMate grammar tests with `vscode-tmgrammar-test` over `editors/vscode/test/*.leash` scope-assertion files.
- `docs/10` §5 updated with install instructions and a Neovim `lspconfig` snippet.

## Out of scope
- Semantic tokens, rename, references, completion (follow-ups; keep the server structured so they can be added without rework).
- JetBrains plugin; Neovim beyond the documented snippet.
- Editing the journal or running flows from inside the webview.

## Interfaces
```cpp
// src/lsp/include/leash/lsp/server.hpp
class Backend {
public:
    Backend(Client& client, Workspace workspace);

private:
    Client& client_;
    std::shared_mutex workspace_mutex_;
    Workspace workspace_;
};

struct Workspace {
    std::map<std::filesystem::path, AnalysedPackage> packages;
    std::map<std::string, std::string> overlays;      // document URI -> text
};

struct AnalysedPackage {
    TypedProgram program;
    std::vector<Diagnostic> diagnostics;
    SymbolIndex symbols;
};

// Added to leash_types (coordinate with WP-08/09 owners):
struct HoverInfo {
    std::string ty;
    EffectSet effects;
    std::optional<CapSet> caps;
    std::optional<std::string> doc;
};
std::optional<HoverInfo> hover_info(const TypedProgram& program, const std::filesystem::path& file,
                                    Position pos);
std::optional<Location> definition(const TypedProgram& program, const std::filesystem::path& file,
                                   Position pos);
```
Command contract used by the extension: `leash lsp` (stdio, no arguments; `leash lsp --version` from WP-22); `leash run <file> --flow <name>`; `leash runs list --json` (WP-22).

## Deliverables
- `src/lsp/` (CMake target `leash_lsp`) with `main.cpp` (invoked by the `leash lsp` subcommand) and `tests/lsp/integration_test.cpp`.
- `editors/vscode/` extension (TypeScript; `npm run build` then `vsce package` produces the `.vsix`), `syntaxes/`, `test/` grammar cases, `README.md` with install steps.
- `editors/tree-sitter-leash/` (optional).
- CI: `ctest --preset dev -R lsp`; `npm ci && npm test && npx vsce package` in `editors/vscode`, `.vsix` uploaded as an artifact.

## Acceptance criteria
- `ctest --preset dev -R lsp` passes: `diagnostic_on_type_error`, `hover_shows_type_and_effects` (hover on the `ask` in `examples/02` reports `Attribution` and `{llm}`), `definition_jumps_to_task`, `format_matches_cli`, `diagnostics_published_for_unopened_file`.
- Opening `examples/02_fix_bug.leash` in VS Code shows no false diagnostics; adding `let x: Int = "s"` shows an error within one second.
- `npx vscode-tmgrammar-test -g editors/vscode/syntaxes/leash.tmLanguage.json 'editors/vscode/test/*.leash'` passes; cases cover interpolation inside `"""` strings, `{{`, `r"…"`, `///` docs, `.Variant`, `! {llm, shell}`, and an identifier named `state` used in an expression (must not be highlighted as a keyword).
- The code lens on `flow FixEmptyPassword` runs `leash run examples/02_fix_bug.leash --flow FixEmptyPassword` in the integrated terminal.
- The journal viewer lists a run and its tool-trace row count equals `leash trace <run> --kind tool --json | jq length`.
- `vsce package` produces a `.vsix` in CI.
- `clang-tidy` over the compile database clean.

## Notes and pitfalls
- Analysis must never block hover; run it on a separate `asio::awaitable` task with cancellation on newer edits.
- Publish diagnostics for files outside the open document under their own URIs, and clear them when they become clean.
- The TextMate grammar cannot know contextual-keyword positions; accept a few false negatives rather than mis-highlighting identifiers.
- Format only on a successful parse; never rewrite a file with syntax errors.
- Pin the minimum `leash` version in the extension and surface a clear message when `leash` is missing from `PATH`.

## Dependencies
Hard: WP-09 (typed program with diagnostics and effects).  Soft: WP-07 (formatter for the complete grammar), WP-22 (`runs list --json`, `--version`).

## Estimated size
M (2–4 days).

# WP-07: Full parser and formatter (complete grammar)

Milestone: M2 · Size: M · Status: see `docs/status.md`

## Goal
After this WP, `leash-syntax` parses the entire language in `spec/grammar.ebnf`
and formats it idempotently, with comments preserved. All five programs in
`examples/` parse and round-trip. The parser never panics on any input. This is
the foundation every later front-end WP (types, effects, LIR, LSP) builds on.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `spec/grammar.ebnf` (all of it; note the hard/contextual keyword lists at the end)
- `docs/02-language-spec.md` §1, §2 (esp. §2.4 literals, §2.6 statement termination), §4
- `docs/01-language-tour.md` (for intent behind each construct)
- `docs/03-runtime-architecture.md` §3 (front end design: logos lexer, recursive descent + Pratt, error recovery, trivia)
- The WP-03 code and its tests (the v0 subset you are extending)

## Scope
- Lexer: raw strings (`r"…"`, `r"""…"""`), triple-quoted strings with common-indent stripping (spec §2.4), `{{`/`}}` literal braces, nested block comments, doc comments (`///`) as tokens attached to the next item, `DURATION` literals, `?.` and `..` tokens, newline tokens for the termination rule.
- Parser, items: `enum` with payload variants, `tool` (doc comment required; diagnostic E0110 if missing), `memory`, `task` with every section (`input`, `output`, `require`, `ensure` with `check … by … expect … within … on_timeout …`, `budget`, `allow`, `deny`), `flow` with an optional `state {…}` as first statement, `test "name" with {…} {…}`, generics on `type`/`enum`/`fn`, effect clauses `! {…}`.
- Parser, statements and expressions: `var`, compound assignment, `checkpoint`, `if`/`else if`, `match` with all pattern forms and guards, `for`/`while`/`loop`/`break value`/`continue`/`return`, `fail Kind(args)` vs `fail expr`, `attempt`/`recover` arms, `retry(n, on: …, backoff: …) {…}`, `parallel(limit: k) {…}`, `race {…}`, `ask`/`run` with named args, `spawn` of ask/run/call, `await`, all three `verify` forms, `complete`, `budget {…} {…}`, `allow {…} {…}`, lambdas (`x => e`, `(a, b) => {…}`), trailing lambdas, UFCS postfix, optional chaining, indexing, tuples, named record literals (`Triage {…}`), record shorthand, variant literals `.X(…)`, `Path` expressions, unary minus.
- Precedence per the grammar (Pratt table for binary operators; `not` above comparison; `and`/`or` lowest).
- Error recovery at item and statement boundaries; multiple diagnostics per file; diagnostics carry spans and codes `E01xx`.
- Formatter: complete coverage, 2-space indent, 100-column soft limit, trailing commas in multi-line lists/records/args, comment and doc-comment preservation through trivia, blank-line preservation (max 1), stable output for triple-quoted strings. `leash fmt` and `leash fmt --check`.
- AST equality ignoring spans (`ast::eq_modulo_spans`) for round-trip tests.
- Fuzz/property test: random token soups and random byte strings must not panic and must terminate within a bound.
- Update `spec/grammar.ebnf` with an "Ambiguity resolutions" section recording every decision below and any new one you make.

## Out of scope
- Name resolution, type checking, exhaustiveness (WP-08), effect inference (WP-09), LIR (WP-10).
- Semantic validation of `task` sections (e.g. duplicate check names) beyond syntax — emit nothing; WP-08/WP-15 own it.
- Tree-sitter or TextMate grammars (WP-23).

## Interfaces
```rust
// crate leash-syntax
pub fn lex(src: &str, file: FileId) -> Vec<Token>;                       // never fails; error tokens carry spans
pub fn parse_str(src: &str, file: FileId) -> ParseResult;               // ParseResult { program: ast::Program, diagnostics: Vec<Diagnostic> }
pub fn parse_file(path: &Path) -> io::Result<ParseResult>;
pub fn format_program(p: &ast::Program) -> String;                       // uses trivia stored in the AST
pub fn format_source(src: &str, file: FileId) -> Result<String, Vec<Diagnostic>>;
pub mod ast {
    pub struct Program { pub module: Option<Path>, pub items: Vec<Item>, pub trivia: Trivia }
    pub enum Item { Use, Const, Type, Enum, Fn, Tool, Agent, Memory, Task, Flow, Test }   // each a struct with NodeId, Span, doc: Option<Doc>
    pub enum Stmt { Let, Var, Assign, State, Checkpoint, Expr }
    pub enum Expr { Literal, Path, Record, NamedRecord, List, Tuple, Variant, Block, If, Match, For, While, Loop,
                    Return, Break, Continue, Fail, Attempt, Retry, Parallel, Race, Ask, Run, Spawn, Await, Verify,
                    Complete, Budget, Allow, Lambda, Binary, Unary, Field, MethodCall, Call, Index, OptChain, Interp }
    pub enum Pattern { Wildcard, Bind, Literal, Tuple, List { items, rest }, Record, Variant, FailureKind }
    pub enum Type { Prim, Named, List, Tuple, Record, Fn, Optional }
    pub struct TaskSection …; pub struct Check { name, method: Expr, expect: Option<Expr>, within: Option<Expr>, on_timeout: Option<Expr> }
    pub fn eq_modulo_spans(a: &Program, b: &Program) -> bool;
}
pub struct Diagnostic { pub code: &'static str, pub severity: Severity, pub message: String, pub labels: Vec<(Span, String)>, pub help: Option<String> }
```
Every node has `id: NodeId` (u32, unique per file, assigned in pre-order) and `span: Span`.

## Deliverables
- `crates/leash-syntax/src/{lexer,parser,ast,format,trivia}.rs` with the API above.
- `spec/grammar.ebnf` updated with the ambiguity section.
- Tests: `tests/parse_examples.rs` (all `examples/*.leash` parse with zero diagnostics), `tests/format_roundtrip.rs` (format twice, compare; parse(format(x)) ≡ x modulo spans), `tests/errors.rs` (snapshot tests with `insta` for ≥ 15 malformed inputs, each yielding a diagnostic with the expected code and a recovered parse of the remainder), `tests/fuzz.rs` (proptest, ≥ 2 000 cases in CI, more locally).
- CLI: `leash fmt`, `leash fmt --check`, `leash check --syntax`.

## Acceptance criteria
- `cargo nextest run -p leash-syntax` → all green, including the proptest suite.
- `for f in examples/*.leash; do leash check --syntax "$f"; done` → exit 0 for all five, no output.
- `leash fmt examples/ && git diff --exit-code examples/` → formatting the (already formatted) examples changes nothing; commit a formatted version first if needed.
- `leash fmt --check examples/` → exit 0; introduce a formatting error → exit 1 with the file name.
- `printf 'fn f( { let = = ) }\n' | leash check --syntax -` → exit 2, at least one `E01xx` diagnostic with a span, no panic.
- `spec/grammar.ebnf` contains a section "Ambiguity resolutions" listing at least the items under Notes.

## Notes and pitfalls
Decisions already made; record them in the grammar's ambiguity section:
- `{ x }` parses as a **block**; record shorthand needs `:` on the first field or a comma (`{ x, }`). `{ x: 1 }` and `{ x, y }` are records. Two-token lookahead after `{` suffices.
- `( e )` is grouping; `( e, )` and `( a, b )` are tuples; `()` is `Unit`; `(a, b) => …` is a lambda — look for `=>` after the matching `)`.
- Newline termination (spec §2.6): a line starting with `-`, `(`, `[`, `{` **terminates** the previous statement (all can start a statement). A trailing lambda must open on the same line as its call's `)`. A line starting with `.`, `?.`, a binary operator other than `-`, `else`, `recover`, or `=>` continues.
- After `fail`, an identifier in the FailureKind list followed by `(` or by end of expression is the kind form; anything else is `fail expr`. Same list after `recover`.
- `budget`/`allow` followed by `{` parses a record literal, then requires a `Block`; in a `task` body they take only the record.
- Contextual keywords (`input`, `state`, `check`, …) are identifiers outside their positions; write the parser so that `let state = 1` is legal at expression level but `state {` at the top of a flow body opens the state block.
- Triple-quoted string indent stripping: drop the first line if it is empty after `"""`; compute the minimum indentation over non-blank lines; strip it; drop a trailing newline immediately before `"""`. Interpolations do not affect indentation computation.
- Interpolation lexing must handle nested braces and nested strings inside `{…}`: `"a {f("x", {b: 1})} c"`.
- Use `logos` for the lexer and a hand-written parser; do not introduce a parser generator (ADR-0001 crate list).
- Formatter must never change semantics: assert `eq_modulo_spans` in tests, not string equality only.

Diagnostic codes owned by this WP (keep the table in `crates/leash-syntax/src/diag.rs` in sync):

| Code | Meaning |
|---|---|
| E0101 | unexpected token (with expected set) |
| E0102 | unterminated string / raw string / block comment |
| E0103 | invalid escape sequence |
| E0104 | unbalanced interpolation braces |
| E0105 | invalid number or duration literal |
| E0106 | unexpected end of file |
| E0110 | `tool` declaration without a doc comment |
| E0111 | `state` block not first in a flow body |
| E0112 | task section repeated or unknown |
| E0113 | `check` missing `by` |
| W0120 | statement continues across a newline in a way that is easy to misread (`-` on a new line) |

Formatter style rules (document them in `docs/10-cli-and-tooling.md` §1 under `leash fmt` when you land):
- Items separated by exactly one blank line; sections inside `task` separated by one blank line.
- Records, lists, argument lists and `parallel` bodies break one element per line when they exceed the width or contain a lambda.
- `ensure` checks always one per line; `by`/`expect`/`within`/`on_timeout` on continuation lines indented by two spaces when the check exceeds the width.
- `match` arms one per line; single-line arms allowed when the whole `match` fits in the width.
- Doc comments are never reflowed; line comments keep their position relative to the following token.

## Dependencies
Hard: WP-03. Soft: none.

## Estimated size
M (2–4 days).

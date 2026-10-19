# WP-07: Full parser and formatter (complete grammar)

Milestone: M2 · Size: M · Status: see `docs/status.md`

## Goal
After this WP, `leash_syntax` parses the entire language in `spec/grammar.ebnf`
and formats it idempotently, with comments preserved. All five programs in
`examples/` parse and round-trip. The parser never crashes on any input. This is
the foundation every later front-end WP (types, effects, LIR, LSP) builds on.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `spec/grammar.ebnf` (all of it; note the hard/contextual keyword lists at the end)
- `docs/02-language-spec.md` §1, §2 (esp. §2.4 literals, §2.6 statement termination), §4
- `docs/01-language-tour.md` (for intent behind each construct)
- `docs/03-runtime-architecture.md` §3 (front end design: hand-written lexer, recursive descent + Pratt, error recovery, trivia)
- The WP-03 code and its tests (the v0 subset you are extending)

## Scope
- Lexer: raw strings (`r"…"`, `r"""…"""`), triple-quoted strings with common-indent stripping (spec §2.4), `{{`/`}}` literal braces, nested block comments, doc comments (`///`) as tokens attached to the next item, `DURATION` literals, `?.` and `..` tokens, newline tokens for the termination rule.
- Parser, items: `enum` with payload variants, `tool` (doc comment required; diagnostic E0110 if missing), `memory`, `task` with every section (`input`, `output`, `require`, `ensure` with `check … by … expect … within … on_timeout …`, `budget`, `allow`, `deny`), `flow` with an optional `state {…}` as first statement, `test "name" with {…} {…}`, generics on `type`/`enum`/`fn`, effect clauses `! {…}`.
- Parser, statements and expressions: `var`, compound assignment, `checkpoint`, `if`/`else if`, `match` with all pattern forms and guards, `for`/`while`/`loop`/`break value`/`continue`/`return`, `fail Kind(args)` vs `fail expr`, `attempt`/`recover` arms, `retry(n, on: …, backoff: …) {…}`, `parallel(limit: k) {…}`, `race {…}`, `ask`/`run` with named args, `spawn` of ask/run/call, `await`, all three `verify` forms, `complete`, `budget {…} {…}`, `allow {…} {…}`, lambdas (`x => e`, `(a, b) => {…}`), trailing lambdas, UFCS postfix, optional chaining, indexing, tuples, named record literals (`Triage {…}`), record shorthand, variant literals `.X(…)`, `Path` expressions, unary minus.
- Precedence per the grammar (Pratt table for binary operators; `not` above comparison; `and`/`or` lowest).
- Error recovery at item and statement boundaries; multiple diagnostics per file; diagnostics carry spans and codes `E01xx`.
- Formatter: complete coverage, 2-space indent, 100-column soft limit, trailing commas in multi-line lists/records/args, comment and doc-comment preservation through trivia, blank-line preservation (max 1), stable output for triple-quoted strings. `leash fmt` and `leash fmt --check`.
- AST equality ignoring spans (`ast::eq_modulo_spans`) for round-trip tests.
- Fuzz/property test: random token soups and random byte strings must not crash and must terminate within a bound.
- Update `spec/grammar.ebnf` with an "Ambiguity resolutions" section recording every decision below and any new one you make.

## Out of scope
- Name resolution, type checking, exhaustiveness (WP-08), effect inference (WP-09), LIR (WP-10).
- Semantic validation of `task` sections (e.g. duplicate check names) beyond syntax — emit nothing; WP-08/WP-15 own it.
- Tree-sitter or TextMate grammars (WP-23).

## Interfaces
```cpp
// src/syntax/include/leash/syntax/ast.hpp
namespace leash::syntax::ast {

using Item = std::variant<UseDecl, ConstDecl, TypeDecl, EnumDecl, FnDecl, ToolDecl,
                          AgentDecl, MemoryDecl, TaskDecl, FlowDecl, TestDecl>;
using Stmt = std::variant<LetStmt, VarStmt, AssignStmt, StateStmt, CheckpointStmt,
                          ExprStmt>;
using Expr = std::variant<LiteralExpr, PathExpr, RecordExpr, NamedRecordExpr, ListExpr,
                          TupleExpr, VariantExpr, BlockExpr, IfExpr, MatchExpr, ForExpr,
                          WhileExpr, LoopExpr, ReturnExpr, BreakExpr, ContinueExpr, FailExpr,
                          AttemptExpr, RetryExpr, ParallelExpr, RaceExpr, AskExpr, RunExpr,
                          SpawnExpr, AwaitExpr, VerifyExpr, CompleteExpr, BudgetExpr,
                          AllowExpr, LambdaExpr, BinaryExpr, UnaryExpr, FieldExpr,
                          MethodCallExpr, CallExpr, IndexExpr, OptChainExpr, InterpExpr>;
using Pattern = std::variant<WildcardPattern, BindPattern, LiteralPattern, TuplePattern,
                             ListPattern, RecordPattern, VariantPattern, FailureKindPattern>;
using Type = std::variant<PrimType, NamedType, ListType, TupleType, RecordType, FnType,
                          OptionalType>;

struct Program {
    std::optional<Path> module;
    std::vector<Item> items;
    Trivia trivia;
};

struct ListPattern {
    std::vector<Pattern> items;
    std::optional<std::string> rest;
};

struct TaskSection { /* name, span, body */ };
struct Check {
    std::string name;
    Expr method;
    std::optional<Expr> expect;
    std::optional<Expr> within;
    std::optional<Expr> on_timeout;
};

bool eq_modulo_spans(const Program& a, const Program& b);

}  // namespace leash::syntax::ast

namespace leash::syntax {

// src/syntax/include/leash/syntax/lexer.hpp
std::vector<Token> lex(std::string_view src, FileId file);              // never fails; error tokens carry spans

// src/syntax/include/leash/syntax/diagnostic.hpp
struct Diagnostic {
    std::string_view code;                   // static literal, e.g. "E0102"
    Severity severity;
    std::string message;
    std::vector<std::pair<Span, std::string>> labels;
    std::optional<std::string> help;
};

// src/syntax/include/leash/syntax/parser.hpp
struct ParseResult {
    ast::Program program;
    std::vector<Diagnostic> diagnostics;
};

ParseResult parse_str(std::string_view src, FileId file);
std::expected<ParseResult, std::error_code> parse_file(const std::filesystem::path& path);

// src/syntax/include/leash/syntax/format.hpp
std::string format_program(const ast::Program& p);                      // uses trivia stored in the AST
std::expected<std::string, std::vector<Diagnostic>> format_source(std::string_view src,
                                                                 FileId file);

}  // namespace leash::syntax
```
Every variant alternative is a struct carrying `NodeId id` (`std::uint32_t`,
unique per file, assigned in pre-order) and `Span span`; item structs also
carry `std::optional<Doc> doc`. Alternatives take the suffix of their variant
(`ListExpr` beside `ListType` and `ListPattern`) because C++ has no per-enum
variant namespace.

## Deliverables
- `src/syntax/{lexer,parser,ast,format,trivia}.cpp` plus headers in
  `src/syntax/include/leash/syntax/`, exposing the API above.
- `spec/grammar.ebnf` updated with the ambiguity section.
- Tests: `tests/syntax/parse_examples_test.cpp` (all `examples/*.leash` parse with zero diagnostics), `tests/syntax/format_roundtrip_test.cpp` (format twice, compare; parse(format(x)) ≡ x modulo spans), `tests/syntax/errors_test.cpp` (golden snapshot tests with `expect_golden` for ≥ 15 malformed inputs, each yielding a diagnostic with the expected code and a recovered parse of the remainder), `tests/syntax/fuzz_parser.cpp` (libFuzzer target) and `tests/syntax/parser_property_test.cpp` (RapidCheck `rc::check`, ≥ 2 000 cases in CI, more locally).
- CLI: `leash fmt`, `leash fmt --check`, `leash check --syntax`.

## Acceptance criteria
- `ctest --preset dev -L syntax` → all green, including the property suite.
- `for f in examples/*.leash; do build/dev/src/cli/leash check --syntax "$f"; done` → exit 0 for all five, no output.
- `build/dev/src/cli/leash fmt examples/ && git diff --exit-code examples/` → formatting the (already formatted) examples changes nothing; commit a formatted version first if needed.
- `build/dev/src/cli/leash fmt --check examples/` → exit 0; introduce a formatting error → exit 1 with the file name.
- `printf 'fn f( { let = = ) }\n' | build/dev/src/cli/leash check --syntax -` → exit 2, at least one `E01xx` diagnostic with a span, no crash.
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
- The lexer is hand-written; do not introduce a parser generator (ADR-0011).
- C++ cannot prove exhaustiveness over variant alternatives: give each AST
  variant one visitor header (`ItemVisitor`, `StmtVisitor`, `ExprVisitor`,
  `PatternVisitor`, `TypeVisitor`) and a unit test that visits every
  alternative (ADR-0011).
- Formatter must never change semantics: assert `eq_modulo_spans` in tests, not string equality only.

Diagnostic codes owned by this WP (keep the table in `src/syntax/diag.cpp` in sync):

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

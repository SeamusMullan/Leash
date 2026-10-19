# WP-03: Lexer, parser, AST, formatter (v0 subset)

Milestone: M1 · Size: M · Status: see `docs/status.md`

## Goal
A front end for the v0 subset of Leash: enough syntax to parse, format and
check `examples/01_hello.leash`, with spans, node ids, error recovery and a
formatter. The full grammar arrives in WP-07; this WP builds the lexer, parser
skeleton, AST and diagnostics that WP-07 extends rather than rewrites. The
lexer is hand-written with a mode stack, the parser is recursive descent with a
Pratt loop for binary operators, and every AST node carries a span and a
per-file node id.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/adr/0011-implementation-language-cpp.md` (C++23, hand-written lexer, diagnostic renderer, libFuzzer targets)
- `spec/grammar.ebnf` (the contract; v0 is a strict subset of it)
- `docs/02-language-spec.md` §1, §2 (lexical structure, statement termination §2.6), §4.4 (calls, UFCS, trailing lambdas)
- `docs/03-runtime-architecture.md` §3 (front end), §5 (why stable ids matter)
- `docs/01-language-tour.md` §1, §3, §4, §7

## Scope
The v0 subset, precisely:
- Items: `module Path`, `use Path` and `use Path.{A, B}`, `let` constants,
  `fn` without generics and without effect clauses (parse `! {…}` and store it;
  do not interpret), `type Name = RecordType` where fields are primitives,
  `[T]`, `T?`, named types, and optional `where <expr>` refinement clauses
  (parsed, stored, not checked), `agent Name = Expr`, `task Name { input
  RecordType; output Type; budget RecordLit }` (other sections → "not yet
  supported" diagnostic), `flow Name(param) -> Type Block`.
- Statements: `let` (identifier patterns only), expression statements, the
  newline termination rule of docs/02 §2.6, optional `;`.
- Expressions: literals (Int with `_`, Float, Duration `500ms|30s|5m|2h`,
  String with `{expr}` interpolation and `{{`/`}}` escapes, `"""` multi-line
  with common-indent stripping, raw `r"…"`, `true false null`), paths, record
  literals `{ a: 1, b }`, named record literals `Agent { … }`, list literals,
  variant literals `.Name` (no payload), `if/else if/else`, blocks, binary and
  unary operators with the precedence table of the grammar, calls with
  positional and named arguments, field access, UFCS method calls
  `x.f(args)`, simple lambdas `x => expr`, `(a, b) => expr`, `x => { … }`,
  trailing lambda `f(args) { x => … }`, `ask Expr(args)`, `fail Kind("msg")`
  and `fail Kind("msg", expr)`, `complete Expr`.
- Comments `//`, nested `/* */`, doc comments `///` attached to the next item.
- Every construct in the grammar outside this subset must be *recognised* by
  keyword and produce diagnostic `E0100: '<construct>' is not supported yet
  (WP-07)` with a span, then recover at the next item.
- `src/syntax` (CMake target `leash_syntax`):
  - `include/leash/syntax/source.hpp`: `FileId`, `Span`, `NodeId`.
  - `token.hpp` and `lexer.cpp`: a hand-written lexer with a mode stack for
    string interpolation: `StringStart`, `StringPart`, `InterpStart`,
    `InterpEnd`, `StringEnd` token kinds. No lexer or parser generator.
  - `ast.hpp` and `ast.cpp`: every node has a `Span` and a `NodeId`; `NodeId`
    is a `std::uint32_t` assigned in pre-order per file. Each sum type is a
    `std::variant` with one visitor header (`ast/item_visit.hpp`,
    `ast/expr_visit.hpp`, `ast/type_visit.hpp`) and a unit test that visits
    every alternative.
  - `parser.cpp`: recursive descent; a Pratt loop for binary operators;
    `recover_to_item()` and `recover_to_stmt()`.
  - `diagnostic.hpp` and `diagnostic_render.cpp`: `Diagnostic` as defined in
    `docs/subagents/shared-interfaces.md`, codes `E01xx` syntax and `W01xx`
    warnings, plus a hand-written renderer that prints the source snippet, the
    underline for each label and the help text.
  - `format.cpp`: pretty printer for the subset, comments preserved via trivia
    attached to the following token, 2-space indent, trailing commas in
    multi-line literals, max width 100.
  - `tests/fuzz/lexer_fuzz.cpp` and `tests/fuzz/parser_fuzz.cpp`: libFuzzer
    entry points over the lexer and the parser.
- CLI: `leash check --syntax <files>` (exit 2 with diagnostics on error),
  `leash fmt [--check] <files>` in `src/cli/cmd/`.
- Fixtures: `tests/fixtures/parse/good/*.leash` (≥ 12 files covering each
  construct above, including tricky newline cases) and
  `tests/fixtures/parse/bad/*.leash` (≥ 8 files) with golden files of the
  rendered diagnostics (`expect_golden`), plus `examples/01_hello.leash` as a
  good fixture and `examples/02..05` as "unsupported yet" fixtures whose first
  diagnostic must name the right construct.

## Out of scope
Enums, `match`, loops, `var`/assignment, generics, `tool`, `memory`,
`state`, `checkpoint`, `ensure`/`require`/`allow`/`deny`, `parallel`,
`race`, `retry`, `attempt`, `spawn`/`await`, `verify`, `budget`/`allow`
expressions, tuple and list patterns, `test` declarations (all WP-07).
Name resolution and typing (WP-08). Interpreting anything.

## Interfaces
```cpp
// src/syntax/include/leash/syntax/source.hpp
struct FileId { std::uint32_t value = 0; };

struct Span {
    FileId file;
    std::uint32_t start = 0;    // byte offset, inclusive
    std::uint32_t end = 0;      // byte offset, exclusive
    bool operator==(const Span&) const = default;
};

struct NodeId {
    std::uint32_t value = 0;
    auto operator<=>(const NodeId&) const = default;
};

// src/syntax/include/leash/syntax/token.hpp
enum class TokenKind : std::uint8_t {
    // one enumerator per keyword and punctuation mark, plus:
    Identifier, IntLit, FloatLit, DurationLit,
    StringStart, StringPart, InterpStart, InterpEnd, StringEnd,
    Newline, Eof, Error
};

struct Token {
    TokenKind kind;
    Span span;
    std::string_view text;      // a slice of the source buffer; copy it if it must outlive the parse
};

// src/syntax/include/leash/syntax/lexer.hpp
// Never fails: an unrecognised byte becomes a token of kind `Error`.
std::vector<Token> lex(std::string_view src, FileId file);

// src/syntax/include/leash/syntax/ast.hpp
namespace leash::syntax::ast {

struct Program;
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

// The declaration structs (UseDecl … FlowDecl) are declared above `Item`.
using Item = std::variant<UseDecl, ConstDecl, TypeDecl, FnDecl, AgentDecl, TaskDecl, FlowDecl>;

struct Expr {
    NodeId id;
    Span span;
    std::variant<Lit, Path, RecordLit, NamedRecord, ListLit, Variant, IfExpr, Block, Unary,
                 Binary, Call, Method, Field, Lambda, Ask, FailExpr, Complete, Interp> kind;
};

struct TypeExpr {
    Span span;
    std::variant<NamedType, ListType, OptionalType, RecordType> kind;
};

struct Field { Ident name; TypeExpr ty; std::optional<ExprPtr> refine; };

struct Program {
    std::optional<Path> module;
    std::vector<Item> items;
    NodeId id;
    Span span;
};

// Every node carries `id` and `span` as its first two members. These visitors
// are the uniform accessors and the only code that knows the layout.
NodeId node_id(const Item& item);
Span node_span(const Item& item);
NodeId node_id(const Expr& expr);
Span node_span(const Expr& expr);

}  // namespace leash::syntax::ast

// src/syntax/include/leash/syntax/parser.hpp
struct Parsed {
    std::optional<ast::Program> program;
    std::vector<Diagnostic> diagnostics;
};
Parsed parse_program(std::string_view src, FileId file);

// src/syntax/include/leash/syntax/format.hpp
std::string format_program(const ast::Program& program, std::string_view src);   // idempotent

// src/syntax/include/leash/syntax/diagnostic.hpp — the canonical shape from
// docs/subagents/shared-interfaces.md; do not define a second one.
struct Diagnostic {
    std::string_view code;              // static literal, e.g. "E0102"
    Severity severity;
    std::string message;
    std::vector<std::pair<Span, std::string>> labels;
    std::optional<std::string> help;
};

// src/syntax/include/leash/syntax/diagnostic_render.hpp
void render(const Diagnostic& diagnostic, std::string_view src, std::ostream& out);
```
Diagnostics render through the hand-written renderer in
`diagnostic_render.cpp` with source snippets and an underline per label. Every
AST node exposes `id` and `span`: the uniform accessor is the two-member
convention plus the `node_id()` / `node_span()` visitors above.

## Deliverables
- `src/syntax` (target `leash_syntax`) with the headers and sources above, one
  Catch2 test file per module under `tests/syntax/`, registered in
  `tests/syntax/CMakeLists.txt` with CTest and the `syntax` label.
- `leash check --syntax` and `leash fmt` in `src/cli/cmd/`.
- Fixture directories and their golden files.
- The two libFuzzer targets under `tests/fuzz/`, built by a Clang-only `fuzz`
  preset that this WP adds to `CMakePresets.json`
  (`-fsanitize=fuzzer,address`).
- Notes appended to `spec/grammar.ebnf` for every ambiguity encountered and
  how the parser resolves it (e.g. `{` after an expression on the same line is
  a trailing lambda only if followed by `IDENT =>` or `( … ) =>`).

## Acceptance criteria
- `ctest --preset dev -L syntax` passes; golden tests cover all good and bad
  fixtures.
- `build/dev/src/cli/leash check --syntax examples/01_hello.leash` exits 0 and prints nothing (or `ok`).
- `for f in examples/0{2,3,4,5}*.leash; do build/dev/src/cli/leash check --syntax $f; done` exits 2 for each with a first diagnostic `E0100` naming (respectively) `enum`/`tool`/`ensure`… as recorded in the fixture goldens.
- `build/dev/src/cli/leash fmt --check examples/01_hello.leash` exits 0 after
  the example has been formatted once; `fmt(fmt(x)) == fmt(x)` is asserted in
  a test over every good fixture.
- Round trip: `parse(format(parse(x))) == parse(x)` modulo spans, asserted for
  every good fixture.
- A bad fixture with two independent errors yields two diagnostics (recovery works).
- `lex` on 1 MiB of random bytes terminates in under 1 s without crashing; the
  case is a RapidCheck `rc::check` inside a Catch2 test.
- Both libFuzzer targets build with `-fsanitize=fuzzer,address` and run for
  60 s each in CI without a crash.

## Notes and pitfalls
- Statement termination: emit `Newline` tokens from the lexer and let the
  parser decide (docs/02 §2.6). A newline followed by `.`, a binary operator,
  `)`, `]`, `}`, `,`, `=>`, `else` continues the expression. A `(` on a new
  line does not continue a call.
- Interpolation: the lexer must count braces inside `{…}` to find the end and
  must support nested strings inside interpolations (`"{f("x")}"`).
- Doc comments are program content (docs/02 §2.5): attach them to items; the
  formatter must preserve them verbatim.
- `NodeId`s are per-file pre-order; stable site ids are WP-10's job, but keep
  node ordering deterministic so WP-10 can derive structural paths.
- No parser generator: recursive descent with a Pratt loop for binary
  operators is the decision (docs/03 §3.2). The lexer is hand-written for the
  same reason.
- Grammar is normative for syntax (docs/02 preamble). If you must deviate,
  change `spec/grammar.ebnf` in the same PR and say why.
- `Token::text` is a `std::string_view` into the caller's source buffer. The
  buffer must outlive the token vector and the AST built from it; copy the
  slice when a token or an identifier is stored beyond the parse.
- Recursive descent recurses once per nesting level. Deeply nested input will
  overflow the stack before the fuzzer finds anything else, so cap nesting
  depth and emit a diagnostic instead of crashing.

## Dependencies
Hard: WP-01. Soft: WP-25 (`expect_golden` in `leash_testkit`; until that
target exists, keep the helper in `tests/support/golden.hpp` with the same
signature and move it when WP-25 lands).

## Estimated size
M (3–4 days).

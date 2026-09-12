# WP-03: Lexer, parser, AST, formatter (v0 subset)

Milestone: M1 · Size: M · Status: see `docs/status.md`

## Goal
A front end for the v0 subset of Leash: enough syntax to parse, format and
check `examples/01_hello.leash`, with spans, node ids, error recovery and a
formatter. The full grammar arrives in WP-07; this WP builds the lexer,
parser skeleton, AST and diagnostics that WP-07 extends rather than rewrites.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
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
- Crate `leash-syntax`: `token.rs`, `lexer.rs` (logos with a mode stack for
  string interpolation: `StringStart`, `StringText`, `InterpStart`,
  `InterpEnd`, `StringEnd` tokens), `ast.rs` (every node has `Span` and
  `NodeId`; `NodeId` is a `u32` assigned in pre-order per file), `parser/`
  (recursive descent; Pratt for binary operators; `recover_to_item()` and
  `recover_to_stmt()`), `diag.rs` (`Diagnostic` with `miette` derive, codes
  `E01xx` syntax, `W01xx` warnings), `fmt.rs` (pretty printer for the subset,
  comments preserved via trivia attached to the following token, 2-space
  indent, trailing commas in multi-line literals, max width 100).
- CLI: `leash check --syntax <files>` (exit 2 with diagnostics on error),
  `leash fmt [--check] <files>`.
- Fixtures: `tests/fixtures/parse/good/*.leash` (≥ 12 files covering each
  construct above, including tricky newline cases) and
  `tests/fixtures/parse/bad/*.leash` (≥ 8 files) with snapshot files of the
  rendered diagnostics (`insta`), plus `examples/01_hello.leash` as a good
  fixture and `examples/02..05` as "unsupported yet" fixtures whose first
  diagnostic must name the right construct.

## Out of scope
Enums, `match`, loops, `var`/assignment, generics, `tool`, `memory`,
`state`, `checkpoint`, `ensure`/`require`/`allow`/`deny`, `parallel`,
`race`, `retry`, `attempt`, `spawn`/`await`, `verify`, `budget`/`allow`
expressions, tuple and list patterns, `test` declarations (all WP-07).
Name resolution and typing (WP-08). Interpreting anything.

## Interfaces
```rust
pub struct FileId(pub u32);
#[derive(Copy, Clone, PartialEq, Eq, Hash)] pub struct Span { pub file: FileId, pub start: u32, pub end: u32 }
#[derive(Copy, Clone, PartialEq, Eq, Hash)] pub struct NodeId(pub u32);
pub fn lex(src: &str, file: FileId) -> Vec<Token>;                   // never fails; bad chars become Token::Error
pub fn parse_program(src: &str, file: FileId) -> Parsed;             // Parsed { program: Option<ast::Program>, diagnostics: Vec<Diagnostic> }
pub fn format_program(p: &ast::Program, src: &str) -> String;        // idempotent
pub mod ast { pub struct Program { pub module: Option<Path>, pub items: Vec<Item>, pub id: NodeId, pub span: Span }
              pub enum Item { Use(UseDecl), Const(ConstDecl), Type(TypeDecl), Fn(FnDecl), Agent(AgentDecl), Task(TaskDecl), Flow(FlowDecl) }
              pub enum Expr { Lit(Lit), Path(Path), Record(RecordLit), NamedRecord(Path, RecordLit), List(Vec<Expr>), Variant(Ident),
                              If(Box<IfExpr>), Block(Block), Unary(UnOp, Box<Expr>), Binary(BinOp, Box<Expr>, Box<Expr>),
                              Call(Box<Expr>, Vec<Arg>), Method(Box<Expr>, Ident, Vec<Arg>), Field(Box<Expr>, Ident),
                              Lambda(Vec<Ident>, Box<Expr>), Ask(Box<Expr>, Vec<Arg>), Fail(FailExpr), Complete(Box<Expr>), Interp(Vec<StrPart>) /* + id, span on every variant via a wrapper struct */ }
              pub struct TypeExpr { pub kind: TypeKind, pub span: Span }  pub enum TypeKind { Named(Path), List(Box<TypeExpr>), Optional(Box<TypeExpr>), Record(Vec<Field>) }
              pub struct Field { pub name: Ident, pub ty: TypeExpr, pub refine: Option<Expr> } }
pub struct Diagnostic { pub code: &'static str, pub severity: Severity, pub message: String, pub labels: Vec<(Span, String)>, pub help: Option<String> }  // one label for single-span diagnostics
```
Diagnostics render through `miette` with source snippets. Every AST node
exposes `id()` and `span()` (implement via a small trait `Node`).

## Deliverables
- `crates/leash-syntax` with the modules above and unit tests per module.
- `leash check --syntax`, `leash fmt` in `leash-cli/src/cmd/`.
- Fixture directories and `insta` snapshots.
- Notes appended to `spec/grammar.ebnf` for every ambiguity encountered and
  how the parser resolves it (e.g. `{` after an expression on the same line is
  a trailing lambda only if followed by `IDENT =>` or `( … ) =>`).

## Acceptance criteria
- `cargo nextest run -p leash-syntax` passes; snapshot tests cover all good
  and bad fixtures.
- `cargo run -p leash-cli -- check --syntax examples/01_hello.leash` exits 0 and prints nothing (or `ok`).
- `for f in examples/0{2,3,4,5}*.leash; do cargo run -q -p leash-cli -- check --syntax $f; done` exits 2 for each with a first diagnostic `E0100` naming (respectively) `enum`/`tool`/`ensure`… as recorded in the fixture snapshots.
- `cargo run -p leash-cli -- fmt --check examples/01_hello.leash` exits 0 after
  the example has been formatted once; `fmt(fmt(x)) == fmt(x)` is asserted in
  a test over every good fixture.
- Round trip: `parse(format(parse(x))) == parse(x)` modulo spans, asserted for
  every good fixture.
- A bad fixture with two independent errors yields two diagnostics (recovery works).
- `lex` on 1 MiB of random bytes terminates in under 1 s without panicking (proptest smoke).

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
- Do not hand-roll a table-driven parser generator; recursive descent with
  Pratt is the decision (docs/03 §3.2).
- Grammar is normative for syntax (docs/02 preamble). If you must deviate,
  change `spec/grammar.ebnf` in the same PR and say why.

## Dependencies
Hard: WP-01. Soft: none.

## Estimated size
M (3–4 days).

# Shared Interfaces: who owns which type

Status: normative for work-package briefs. Several briefs were written in
parallel and sketched the same type twice; this file decides the single owner
and the canonical shape. If a brief disagrees with this file, this file wins;
fix the brief in the PR that notices it.

Rule of thumb: a type is defined in the lowest module that needs it and
re-exported upward. Modules never define a second copy "of the same shape".

Upward visibility means including the owning module's public header directly.
There are no forwarding headers and no module-level re-export.

## Module ownership

| Module | CMake target | Owns | Notes |
|---|---|---|---|
| `src/spec` | `leash_spec` | `Money`, `Usage`, `PayloadRef`, `Budget`, `CapSet`, `Globs`, `ShellPattern`, `HostPattern`, `FailureKind`, `Profile`, price table, and the embedded JSON schemas | WP-02. Depends only on nlohmann/json and valijson; glob and shell pattern matching is implemented in `leash_policy`. Every other module depends on it. |
| `src/syntax` | `leash_syntax` | `Span`, `NodeId`, `Diagnostic`, `Severity`, AST, lexer, parser, formatter | WP-03/07 |
| `src/types` | `leash_types` | `Ty`, `TypeId`, `TypedProgram`, `Effect`, `EffectSet`, `EffectAnalysis`, `CapSetLit`, `HoverInfo`, JSON Schema generation | WP-08/09 |
| `src/ir` | `leash_ir` | `Lir`, `LirNode`, `SiteId`, lowering | WP-10 |
| `src/runtime` | `leash_runtime` | `Value`, `Failure`, `EffectRequest`, `EffectResult`, `EffectGateway`, `Journal`, `BudgetStack`, `CapStack`, `Scheduler`, `Handle` | WP-04/12/16/20 |
| `src/store` | `leash_store` | content-addressed store, run directory, manifest | WP-06 |
| `src/effects` | `leash_effects` | `EffectHandler`, `EffectCtx`, sandbox, built-in tools, MCP client, tool middleware | WP-11/14 |
| `src/policy` | `leash_policy` | shell tokenisation and command matching, path policy, host matching (`Permit`) | WP-11/12; split out so `leash_types` can reuse matching without depending on the runtime |
| `src/drivers` | `leash_drivers` | `Driver`, `DriverCtx`, `DriverCaps`, `AskRequest/Response`, `RunRequest/Response`, `StopReason`, `DriverError`, `EventSink`, `DriverEvent`, subprocess helpers | WP-05/13/18/19 |
| `src/trace` | `leash_trace` | trace writers, `Evidence` serialisation, report, outcome classification | WP-06/15 |
| `src/testkit` | `leash_testkit` | fixtures, scripted mocks, journal assertions | WP-25 |

`leash_spec`, `leash_store`, `leash_policy` and `leash_testkit` are created
empty by WP-01. `leash_policy` is filled by whichever of WP-09 and WP-11 lands
first (both need command and path matching) and consumed by WP-12.
`leash_testkit` is filled by WP-25.
`docs/03-runtime-architecture.md` §2 lists the same set.

## Canonical shapes

```cpp
// src/spec/include/leash/spec/money.hpp
class Money {
public:
    static Money usd(double v);
    static Money micros(std::int64_t v);
    std::optional<Money> checked_sub(Money other) const;
    std::string display() const;         // "$3.1200"

private:
    std::int64_t micros_ = 0;            // exact micro-USD; int64_t covers ±9.2e12 USD
};

struct Usage {                           // what a driver reports for one turn or one call
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
    std::uint64_t cache_read_input_tokens = 0;
    std::uint64_t cache_creation_input_tokens = 0;
    std::optional<Money> cost;           // set when the backend reports cost (Claude Code `total_cost_usd`)
    std::string model;

    std::uint64_t total_tokens() const;
};

struct PayloadRef {                      // `Cas` holds "b3:<hex>"
    struct Inline { nlohmann::json value; };
    struct Cas    { std::string hash; };

    std::variant<Inline, Cas> value;
};

struct Budget {
    std::optional<std::uint64_t> tokens;
    std::optional<Money> cost;
    std::optional<std::chrono::nanoseconds> wall;
    std::optional<std::uint32_t> tool_calls;
    std::optional<std::uint32_t> llm_calls;
    std::optional<std::uint32_t> turns;
};

struct CapSet {
    Globs fs_read;
    Globs fs_write;
    std::vector<ShellPattern> shell;
    std::vector<HostPattern> net;
    bool human = false;
    bool spawn = false;
    std::vector<std::string> env;
    std::vector<std::string> secrets;
};
```

`PayloadRef` has one `std::visit` visitor, `PayloadRefVisitor`, declared beside
the variant in the same header; do not open-code a second one. Every variant in
this file follows the same rule.

Counters that are *consumed* rather than *reported* (`tool_calls`, `llm_calls`,
`turns`) live in `BudgetStack` accounting, not in `Usage`. A driver reports
`Usage`; the runtime converts it into budget consumption.

```cpp
// src/syntax/include/leash/syntax/diagnostic.hpp
struct Diagnostic {
    std::string_view code;               // static literal, e.g. "E0102"
    Severity severity;
    std::string message;
    std::vector<std::pair<Span, std::string>> labels;
    std::optional<std::string> help;
};
```
Single-span diagnostics use one label. `leash_types` and `leash_lsp` reuse this
type; neither defines its own.

```cpp
// src/runtime/include/leash/runtime/failure.hpp
struct Failure {
    FailureKind kind;
    std::string message;
    std::optional<nlohmann::json> data;
    Site site;
    std::unique_ptr<Failure> cause;      // null when there is no cause
};
```
`FailureKind` (the eleven kinds of `docs/02-language-spec.md` §9.1) lives in
`leash_spec` so drivers, effects and the trace layer can name kinds without
depending on the runtime. `Failure` itself lives in `leash_runtime`; lower
modules return their own failure types (`DriverError`, `ToolError`,
`SpecError`) and the runtime maps them to kinds at the effect gateway
(`docs/02` §9.6).

## Context objects

- `EffectCtx` (`leash_effects`) is what an **effect handler** gets: active
  `CapStack` view, `BudgetStack` view, sandbox handle, run directory, trace
  sink, `asio::cancellation_signal`.
- `DriverCtx` (`leash_drivers`) is what a **driver** gets: `run_id`,
  `asio::cancellation_signal`, payload sink, price table and the translated
  `DriverCapTranslation`. Drivers never see the runtime's stacks. The LLM
  effect handler builds a `DriverCtx` from its `EffectCtx`.

`docs/09-drivers-and-tools.md` §1 uses `DriverCtx` for this reason.

## Naming collisions to avoid

- `Workspace` means the LSP's analysed package set (`leash_lsp`). The sandbox's
  per-run working copy is `RunWorkspace` (`leash_effects`), never `Workspace`.
- `Context` is the Leash value produced by `select` (`leash_runtime`). Context
  objects are always `*Ctx`.
- `Check` is the task-level verification check (`leash_spec` + `leash_trace`).
  `leash check` the command is unrelated; do not name a C++ type `Check` in
  `leash_cli`.

# WP-02: Machine-readable spec: LIR, journal, trace schemas, profiles, prices

Milestone: M0 · Size: M · Status: see `docs/status.md`

## Goal
Turn the prose contracts in the design documents into JSON Schema and TOML
files that the runtime, trace layer, CLI and tests validate against, embedded
in the `leash_spec` module. Later WPs code against these files; a change to a
format is a change to a schema first.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/04-determinism-and-replay.md` §3 (journal entry), §9 (manifest)
- `docs/11-episode-package-and-eval.md` §1 (eight traces), §2
- `docs/06-verification-and-evidence.md` §3 (Evidence), §6 (outcome labels)
- `docs/05-harness-model.md` §3 (profile visibility matrix)
- `docs/02-language-spec.md` §11 (type → JSON Schema), `spec/grammar.ebnf` (node kinds)
- `docs/07-budgets-and-capabilities.md` §1.2 (usage → cost)
- `docs/09-drivers-and-tools.md` §8 (price table)

## Scope
- `spec/journal.schema.json` (draft 2020-12): one entry object exactly as
  docs/04 §3, with `status ∈ {started, ok, failed, cancelled, timeout}`,
  `kind` enumerated (`llm.ask llm.run tool shell fs.read fs.write net human
  clock rand env checkpoint spawn race complete fail state`), `path` as an
  array of `[site_id, occurrence]` pairs, `usage`, `driver`, `caps`,
  `budget_after`, `failure` (a `Failure` object with `kind` enumerated),
  `reused_from`, `parent`, `children`, `note`. Payload references are
  `{"cas": "b3:<64 hex>"}`; document the 8 KiB rule in `description`.
- `spec/manifest.schema.json` from docs/04 §9.
- `spec/traces/{action,tool,context,verification,attribution,intervention,entropy,outcome}.schema.json`
  from docs/11 §1; `verification` entries are the Evidence object of docs/06 §3
  including `confidence` and `scope`; `outcome` uses the five labels of
  docs/06 §6 plus `justification` and `metrics` (the per-episode metrics of
  docs/11 §3, all optional).
- `spec/lir.schema.json`: versioned envelope `{lir_version, module, items[]}`
  and an enumeration of node kinds (`Let Var Assign If Match For While Loop
  Return Break Continue Fail Attempt Retry Parallel Race Ask Run Spawn Await
  Verify Complete Budget Allow Lambda Call Field Index Literal Record List
  Tuple Variant Block Checkpoint State`), each with `id`, `span`, `ty`,
  `effects`, and `site_id` on effectful kinds. Detailed per-kind fields are
  left as `additionalProperties: true` for WP-10 to tighten.
- `spec/profiles/h0.toml … h3.toml`: `[profile] name`, `[artifacts]` with a
  boolean per artifact key from the docs/05 §3 table
  (`tool_registry`, `test_command_registry`, `tool_usage_protocol`,
  `project_memory`, `task_state`, `context_selection_protocol`,
  `check_registry`, `reproduction_protocol`, `attribution_protocol`,
  `verification_protocol`, `report_template`), and `[system_prompt] sections = [...]`.
- `std/prices.toml`: `[models."<id>"] input, output, cache_read, cache_write`
  in USD per million tokens, with `verified_on = "2026-09-12"` and a `source`
  comment. Values: `claude-opus-5` 5/25, `claude-opus-4-8` 5/25,
  `claude-opus-4-7` 5/25, `claude-opus-4-6` 5/25, `claude-sonnet-5` 2/10,
  `claude-sonnet-4-6` 3/15, `claude-haiku-4-5` 1/5, `claude-fable-5-1` 10/50,
  `claude-fable-5` 10/50. Cache read = 0.1 × input and cache write = 1.25 ×
  input unless overridden; override `claude-fable-5-1` cache_read = 0.25 with a
  comment that the API reference lists that figure and it must be re-verified.
- `spec/README.md`: versioning rules (`journal_version`, `lir_version`,
  trace schema `$id` with version), how to bump, and the rule "schema first".
- `src/spec`: embeds every file under `spec/` and `std/prices.toml` at build
  time. A CMake script turns each file into a raw string literal in a generated
  `.cpp`; each schema is parsed once on first use into a `valijson::Schema`
  held in a function-local `static const`, which C++ initialises thread-safely.
  Exposes the validators and typed loaders below. Headers live at
  `src/spec/include/leash/spec/*.hpp`.
- Fixtures: `spec/fixtures/journal/hello.jsonl` (a plausible 6-entry journal
  for `examples/01_hello.leash`: started/ok pairs for one `llm.ask`, a
  `complete`), `spec/fixtures/journal/invalid.jsonl`, one valid fixture per
  trace file, `spec/fixtures/manifest.json`.

## Out of scope
Producing these files at run time (WP-04, WP-06). Tightening per-node LIR
fields (WP-10). Price table auto-update. Non-USD currencies.

## Interfaces
```cpp
// src/spec/include/leash/spec/money.hpp
class Money {
public:
    static Money usd(double v);
    static Money micros(std::int64_t v);
    std::optional<Money> checked_sub(Money other) const;
    std::string display() const;            // "$3.1200"

private:
    std::int64_t micros_ = 0;               // exact micro-USD
};

// src/spec/include/leash/spec/usage.hpp
struct Usage {
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
    std::uint64_t cache_read_input_tokens = 0;
    std::uint64_t cache_creation_input_tokens = 0;
    std::optional<Money> cost;              // set when the backend reports cost
    std::string model;

    std::uint64_t total_tokens() const;
};

// src/spec/include/leash/spec/spec_error.hpp
struct SchemaError { std::string message; std::string json_pointer; };
struct ParseError { std::string message; };
struct UnknownProfileError { std::string name; };
using SpecError = std::variant<SchemaError, ParseError, UnknownProfileError>;

// src/spec/include/leash/spec/journal.hpp
namespace leash::spec::journal {
inline constexpr std::uint32_t kVersion = 1;
std::expected<void, SpecError> validate_line(std::string_view line);
}

// src/spec/include/leash/spec/manifest.hpp
namespace leash::spec::manifest {
std::expected<void, SpecError> validate(const nlohmann::json& value);
}

// src/spec/include/leash/spec/lir.hpp
namespace leash::spec::lir {
inline constexpr std::uint32_t kVersion = 1;
std::expected<void, SpecError> validate(const nlohmann::json& value);
}

// src/spec/include/leash/spec/traces.hpp
namespace leash::spec::traces {
enum class TraceKind {
    action, tool, context, verification, attribution, intervention, entropy, outcome
};
std::expected<void, SpecError> validate(TraceKind kind, const nlohmann::json& value);
}

// src/spec/include/leash/spec/profile.hpp
namespace leash::spec::profiles {
enum class ArtifactKey { tool_registry, test_command_registry, memory,
                         task_state, check_registry, report_template };
enum class ProtocolKey { tool_usage, context_selection, bug_reproduction,
                         failure_attribution, verification };

struct Profile {
    std::string name;
    std::set<ArtifactKey> artifacts;        // canonical order
    std::vector<ProtocolKey> protocols;
};

// "h0".."h3" are embedded; any other name is read as <name>.toml from the
// current directory.
std::expected<Profile, SpecError> load(std::string_view name);
}

// src/spec/include/leash/spec/prices.hpp
namespace leash::spec::prices {
struct ModelPrice {
    double input = 0.0;
    double output = 0.0;
    double cache_read = 0.0;
    double cache_write = 0.0;
};

class PriceTable {
public:
    static PriceTable default_embedded();
    std::expected<PriceTable, SpecError> with_overrides(std::string_view toml) const;
    std::optional<ModelPrice> price_for(std::string_view model) const;
    std::optional<Money> cost_usd(std::string_view model, const Usage& usage) const;
};
}
```
`SchemaError::json_pointer` is the RFC 6901 pointer of the first failure, for
example `/usage/input_tokens`. `Usage` and `Money` are the canonical shapes of
`docs/subagents/shared-interfaces.md`; that file wins if this block drifts.

## Deliverables
- The files under `spec/`, `spec/traces/`, `spec/profiles/`, `spec/fixtures/`, `std/prices.toml`, `spec/README.md`.
- `src/spec` with the API above and tests.

## Acceptance criteria
- `ctest --preset dev -L spec` passes with tests that: every line of
  `spec/fixtures/journal/hello.jsonl` validates; `invalid.jsonl` fails with a
  `SchemaError` whose `json_pointer` names the offending field; each trace
  fixture validates against its schema and fails against a different one;
  `profiles::load("h3")` exposes all eleven artifacts and `load("h0")` none;
  `prices::cost_usd("claude-opus-5", Usage{.input_tokens = 1'000'000,
  .output_tokens = 100'000})` equals `$7.50`; `cost_usd` for an unknown model
  returns `std::nullopt`.
- `python3 -c "import json,glob;[json.load(open(f)) for f in glob.glob('spec/**/*.json', recursive=True)]"` exits 0 (all files are valid JSON).
- Schemas declare `$schema: https://json-schema.org/draft/2020-12/schema` and a versioned `$id`.
- `spec/README.md` explains how to bump each version.

## Notes and pitfalls
- Use valijson: parse each schema once into a `valijson::Schema` and reuse the
  validator. Do not re-read or re-parse the schema per line or per entry.
- Keep enumerations in one place per schema (`$defs`) so the runtime can
  reuse them through `nlohmann::json` `from_json` overloads with identical
  spellings.
- Money must not be a float in schemas: represent `cost_usd` as a decimal
  string (`"0.2450"`); the C++ `Money` type is a micro-dollar integer.
- Timestamps are RFC 3339 UTC with millisecond precision.
- Profile files are data on purpose (docs/05 §3): packages may add their own.
- valijson reports a failure message and the failing subschema, not an RFC 6901
  pointer. `SchemaError::json_pointer` is derived by a path-recording
  validation pass in `leash_spec`: walk object properties in order, validate
  each subschema, and return the pointer of the first failure. Do not rely on
  valijson to report a pointer.

## Dependencies
Hard: WP-01. Soft: none.

## Estimated size
M (2–3 days).

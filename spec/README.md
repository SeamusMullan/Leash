# The machine-readable Leash specification

This directory is normative. The prose in `docs/` explains intent; the JSON
Schema and TOML files here are what the runtime, trace layer, CLI and tests
validate against. **A change to a format is a change to a schema first.** Land
the schema change (and a fixture) before the code that consumes it.

## Files

| Path | Contract |
|---|---|
| `journal.schema.json` | One line of `journal.jsonl` (docs/04 §3) |
| `manifest.schema.json` | `.leash/runs/<run_id>/manifest.json` (docs/04 §9) |
| `lir.schema.json` | Versioned LIR envelope and node kinds |
| `traces/*.schema.json` | The eight episode traces (docs/11 §1) |
| `profiles/h0.toml` … `h3.toml` | Harness visibility matrix (docs/05 §3) |
| `fixtures/` | Valid and invalid examples used by `tests/spec` |
| `grammar.ebnf` | Surface grammar (owned by `leash_syntax`) |

`std/prices.toml` is the other normative data file; it is embedded with this
directory even though it lives under `std/`.

## Versioning

Every format carries a version, and every version is bumped in its own commit
with a fixture that exercises the new shape.

### `journal_version`

The integer is written into every manifest and checked by replay. Bump
`kVersion` in `src/spec/include/leash/spec/journal.hpp`, the `const` in
`manifest.schema.json`'s `journal_version` property, and change
`spec/journal.schema.json`'s `$id` from `.../journal/v1.schema.json` to
`.../journal/v2.schema.json`. Old journals remain readable by old runtimes;
replay refuses a journal whose version differs from the runtime's unless
`--force` is passed (docs/04 §9).

Bump only for a change that a previous runtime cannot interpret: a new
required field, a removed field, a changed meaning, or a new status/kind that
changes replay. Adding an optional field with a harmless default is a minor
change and still requires the `$id` minor segment to move (`v1.1`).

### `lir_version`

`lir_version` is an integer in the LIR envelope and in the manifest. Bump
`kVersion` in `src/spec/include/leash/spec/lir.hpp`, the `const` in
`lir.schema.json`, and the schema `$id` (`.../lir/v2.schema.json`). A journal is
replayable only with the same `lir_version` **and** the same `lir_hash`;
`leash replay` refuses otherwise unless `--force` (docs/04 §9). Per-node
payload fields are intentionally open in v1 (`additionalProperties: true`);
WP-10 tightens them without changing the envelope version until a field
becomes required.

### Trace schemas

Each trace schema has its own `$id` with a `vN` segment, for example
`https://leash.dev/spec/traces/outcome/v1.schema.json`. Traces are regenerable
from the journal (`leash report --regenerate`), so they are never the source of
truth and may evolve without invalidating old runs. Bump the `vN` segment and
the schema file together when a field becomes required or changes meaning; add
an optional field without a version bump only when consumers already tolerate
unknown fields.

### Profiles and prices

`spec/profiles/*.toml` are data, not code: packages may add their own profile
files. Adding a profile does not change a version. `std/prices.toml` is dated by
`verified_on`; update that date and the `source` comment when prices change.
The price table has no schema version because it is read at start-up and never
replayed from a journal.

## JSON Schema dialect and `$ref` style

Schemas declare `$schema: https://json-schema.org/draft/2020-12/schema` and a
versioned `$id`. They are validated at runtime by valijson 1.1.3, whose newest
parser is **draft 7**. The schemas therefore use only draft-7-compatible
keywords and keep shared enumerations in `definitions`, referenced as
`$ref: "#/definitions/..."`. Do not use `$defs`: valijson's draft-7 parser does
not resolve it. Do not use 2020-12-only keywords (`prefixItems`,
`unevaluatedProperties`, `$dynamicRef`). The `$schema` value documents the
intended dialect; `definitions` is the portable spelling that both dialects
accept.

## Representation rules

- Money is never a JSON float. Amounts are decimal strings with four
  fractional digits (`"0.2450"`); the C++ `Money` type is an exact micro-USD
  integer.
- Timestamps are RFC 3339 UTC with millisecond precision
  (`2026-09-12T10:00:00.123Z`).
- Payloads above 8 KiB are stored in the CAS and referenced as
  `{"cas": "b3:<64 hex>"}`. Smaller payloads may be inlined.
- Enumerations are spelled once per schema, in `definitions`, so the runtime's
  `nlohmann::json` conversions and the schema cannot drift.

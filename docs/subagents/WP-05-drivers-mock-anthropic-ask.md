# WP-05: Mock driver and the Anthropic streaming spike

Milestone: M1 · Size: M · Status: see `docs/status.md`

## Goal
The `Driver` interface and the in-process `mock` driver, so programs and tests
run end to end with no network, plus a streaming spike that proves the
Boost.Beast SSE path and the usage accounting against the Anthropic Messages
API before the parser and interpreter depend on them. ADR-0004 puts the mock
first in the driver build order, and roadmap v0.2 makes mock plus Hermes the M1
tracer bullet. The registered `anthropic` driver — credentials, configuration,
retries, refusal mapping and the `ask`/`run` surface — moves to WP-13; this WP
only proves the transport.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/09-drivers-and-tools.md` §1 (contract), §2 (anthropic request construction), §6 (mock), §8 (prices)
- `docs/02-language-spec.md` §6.2 (`ask` semantics), §11 (schemas the driver receives)
- `docs/04-determinism-and-replay.md` §3 (what the journal needs from a driver: usage, model, notes)
- `docs/07-budgets-and-capabilities.md` §1.2 (usage fields → cost)
- `docs/adr/0004-drivers.md` (driver contract, build order), `docs/adr/0011-implementation-language-cpp.md` (Beast over `asio::ssl`, the spike gate)
- `docs/12-roadmap.md` (v0.2, M1 exit criterion)
- The Claude API request shapes recorded in this repository's docs are current as of 2026-06; re-check `anthropic-version`, betas and `output_config` in the official docs before coding.

## Scope
- `src/drivers` (CMake target `leash_drivers`, no dependency on `leash_runtime`):
  - `driver.hpp`: `Driver` interface, request/response types, `DriverCaps`,
    `DriverCtx`, `DriverError`, `PayloadSink`, `DriverRegistry` helpers,
    `Effort`, `StopReason`, and `Usage` re-exported from `leash_spec`.
  - `mock.cpp`: `MockDriver::from_script(path)`. Script format (JSON):
    ```json
    { "version": 1,
      "entries": [
        { "match": { "seq": 0 }, "response": { "json": [ {"name": "Leash", "rationale": "…"} ] }, "usage": { "input_tokens": 120, "output_tokens": 40 } },
        { "match": { "prompt_contains": "Suggest three" }, "response": { "text": "not json" } },
        { "match": { "prompt_hash": "b3:…" }, "fail": { "kind": "http", "status": 429, "times": 1 }, "latency_ms": 50 },
        { "match": { "any": true }, "fail": { "kind": "refusal", "category": "cyber" } }
      ]
    }
    ```
    Matching order: first entry whose `match` holds; `seq` counts calls on
    this driver instance; `prompt_hash` is `b3:` over the exact prompt string;
    `fail.times` limits how often an entry fails before falling through.
    Unmatched prompts return `DriverError::MockUnmatched { prompt_excerpt }`.
    `latency_ms` waits on an `asio::steady_timer` on the run's `io_context`, so
    cancellation and timeouts can be tested. `run` returns
    `DriverError::Unsupported`.
  - `anthropic/wire.cpp`: builds the request body for the `ask` subset of
    docs/09 §2: `model`, `max_tokens`, `stream: true`, `system` as blocks with
    one `cache_control` breakpoint on the last block, `messages`,
    `thinking: {"type":"adaptive"}`, and
    `output_config: {"effort": …, "format": {"type":"json_schema","schema": …}}`
    when a schema is present. No configuration file and no defaults beyond the
    constants in `anthropic/consts.hpp`.
  - `anthropic/sse.cpp`: `SseDecoder`, a pure state machine over received
    bytes. It decodes `message_start`, `content_block_start/delta/stop`,
    `message_delta` (usage, `stop_reason`, `stop_details`), `message_stop`,
    `ping` and `error` into `SseEvent`, and reassembles text blocks, plus
    `json` when a schema was requested. `SseClient<Stream>` drives it over
    Boost.Beast: `http::response_parser<http::buffer_body>`, `http::read_some`
    in a loop, body chunks fed to the decoder as they arrive.
  - `anthropic/usage.cpp`: turns `message_start.usage` and
    `message_delta.usage` into `leash_spec`'s `Usage` (input, output, cache
    read and cache creation tokens, model). The spike does not compute money;
    the runtime uses `leash_spec`'s price table.
  - `anthropic/spike.hpp`: `spike::ask(const SpikeConfig&, const AskRequest&,
    DriverCtx&)` returning
    `asio::awaitable<std::expected<AskResponse, DriverError>>`. It is not
    registered in `DriverRegistry` and does not implement `Driver`; WP-13
    wraps it into the `anthropic` driver.
  - Large payloads: raw response bodies above 8 KiB are handed to
    `DriverCtx.payload_sink.put()` and referenced by `AskResponse.raw`.
  - A refusal (`stop_reason == "refusal"`) produces
    `StopReason::Refusal { category, explanation }` in the spike result. The
    mapping to a `Model` failure is WP-13's.
  - Structured output: when `schema` is present the spike returns `json`
    parsed from the text content. It never validates schemas itself beyond
    JSON parsing.

## Out of scope
Registering the `anthropic` driver, `leash.toml` configuration, credential
lookup and the `leash doctor` credentials row, retry and backoff policy,
refusal-to-`Failure` mapping, fallbacks and the betas they need (all WP-13).
`run`/tools/tool loop (WP-13), Claude Code and Codex drivers (WP-18/19),
Managed Agents, compaction, task budgets, prompt-caching strategy beyond the
single system breakpoint, batch API.

## Interfaces
```cpp
// src/drivers/include/leash/drivers/driver.hpp
class Driver {
public:
    virtual ~Driver() = default;

    virtual std::string id() const = 0;   // "<name>/<model>/<config hash>/<version>"
    virtual DriverCaps caps() const = 0;  // { structured_output, tools, effort,
                                          //   sessions, cost_reporting, cancellation }

    virtual asio::awaitable<std::expected<AskResponse, DriverError>>
    ask(const AskRequest& req, DriverCtx& ctx) = 0;

    virtual asio::awaitable<std::expected<RunResponse, DriverError>>
    run(const RunRequest& req, DriverCtx& ctx, EventSink& sink) = 0;   // Unsupported here
};

struct DriverCaps {
    bool structured_output = false;
    bool tools = false;
    bool effort = false;
    bool sessions = false;
    bool cost_reporting = false;
    bool cancellation = false;
};

struct AskRequest {
    std::vector<PromptBlock> system;
    std::string prompt;
    std::optional<nlohmann::json> schema;
    std::optional<Effort> effort;
    std::optional<std::string> model;
    std::optional<std::uint32_t> max_tokens;
    std::optional<std::string> context;
    std::vector<Blob> attachments;
};

struct AskResponse {
    std::string text;
    std::optional<nlohmann::json> json;
    Usage usage;
    StopReason stop_reason;
    PayloadRef raw;
    std::vector<std::string> notes;
    std::string model;
};

struct StopReason {
    struct EndTurn {};
    struct MaxTokens {};
    struct ToolUse {};
    struct PauseTurn {};
    struct Refusal { std::optional<std::string> category; std::optional<std::string> explanation; };
    struct Other { std::string reason; };

    std::variant<EndTurn, MaxTokens, ToolUse, PauseTurn, Refusal, Other> value;
};

struct PayloadError { std::string message; };

class PayloadSink {
public:
    virtual ~PayloadSink() = default;
    virtual std::expected<PayloadRef, PayloadError> put(std::span<const std::byte> bytes) = 0;
};

struct DriverCtx {
    std::string run_id;
    asio::cancellation_signal& cancel;
    PayloadSink& payload_sink;
};

struct DriverError {
    struct NoCredentials { std::vector<std::string> looked_at; };
    struct Http { std::uint16_t status; std::string body; std::uint8_t retried; };
    struct Transport { std::string message; };
    struct Protocol { std::string message; };
    struct Unsupported { std::string_view capability; };
    struct MockUnmatched { std::string prompt_excerpt; };
    struct Cancelled {};
    struct Timeout {};

    std::variant<NoCredentials, Http, Transport, Protocol, Unsupported, MockUnmatched,
                 Cancelled, Timeout> value;
};

enum class Effort { low, medium, high, xhigh, max };

class MockDriver final : public Driver {
public:
    static std::expected<MockDriver, DriverError> from_script(const std::filesystem::path& path);

    std::string id() const override;   // "mock/<model>/<config hash over the script bytes>/<version>"
    DriverCaps caps() const override;

    asio::awaitable<std::expected<AskResponse, DriverError>>
    ask(const AskRequest& req, DriverCtx& ctx) override;

    asio::awaitable<std::expected<RunResponse, DriverError>>
    run(const RunRequest& req, DriverCtx& ctx, EventSink& sink) override;   // Unsupported
};
```
`PayloadRef` is `leash_spec`'s; `PayloadSink::put` returns `std::expected`
because a full store must not be swallowed, and the runtime adapter maps
`PayloadError` to a `Failure`. `DriverError` names its alternatives
(`DriverError::Http`, `DriverError::MockUnmatched`, …) so other briefs keep the
spelling they already use.

```cpp
// src/drivers/include/leash/drivers/anthropic/spike.hpp
struct SpikeConfig {
    std::string base_url = "https://api.anthropic.com";
    std::string api_key;                        // supplied by the caller; no environment lookup here
    std::chrono::steady_clock::duration timeout = std::chrono::minutes(10);
};

// Stream is asio::ssl::stream<tcp::socket> in production and a plain
// tcp::socket against the testkit stub server in tests.
template <class Stream>
class SseClient {
public:
    SseClient(Stream& stream, SseDecoder& decoder);

    asio::awaitable<std::expected<AskResponse, DriverError>>
    send(const SpikeConfig& config, const AskRequest& req);
};
```

The id string's config-hash field covers every config field that changes
responses (model, effort, max_tokens, thinking, base_url), so the runtime's
`input_hash` (WP-04) can include it.

## Deliverables
- `src/drivers` with `driver.hpp`, `mock.cpp` and `anthropic/`
  (`wire.cpp`, `sse.cpp`, `usage.cpp`, `spike.hpp`, `consts.hpp`).
- `tests/fixtures/anthropic/*.sse` recorded streams: `ask_text.sse`,
  `ask_json.sse`, `refusal.sse`, `rate_limited_then_ok.sse` (two recorded
  bodies), `error_event.sse`; `tests/fixtures/mock/*.json`.
- Unit tests: SSE decoder over every fixture, request-body goldens
  (`expect_golden`), mock matching, and one transport test against
  `StubHttpServer` from `leash_testkit`.
- One live smoke test, CTest label `network`, gated on `LEASH_TEST_NETWORK=1`
  and `ANTHROPIC_API_KEY`.

## Acceptance criteria
- `ctest --preset dev -L drivers` passes: request-body golden for an `ask` with
  a schema shows `thinking.adaptive`, `output_config.effort`,
  `output_config.format.json_schema`, `stream: true` and a single
  `cache_control` on the last system block; SSE fixtures yield the expected
  `text`/`json`/`usage`/`stop_reason`; `refusal.sse` yields
  `StopReason::Refusal` with `category == "cyber"`; `error_event.sse`
  yields `DriverError::Protocol` carrying the event message.
- `rate_limited_then_ok.sse` holds two recorded bodies queued in the stub
  server: the first call returns `DriverError::Http { status = 429 }`, the
  second parses cleanly. Retry policy is WP-13's, so this proves the client is
  re-drivable, not that it retries.
- Mock driver tests: `seq`, `prompt_contains`, `prompt_hash`, `any` matching;
  `fail.times` exhaustion; `latency_ms` respects cancellation
  (`ctx.cancel.emit(asio::cancellation_type::all)` yields
  `DriverError::Cancelled` within 100 ms).
- With WP-04 merged: `build/dev/src/cli/leash run examples/01_hello.leash --input '{"topic":"harness"}' --driver anthropic=mock:tests/fixtures/mock/hello.json`
  completes with a typed list and journals the mock's usage (M1 exit criterion;
  document the run id in the PR).
- `LEASH_TEST_NETWORK=1 ANTHROPIC_API_KEY=… ctest --preset dev -L network -R anthropic_sse`
  passes: asks `claude-opus-5` for `{"answer": Int}` with a schema, receives
  `json` matching it, and reports non-zero input and output tokens. Skipped
  without credentials.

## Notes and pitfalls
- Never send `temperature`, `top_p`, `budget_tokens` or assistant prefill; the
  current models reject them (docs/09 §2 and the API reference). Record
  requested-but-unsupported options in `notes`.
- `max_tokens` for `ask` defaults to 16000; always stream (SSE) so long
  answers do not hit HTTP timeouts.
- Use `http::response_parser<http::buffer_body>` and `http::read_some` in a
  loop. `http::read` buffers the whole body, which defeats streaming and can
  exhaust memory on a long answer.
- TLS: set SNI with `SSL_set_tlsext_host_name` and keep certificate
  verification on. A stub server with a self-signed certificate needs an
  explicit test-only verify callback; never relax verification in production
  code paths.
- `asio::ssl::stream::async_shutdown` waits for the peer's `close_notify` and
  can hang after a truncated stream. On cancellation, close the socket instead.
- SSE framing: an event ends at a blank line and a chunk may split one
  anywhere. Buffer until the full event is present, join multiple `data:`
  lines with `\n`, and ignore `:` comment lines.
- Usage: `message_start.usage` carries the input and cache token counts;
  `message_delta.usage.output_tokens` is cumulative, so take the last value and
  never sum the deltas.
- Keep every API constant (URL, version header, beta header names, field
  names) in `anthropic/consts.hpp` with a comment giving the date verified.
- Do not log prompts or responses through spdlog at info level; debug only,
  truncated.
- The registry holds `std::shared_ptr<Driver>`, so one driver object serves all
  branches of a run: keep per-call state in the coroutine frame, not in the
  driver.
- Cancellation closes the socket and completes the awaitable with
  `DriverError::Cancelled`; Beast does not take a cancellation slot directly,
  so bind the signal to the socket close.

## Dependencies
Hard: WP-02 (Usage, prices). Soft: WP-04 (integration criterion).

## Estimated size
M (3–4 days).

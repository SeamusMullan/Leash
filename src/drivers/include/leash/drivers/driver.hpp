// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "leash/spec/payload.hpp"
#include "leash/spec/usage.hpp"

namespace leash::drivers {

using spec::PayloadRef;
using spec::Usage;

/// How much reasoning effort the caller asks for. Drivers that do not support
/// the option record a note instead of failing.
enum class Effort : std::uint8_t { low, medium, high, xhigh, max };

[[nodiscard]] constexpr std::string_view to_string(Effort effort) noexcept {
    switch (effort) {
    case Effort::low:
        return "low";
    case Effort::medium:
        return "medium";
    case Effort::high:
        return "high";
    case Effort::xhigh:
        return "xhigh";
    case Effort::max:
        return "max";
    }
    return "medium";
}

/// A system-prompt block. The anthropic wire builder places the single
/// cache_control breakpoint on the last block it receives.
struct PromptBlock {
    std::string text;
};

/// A payload that is either small enough to hold inline or already stored in
/// the content-addressed store.
struct Blob {
    std::variant<std::string, PayloadRef> value;

    Blob() = default;
    explicit Blob(std::string text) : value(std::move(text)) {}
    explicit Blob(PayloadRef ref) : value(std::move(ref)) {}

    [[nodiscard]] bool empty() const noexcept {
        const auto* text = std::get_if<std::string>(&value);
        return text != nullptr && text->empty();
    }

    [[nodiscard]] const std::string* inline_text() const noexcept {
        return std::get_if<std::string>(&value);
    }

    [[nodiscard]] const PayloadRef* ref() const noexcept { return std::get_if<PayloadRef>(&value); }
};

/// The reason a model stopped generating.
struct StopReason {
    struct EndTurn {};
    struct MaxTokens {};
    struct ToolUse {};
    struct PauseTurn {};
    struct Refusal {
        std::optional<std::string> category;
        std::optional<std::string> explanation;
    };
    struct Other {
        std::string reason;
    };

    std::variant<EndTurn, MaxTokens, ToolUse, PauseTurn, Refusal, Other> value;
};

struct DriverCaps {
    bool structured_output = false;
    bool tools = false;
    bool effort = false;
    bool sessions = false;
    bool cost_reporting = false;
    bool cancellation = false;

    friend bool operator==(const DriverCaps&, const DriverCaps&) = default;
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
    Blob raw;
    std::vector<std::string> notes;
    std::string model;
};

/// A tool offered to an agentic driver. `input_schema` is the strict-ready
/// JSON Schema the backend validates arguments against.
struct ToolSpec {
    std::string name;
    std::string description;
    nlohmann::json input_schema = nlohmann::json::object();
};

struct SessionRef {
    std::string id;
};

struct RenderedArtifact {
    std::string name;
    std::string content;
};

/// An agentic run. Capability and budget views are built by the runtime's LLM
/// effect handler and arrive with the `run` implementation (WP-12/WP-13);
/// the mock driver rejects `run` before looking at them.
struct RunRequest {
    std::vector<PromptBlock> system;
    std::string task;
    std::optional<nlohmann::json> schema;
    std::vector<ToolSpec> tools;
    std::filesystem::path workdir;
    std::optional<std::uint32_t> max_turns;
    std::optional<Effort> effort;
    std::optional<std::string> model;
    std::vector<RenderedArtifact> profile_artifacts;
    std::optional<SessionRef> session;
};

struct RunResponse {
    std::string final_text;
    std::optional<nlohmann::json> json;
    Usage usage;
    std::uint32_t turns = 0;
    Blob transcript;
    std::optional<SessionRef> session;
    std::vector<std::string> notes;
};

struct Turn {
    std::uint32_t index = 0;
};

struct ToolCall {
    std::string id;
    std::string name;
    nlohmann::json input;
};

struct ToolResult {
    std::string id;
    nlohmann::json output;
    bool is_error = false;
    std::uint64_t duration_ms = 0;
};

struct Text {
    std::string text;
};

struct Thinking {
    std::string text;
};

struct Error {
    std::string message;
};

using DriverEvent = std::variant<Turn, ToolCall, ToolResult, Text, Thinking, Usage, Error>;

/// No-op visitor that makes every alternative explicit; a concrete consumer
/// derives from it or supplies its own overload set to std::visit.
struct DriverEventVisitor {
    void operator()(const Turn& /*unused*/) const {}
    void operator()(const ToolCall& /*unused*/) const {}
    void operator()(const ToolResult& /*unused*/) const {}
    void operator()(const Text& /*unused*/) const {}
    void operator()(const Thinking& /*unused*/) const {}
    void operator()(const Usage& /*unused*/) const {}
    void operator()(const Error& /*unused*/) const {}
};

class EventSink {
  public:
    EventSink() = default;
    virtual ~EventSink() = default;
    EventSink(const EventSink&) = default;
    EventSink(EventSink&&) = default;
    EventSink& operator=(const EventSink&) = default;
    EventSink& operator=(EventSink&&) = default;

    /// May be called from any io_context strand.
    virtual void emit(const DriverEvent& event) = 0;
};

struct PayloadError {
    std::string message;
};

class PayloadSink {
  public:
    PayloadSink() = default;
    virtual ~PayloadSink() = default;
    PayloadSink(const PayloadSink&) = default;
    PayloadSink(PayloadSink&&) = default;
    PayloadSink& operator=(const PayloadSink&) = default;
    PayloadSink& operator=(PayloadSink&&) = default;

    /// Store a large payload. A full store is an error, not something a driver
    /// may silently drop.
    virtual std::expected<PayloadRef, PayloadError> put(std::span<const std::byte> bytes) = 0;
};

/// Everything a driver may use for one call. Drivers never see the runtime's
/// budget or capability stacks.
struct DriverCtx {
    std::string run_id;
    // The context borrows the run's signal and store; both outlive the call.
    asio::cancellation_signal& cancel; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    PayloadSink& payload_sink;         // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

struct DriverError {
    struct NoCredentials {
        std::vector<std::string> looked_at;
    };
    struct Http {
        std::uint16_t status = 0;
        std::string body;
        std::uint8_t retried = 0;
    };
    struct Transport {
        std::string message;
    };
    struct Protocol {
        std::string message;
    };
    struct Unsupported {
        std::string capability;
    };
    struct MockUnmatched {
        std::string prompt_excerpt;
    };
    struct Cancelled {};
    struct Timeout {};

    std::variant<NoCredentials, Http, Transport, Protocol, Unsupported, MockUnmatched, Cancelled,
                 Timeout>
        value;

    [[nodiscard]] bool is_cancelled() const noexcept {
        return std::holds_alternative<Cancelled>(value);
    }
};

class Driver {
  public:
    Driver() = default;
    virtual ~Driver() = default;
    Driver(const Driver&) = default;
    Driver(Driver&&) = default;
    Driver& operator=(const Driver&) = default;
    Driver& operator=(Driver&&) = default;

    /// "<name>/<model>/<config hash>/<version>"; the config hash covers every
    /// field that can change responses.
    [[nodiscard]] virtual std::string id() const = 0;

    [[nodiscard]] virtual DriverCaps caps() const = 0;

    virtual asio::awaitable<std::expected<AskResponse, DriverError>> ask(const AskRequest& req,
                                                                         DriverCtx& ctx) = 0;

    /// Agentic session. The mock driver returns DriverError::Unsupported.
    virtual asio::awaitable<std::expected<RunResponse, DriverError>>
    run(const RunRequest& req, DriverCtx& ctx, EventSink& sink) = 0;
};

/// The driver name component of a driver id, i.e. everything before the first
/// '/'. Returns the whole string when there is no '/'.
[[nodiscard]] std::string_view driver_name(std::string_view driver_id) noexcept;

/// Process-wide-ish registry of drivers keyed by name. The runtime holds one
/// registry per run; drivers are shared because every branch uses the same
/// instance and keeps its per-call state in the coroutine frame.
class DriverRegistry {
  public:
    DriverRegistry() = default;

    /// Register under the driver's own name (the first id component).
    void add(std::shared_ptr<Driver> driver);

    /// Register under an explicit name, replacing any previous entry.
    void add(std::string name, std::shared_ptr<Driver> driver);

    [[nodiscard]] std::shared_ptr<Driver> find(std::string_view name) const;

    [[nodiscard]] bool contains(std::string_view name) const noexcept;

    [[nodiscard]] std::vector<std::string> names() const;

    [[nodiscard]] std::size_t size() const noexcept;

  private:
    std::map<std::string, std::shared_ptr<Driver>, std::less<>> drivers_;
};

} // namespace leash::drivers

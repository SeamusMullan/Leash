// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/drivers/mock.hpp"

#include <asio/as_tuple.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <blake3.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace leash::drivers {

namespace {

constexpr std::size_t kPromptExcerpt = 80;
constexpr std::string_view kHashPrefix = "b3:";
constexpr std::size_t kHashHexLength = 64;

class ParseError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string blake3_hash(std::string_view bytes) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    const char* data = bytes.empty() ? "" : bytes.data();
    blake3_hasher_update(&hasher, data, bytes.size());

    std::array<std::uint8_t, BLAKE3_OUT_LEN> digest{};
    blake3_hasher_finalize(&hasher, digest.data(), digest.size());

    constexpr std::string_view kHex = "0123456789abcdef";
    std::string out = std::string(kHashPrefix);
    out.reserve(kHashPrefix.size() + (digest.size() * 2));
    for (const std::uint8_t byte : digest) {
        out.push_back(kHex.at(static_cast<std::size_t>(byte >> 4U)));
        out.push_back(kHex.at(static_cast<std::size_t>(byte & 0x0FU)));
    }
    return out;
}

[[nodiscard]] std::string prompt_excerpt(std::string_view prompt) {
    if (prompt.size() <= kPromptExcerpt) {
        return std::string(prompt);
    }
    std::string out(prompt.substr(0, kPromptExcerpt));
    out += "...";
    return out;
}

void require_object(const nlohmann::json& value, std::string_view what) {
    if (!value.is_object()) {
        throw ParseError(std::string(what) + " must be an object");
    }
}

[[nodiscard]] std::string require_string(const nlohmann::json& object, const char* key,
                                         std::string_view what) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        throw ParseError(std::string(what) + "." + key + " must be a string");
    }
    return object.at(key).get<std::string>();
}

[[nodiscard]] std::optional<std::string> optional_string(const nlohmann::json& object,
                                                         const char* key, std::string_view what) {
    if (!object.contains(key)) {
        return std::nullopt;
    }
    if (!object.at(key).is_string()) {
        throw ParseError(std::string(what) + "." + key + " must be a string");
    }
    return object.at(key).get<std::string>();
}

[[nodiscard]] std::uint64_t require_unsigned(const nlohmann::json& object, const char* key,
                                             std::string_view what) {
    if (!object.contains(key) || !object.at(key).is_number_unsigned()) {
        throw ParseError(std::string(what) + "." + key + " must be an unsigned integer");
    }
    return object.at(key).get<std::uint64_t>();
}

[[nodiscard]] std::optional<std::uint64_t>
optional_unsigned(const nlohmann::json& object, const char* key, std::string_view what) {
    if (!object.contains(key)) {
        return std::nullopt;
    }
    if (!object.at(key).is_number_unsigned()) {
        throw ParseError(std::string(what) + "." + key + " must be an unsigned integer");
    }
    return object.at(key).get<std::uint64_t>();
}

[[nodiscard]] bool is_lower_hex(std::string_view text) {
    return std::ranges::all_of(text, [](char character) {
        const bool digit = character >= '0' && character <= '9';
        const bool lower = character >= 'a' && character <= 'f';
        return digit || lower;
    });
}

} // namespace

namespace mock_detail {

struct Script {
    struct Match {
        enum class Kind : std::uint8_t { seq, prompt_contains, prompt_hash, any };

        Kind kind = Kind::any;
        std::uint64_t seq = 0;
        std::string text;
    };

    struct Fail {
        enum class Kind : std::uint8_t { http, timeout, transport, protocol, refusal };

        Kind kind = Kind::protocol;
        std::uint16_t status = 0;
        std::string body;
        std::uint8_t retried = 0;
        std::string message;
        std::optional<std::string> category;
        std::optional<std::string> explanation;
        std::optional<std::uint32_t> times;
    };

    struct Entry {
        Match match;
        std::optional<std::string> text;
        std::optional<nlohmann::json> json;
        Usage usage;
        std::optional<Fail> fail;
        std::chrono::milliseconds latency{0};
    };

    int version = 1;
    std::string model = "mock";
    std::vector<Entry> entries;
};

struct State {
    explicit State(std::size_t entries) : failures(entries) {}

    std::atomic<std::uint64_t> seq{0};
    std::vector<std::atomic<std::uint32_t>> failures;
};

} // namespace mock_detail

namespace {

[[nodiscard]] mock_detail::Script::Match parse_match(const nlohmann::json& value,
                                                     std::size_t index) {
    const std::string what = "entries[" + std::to_string(index) + "].match";
    require_object(value, what);
    const nlohmann::json& object = value;

    mock_detail::Script::Match match;
    std::size_t alternatives = 0;

    if (object.contains("seq")) {
        const std::uint64_t seq = require_unsigned(object, "seq", what);
        match.kind = mock_detail::Script::Match::Kind::seq;
        match.seq = seq;
        ++alternatives;
    }
    if (object.contains("prompt_contains")) {
        match.kind = mock_detail::Script::Match::Kind::prompt_contains;
        match.text = require_string(object, "prompt_contains", what);
        ++alternatives;
    }
    if (object.contains("prompt_hash")) {
        const std::string hash = require_string(object, "prompt_hash", what);
        if (hash.size() != kHashPrefix.size() + kHashHexLength || !hash.starts_with(kHashPrefix) ||
            !is_lower_hex(hash.substr(kHashPrefix.size()))) {
            throw ParseError(what +
                             ".prompt_hash must be \"b3:\" followed by 64 lowercase hex digits");
        }
        match.kind = mock_detail::Script::Match::Kind::prompt_hash;
        match.text = hash;
        ++alternatives;
    }
    if (object.contains("any")) {
        if (!object.at("any").is_boolean() || !object.at("any").get<bool>()) {
            throw ParseError(what + ".any must be true");
        }
        match.kind = mock_detail::Script::Match::Kind::any;
        ++alternatives;
    }

    if (alternatives != 1) {
        throw ParseError(what + " must hold exactly one of seq, prompt_contains, prompt_hash, any");
    }
    return match;
}

[[nodiscard]] mock_detail::Script::Fail parse_fail(const nlohmann::json& value, std::size_t index) {
    const std::string what = "entries[" + std::to_string(index) + "].fail";
    require_object(value, what);
    const nlohmann::json& object = value;

    mock_detail::Script::Fail fail;
    const std::string kind = require_string(object, "kind", what);
    if (kind == "http") {
        fail.kind = mock_detail::Script::Fail::Kind::http;
        const std::uint64_t status = require_unsigned(object, "status", what);
        if (status > 0xFFFFU) {
            throw ParseError(what + ".status must fit in 16 bits");
        }
        fail.status = static_cast<std::uint16_t>(status);
        fail.body = optional_string(object, "body", what).value_or(std::string{});
        if (const auto retried = optional_unsigned(object, "retried", what)) {
            if (*retried > 0xFFU) {
                throw ParseError(what + ".retried must fit in 8 bits");
            }
            fail.retried = static_cast<std::uint8_t>(*retried);
        }
    } else if (kind == "timeout") {
        fail.kind = mock_detail::Script::Fail::Kind::timeout;
        fail.message = optional_string(object, "message", what).value_or("mock timeout");
    } else if (kind == "transport") {
        fail.kind = mock_detail::Script::Fail::Kind::transport;
        fail.message = optional_string(object, "message", what).value_or("mock transport failure");
    } else if (kind == "protocol") {
        fail.kind = mock_detail::Script::Fail::Kind::protocol;
        fail.message = optional_string(object, "message", what).value_or("mock protocol failure");
    } else if (kind == "refusal") {
        fail.kind = mock_detail::Script::Fail::Kind::refusal;
        fail.category = optional_string(object, "category", what);
        fail.explanation = optional_string(object, "explanation", what);
    } else {
        throw ParseError(what + ".kind must be one of http, timeout, transport, protocol, refusal");
    }

    if (const auto times = optional_unsigned(object, "times", what)) {
        if (*times > 0xFFFFFFFFU) {
            throw ParseError(what + ".times must fit in 32 bits");
        }
        fail.times = static_cast<std::uint32_t>(*times);
    }
    return fail;
}

[[nodiscard]] Usage parse_usage(const nlohmann::json& value, std::size_t index) {
    const std::string what = "entries[" + std::to_string(index) + "].usage";
    require_object(value, what);
    const nlohmann::json& object = value;

    Usage usage;
    usage.input_tokens = optional_unsigned(object, "input_tokens", what).value_or(0);
    usage.output_tokens = optional_unsigned(object, "output_tokens", what).value_or(0);
    usage.cache_read_input_tokens =
        optional_unsigned(object, "cache_read_input_tokens", what).value_or(0);
    usage.cache_creation_input_tokens =
        optional_unsigned(object, "cache_creation_input_tokens", what).value_or(0);
    if (object.contains("cost_usd")) {
        if (!object.at("cost_usd").is_number()) {
            throw ParseError(what + ".cost_usd must be a number");
        }
        usage.cost = spec::Money::usd(object.at("cost_usd").get<double>());
    }
    usage.model = optional_string(object, "model", what).value_or(std::string{});
    return usage;
}

[[nodiscard]] mock_detail::Script::Entry parse_entry(const nlohmann::json& value,
                                                     std::size_t index) {
    const std::string what = "entries[" + std::to_string(index) + "]";
    require_object(value, what);
    const nlohmann::json& object = value;

    mock_detail::Script::Entry entry;
    if (!object.contains("match")) {
        throw ParseError(what + ".match is required");
    }
    entry.match = parse_match(object.at("match"), index);

    if (const auto latency = optional_unsigned(object, "latency_ms", what)) {
        if (*latency > static_cast<std::uint64_t>(std::chrono::milliseconds::max().count())) {
            throw ParseError(what + ".latency_ms is too large");
        }
        entry.latency = std::chrono::milliseconds(*latency);
    }

    if (object.contains("response")) {
        const std::string response_what = what + ".response";
        const nlohmann::json& response = object.at("response");
        require_object(response, response_what);
        entry.text = optional_string(response, "text", response_what);
        if (response.contains("json")) {
            entry.json = response.at("json");
        }
        if (entry.text.has_value() == entry.json.has_value()) {
            throw ParseError(response_what + " must hold exactly one of text, json");
        }
    }

    if (object.contains("usage")) {
        entry.usage = parse_usage(object.at("usage"), index);
    }
    if (object.contains("fail")) {
        entry.fail = parse_fail(object.at("fail"), index);
    }

    if (!entry.text && !entry.json && !entry.fail) {
        throw ParseError(what + " needs a response (text or json) or a fail entry");
    }
    return entry;
}

[[nodiscard]] mock_detail::Script parse_script(const nlohmann::json& root) {
    require_object(root, "script");
    const nlohmann::json& object = root;

    const std::uint64_t version = require_unsigned(object, "version", "script");
    if (version != 1) {
        throw ParseError("unsupported mock script version " + std::to_string(version) +
                         " (expected 1)");
    }
    if (!object.contains("entries") || !object.at("entries").is_array()) {
        throw ParseError("script.entries must be an array");
    }

    mock_detail::Script script;
    script.version = 1;
    script.model = optional_string(object, "model", "script").value_or("mock");

    const nlohmann::json& entries = object.at("entries");
    script.entries.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        script.entries.push_back(parse_entry(entries.at(i), i));
    }
    return script;
}

[[nodiscard]] bool matches(const mock_detail::Script::Match& match, std::uint64_t call_index,
                           std::string_view prompt) {
    switch (match.kind) {
    case mock_detail::Script::Match::Kind::seq:
        return match.seq == call_index;
    case mock_detail::Script::Match::Kind::prompt_contains:
        return prompt.contains(match.text);
    case mock_detail::Script::Match::Kind::prompt_hash:
        return prompt_hash(prompt) == match.text;
    case mock_detail::Script::Match::Kind::any:
        return true;
    }
    return false;
}

/// Wait for the scripted latency on the caller's io_context. Returns false
/// when the run's cancellation signal fires first. The slot is taken by value
/// because a coroutine must not hold a reference parameter across suspension.
asio::awaitable<bool> wait_latency(std::chrono::milliseconds delay, asio::cancellation_slot slot) {
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(delay);
    auto [error_code] = co_await timer.async_wait(
        asio::bind_cancellation_slot(slot, asio::as_tuple(asio::use_awaitable)));
    co_return error_code != asio::error::operation_aborted;
}

[[nodiscard]] std::optional<std::size_t> select_entry(const mock_detail::Script& script,
                                                      const mock_detail::State& state,
                                                      std::uint64_t call_index,
                                                      std::string_view prompt) {
    for (std::size_t i = 0; i < script.entries.size(); ++i) {
        const mock_detail::Script::Entry& entry = script.entries.at(i);
        if (!matches(entry.match, call_index, prompt)) {
            continue;
        }
        if (entry.fail && entry.fail->times &&
            state.failures.at(i).load(std::memory_order_relaxed) >= *entry.fail->times) {
            continue;
        }
        return i;
    }
    return std::nullopt;
}

[[nodiscard]] AskResponse make_refusal(const mock_detail::Script::Fail& fail,
                                       const mock_detail::Script::Entry& entry,
                                       const std::string& model, bool effort_requested) {
    AskResponse response;
    response.usage = entry.usage;
    response.usage.model = model;
    response.model = model;
    response.stop_reason.value = StopReason::Refusal{
        .category = fail.category,
        .explanation = fail.explanation,
    };
    response.raw = Blob{nlohmann::json{{"error", "refusal"}}.dump()};
    if (effort_requested) {
        response.notes.emplace_back("mock driver ignores effort");
    }
    return response;
}

[[nodiscard]] std::expected<AskResponse, DriverError>
make_failure(const mock_detail::Script::Fail& fail, const mock_detail::Script::Entry& entry,
             const std::string& model, bool effort_requested) {
    switch (fail.kind) {
    case mock_detail::Script::Fail::Kind::http:
        return std::unexpected(DriverError{
            DriverError::Http{
                .status = fail.status,
                .body = fail.body,
                .retried = fail.retried,
            },
        });
    case mock_detail::Script::Fail::Kind::timeout:
        return std::unexpected(DriverError{DriverError::Timeout{}});
    case mock_detail::Script::Fail::Kind::transport:
        return std::unexpected(DriverError{DriverError::Transport{fail.message}});
    case mock_detail::Script::Fail::Kind::protocol:
        return std::unexpected(DriverError{DriverError::Protocol{fail.message}});
    case mock_detail::Script::Fail::Kind::refusal:
        return make_refusal(fail, entry, model, effort_requested);
    }
    return std::unexpected(
        DriverError{DriverError::Protocol{"mock script has an unhandled failure kind"}});
}

[[nodiscard]] AskResponse make_success(const mock_detail::Script::Entry& entry,
                                       const std::string& model, bool effort_requested,
                                       bool has_attachments) {
    AskResponse response;
    response.usage = entry.usage;
    response.usage.model = model;
    response.model = model;

    if (entry.json) {
        response.json = entry.json;
        response.text = entry.json->dump();
        response.raw = Blob{nlohmann::json{{"json", *entry.json}}.dump()};
    } else {
        response.text = entry.text.value_or(std::string{});
        response.raw = Blob{nlohmann::json{{"text", response.text}}.dump()};
    }

    if (effort_requested) {
        response.notes.emplace_back("mock driver ignores effort");
    }
    if (has_attachments) {
        response.notes.emplace_back("mock driver ignores attachments");
    }
    return response;
}

} // namespace

std::string prompt_hash(std::string_view prompt) { return blake3_hash(prompt); }

MockDriver::MockDriver(std::shared_ptr<const mock_detail::Script> script, std::string config_hash)
    : script_(std::move(script)),
      state_(std::make_shared<mock_detail::State>(script_->entries.size())),
      config_hash_(std::move(config_hash)) {
    id_ = "mock/" + script_->model + "/" + config_hash_ + "/" + std::to_string(script_->version);
}

std::expected<MockDriver, DriverError> MockDriver::from_script(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected(
            DriverError{DriverError::Transport{"cannot open mock script " + path.string()}});
    }

    const std::string bytes((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    if (input.bad()) {
        return std::unexpected(
            DriverError{DriverError::Transport{"cannot read mock script " + path.string()}});
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(bytes);
    } catch (const nlohmann::json::exception& error) {
        return std::unexpected(DriverError{
            DriverError::Protocol{"mock script is not valid JSON: " + std::string(error.what())},
        });
    }

    try {
        mock_detail::Script script = parse_script(root);
        return MockDriver(std::make_shared<const mock_detail::Script>(std::move(script)),
                          blake3_hash(bytes));
    } catch (const std::exception& error) {
        return std::unexpected(DriverError{
            DriverError::Protocol{"invalid mock script: " + std::string(error.what())},
        });
    }
}

std::string MockDriver::id() const { return id_; }

DriverCaps MockDriver::caps() const {
    DriverCaps caps;
    caps.structured_output = true;
    caps.tools = false;
    caps.effort = false;
    caps.sessions = false;
    caps.cost_reporting = false;
    caps.cancellation = true;
    return caps;
}

asio::awaitable<std::expected<AskResponse, DriverError>> MockDriver::ask(
    const AskRequest& req, // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    DriverCtx& ctx) {      // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    // Per-call state lives in this coroutine frame; the driver is shared by
    // every branch of a run.
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    const AskRequest request = req;
    const std::uint64_t call_index = state_->seq.fetch_add(1, std::memory_order_relaxed);

    const std::optional<std::size_t> selected =
        select_entry(*script_, *state_, call_index, request.prompt);
    if (!selected) {
        co_return std::unexpected(
            DriverError{DriverError::MockUnmatched{prompt_excerpt(request.prompt)}});
    }

    const mock_detail::Script::Entry& entry = script_->entries.at(*selected);

    if (entry.latency.count() > 0) {
        const bool completed = co_await wait_latency(entry.latency, ctx.cancel.slot());
        if (!completed) {
            co_return std::unexpected(DriverError{DriverError::Cancelled{}});
        }
    }

    const std::string effective_model =
        request.model.value_or(entry.usage.model.empty() ? script_->model : entry.usage.model);

    if (entry.fail) {
        const mock_detail::Script::Fail& fail = *entry.fail;
        if (fail.times) {
            state_->failures.at(*selected).fetch_add(1, std::memory_order_relaxed);
        }
        co_return make_failure(fail, entry, effective_model, request.effort.has_value());
    }

    co_return make_success(entry, effective_model, request.effort.has_value(),
                           !request.attachments.empty());
}

asio::awaitable<std::expected<RunResponse, DriverError>> MockDriver::run(
    const RunRequest& /*req*/, // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    DriverCtx& /*ctx*/,        // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    EventSink& /*sink*/) {     // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    co_return std::unexpected(DriverError{DriverError::Unsupported{"run"}});
}

} // namespace leash::drivers

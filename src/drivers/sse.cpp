// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/drivers/anthropic/sse.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "leash/drivers/anthropic/consts.hpp"
#include "leash/drivers/anthropic/usage.hpp"

namespace leash::drivers::anthropic {

namespace {

[[nodiscard]] SseEventKind kind_for(std::string_view name) {
    if (name == consts::kEventMessageStart) {
        return SseEventKind::MessageStart;
    }
    if (name == consts::kEventContentBlockStart) {
        return SseEventKind::ContentBlockStart;
    }
    if (name == consts::kEventContentBlockDelta) {
        return SseEventKind::ContentBlockDelta;
    }
    if (name == consts::kEventContentBlockStop) {
        return SseEventKind::ContentBlockStop;
    }
    if (name == consts::kEventMessageDelta) {
        return SseEventKind::MessageDelta;
    }
    if (name == consts::kEventMessageStop) {
        return SseEventKind::MessageStop;
    }
    if (name == consts::kEventPing) {
        return SseEventKind::Ping;
    }
    if (name == consts::kEventError) {
        return SseEventKind::Error;
    }
    return SseEventKind::Unknown;
}

[[nodiscard]] std::optional<std::string> string_field(const nlohmann::json& object,
                                                      std::string_view key) {
    const auto entry = object.find(std::string(key));
    if (entry == object.end() || !entry->is_string()) {
        return std::nullopt;
    }
    return entry->get<std::string>();
}

[[nodiscard]] bool is_known_event(SseEventKind kind) {
    return kind != SseEventKind::Ping && kind != SseEventKind::Unknown;
}

} // namespace

void SseDecoder::reset() {
    buffer_.clear();
    event_.clear();
    data_.clear();
    has_data_ = false;
    overflowed_ = false;
}

std::vector<SseEvent> SseDecoder::feed(std::string_view bytes) {
    std::vector<SseEvent> out;
    if (overflowed_) {
        return out;
    }

    buffer_.append(bytes);

    std::size_t position = 0;
    while (position < buffer_.size()) {
        const std::size_t end = buffer_.find_first_of("\r\n", position);
        if (end == std::string::npos) {
            break;
        }

        std::size_t next = end + 1;
        if (buffer_.at(end) == '\r') {
            if (next == buffer_.size()) {
                // Could be the first half of a CRLF split across chunks.
                break;
            }
            if (buffer_.at(next) == '\n') {
                ++next;
            }
        }

        consume_line(std::string_view(buffer_).substr(position, end - position), out);
        position = next;
    }

    buffer_.erase(0, position);

    if (buffer_.size() > consts::kMaxEventBytes) {
        overflowed_ = true;
        buffer_.clear();
        event_.clear();
        data_.clear();
        has_data_ = false;
        out.clear();
    }
    return out;
}

std::vector<SseEvent> SseDecoder::finish() {
    std::vector<SseEvent> out;
    if (!buffer_.empty()) {
        consume_line(buffer_, out);
        buffer_.clear();
    }
    dispatch(out);
    return out;
}

void SseDecoder::consume_line(std::string_view line, std::vector<SseEvent>& out) {
    if (line.empty()) {
        dispatch(out);
        return;
    }
    if (line.front() == ':') {
        return; // comment / keep-alive
    }

    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        // A field with no value is ignored, per the SSE grammar.
        return;
    }

    const std::string_view field = line.substr(0, colon);
    std::string_view value = line.substr(colon + 1);
    if (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
    }

    if (field == "event") {
        event_.assign(value);
    } else if (field == "data") {
        if (has_data_) {
            data_.push_back('\n');
        }
        data_.append(value);
        has_data_ = true;
    }
    // `id` and `retry` are not used by the Messages API; ignore them.
}

void SseDecoder::dispatch(std::vector<SseEvent>& out) {
    if (!has_data_ && event_.empty()) {
        return;
    }

    SseEvent event;
    event.name = std::move(event_);
    event.data = std::move(data_);
    event.kind = kind_for(event.name);
    event.json = nlohmann::json::parse(event.data, nullptr, false);
    event.json_valid = !event.json.is_discarded();
    if (!event.json_valid) {
        event.json = nlohmann::json::object();
    }
    out.push_back(std::move(event));

    event_.clear();
    data_.clear();
    has_data_ = false;
}

SseAssembler::SseAssembler(bool want_json) : want_json_(want_json) {}

void SseAssembler::reset(bool want_json) {
    want_json_ = want_json;
    saw_stop_ = false;
    saw_stop_reason_ = false;
    state_ = SseResult{};
}

std::expected<void, DriverError> SseAssembler::consume(const SseEvent& event) {
    if (is_known_event(event.kind) && !event.json_valid) {
        return std::unexpected(protocol_error("SSE event '" + event.name + "' is not valid JSON"));
    }

    switch (event.kind) {
    case SseEventKind::MessageStart:
        return on_message_start(event);
    case SseEventKind::ContentBlockStart:
        return on_block_start(event);
    case SseEventKind::ContentBlockDelta:
        return on_block_delta(event);
    case SseEventKind::ContentBlockStop:
        return {};
    case SseEventKind::MessageDelta:
        return on_message_delta(event);
    case SseEventKind::MessageStop:
        saw_stop_ = true;
        return {};
    case SseEventKind::Ping:
        return {};
    case SseEventKind::Error:
        return std::unexpected(error_from(event));
    case SseEventKind::Unknown:
        note_unknown(event.name);
        return {};
    }
    return {};
}

std::expected<void, DriverError> SseAssembler::on_message_start(const SseEvent& event) {
    const auto message = event.json.find(std::string(consts::kFieldMessage));
    if (message == event.json.end() || !message->is_object()) {
        return std::unexpected(protocol_error("message_start has no message object"));
    }
    apply_message_start(state_.usage, *message);

    if (auto message_id_value = string_field(*message, consts::kFieldId);
        message_id_value.has_value()) {
        state_.message_id = std::move(*message_id_value);
    }
    if (auto model = string_field(*message, consts::kFieldModel); model.has_value()) {
        state_.model = std::move(*model);
    }
    return {};
}

std::expected<void, DriverError> SseAssembler::on_block_start(const SseEvent& event) {
    const auto block = event.json.find(std::string(consts::kFieldContentBlock));
    if (block == event.json.end() || !block->is_object()) {
        return {};
    }
    if (string_field(*block, consts::kFieldType) != consts::kTypeText) {
        return {};
    }
    if (auto text = string_field(*block, consts::kFieldText); text.has_value()) {
        state_.text += *text;
    }
    return {};
}

std::expected<void, DriverError> SseAssembler::on_block_delta(const SseEvent& event) {
    const auto delta = event.json.find(std::string(consts::kFieldDelta));
    if (delta == event.json.end() || !delta->is_object()) {
        return {};
    }

    const auto type = string_field(*delta, consts::kFieldType);
    if (type == consts::kDeltaText) {
        if (auto text = string_field(*delta, consts::kFieldText); text.has_value()) {
            state_.text += *text;
        }
        return {};
    }
    if (type == consts::kDeltaThinking) {
        if (const auto thinking = delta->find(std::string(consts::kFieldThinking));
            thinking != delta->end() && thinking->is_string()) {
            state_.thinking += thinking->get<std::string>();
        }
        return {};
    }
    if (type == consts::kDeltaSignature) {
        // Signatures are passed back unchanged on later turns (WP-13).
        return {};
    }
    if (auto text = string_field(*delta, consts::kFieldText); text.has_value()) {
        state_.text += *text;
    }
    return {};
}

std::expected<void, DriverError> SseAssembler::on_message_delta(const SseEvent& event) {
    if (const auto usage = event.json.find(std::string(consts::kFieldUsage));
        usage != event.json.end()) {
        apply_message_delta(state_.usage, *usage);
    }

    const auto delta = event.json.find(std::string(consts::kFieldDelta));
    if (delta == event.json.end() || !delta->is_object()) {
        return {};
    }

    const auto reason = string_field(*delta, consts::kFieldStopReason);
    if (!reason.has_value()) {
        return {};
    }

    nlohmann::json details = nlohmann::json::object();
    if (const auto found = delta->find(std::string(consts::kFieldStopDetails));
        found != delta->end()) {
        details = *found;
    }
    parse_stop_reason(*reason, details);
    saw_stop_reason_ = true;
    return {};
}

DriverError SseAssembler::error_from(const SseEvent& event) {
    std::string message;
    if (const auto error = event.json.find(std::string(consts::kFieldError));
        error != event.json.end() && error->is_object()) {
        if (auto text = string_field(*error, "message"); text.has_value()) {
            message = std::move(*text);
        } else if (auto type = string_field(*error, consts::kFieldType); type.has_value()) {
            message = std::move(*type);
        }
    } else if (auto text = string_field(event.json, "message"); text.has_value()) {
        message = std::move(*text);
    }
    if (message.empty()) {
        message = event.data.empty() ? "anthropic error event" : event.data;
    }
    return protocol_error(std::move(message));
}

std::expected<SseResult, DriverError> SseAssembler::result() const {
    if (!saw_stop_) {
        return std::unexpected(protocol_error("SSE stream ended before message_stop"));
    }
    if (!saw_stop_reason_) {
        return std::unexpected(protocol_error("message_stop arrived without a stop_reason"));
    }

    SseResult out = state_;
    // A refusal's prose is not schema output; the stop reason is the result.
    const bool refused = std::holds_alternative<StopReason::Refusal>(out.stop_reason.value);
    if (want_json_ && !refused && !out.text.empty()) {
        auto parsed = nlohmann::json::parse(out.text, nullptr, false);
        if (parsed.is_discarded()) {
            return std::unexpected(protocol_error("structured output is not valid JSON"));
        }
        out.json = std::move(parsed);
    }
    return out;
}

void SseAssembler::parse_stop_reason(const std::string& reason, const nlohmann::json& details) {
    if (reason == consts::kStopEndTurn) {
        state_.stop_reason.value = StopReason::EndTurn{};
        return;
    }
    if (reason == consts::kStopMaxTokens) {
        state_.stop_reason.value = StopReason::MaxTokens{};
        return;
    }
    if (reason == consts::kStopToolUse) {
        state_.stop_reason.value = StopReason::ToolUse{};
        return;
    }
    if (reason == consts::kStopPauseTurn) {
        state_.stop_reason.value = StopReason::PauseTurn{};
        return;
    }
    if (reason == consts::kStopRefusal) {
        StopReason::Refusal refusal;
        if (details.is_object()) {
            refusal.category = string_field(details, consts::kFieldCategory);
            refusal.explanation = string_field(details, consts::kFieldExplanation);
        }
        state_.stop_reason.value = std::move(refusal);
        return;
    }
    state_.stop_reason.value = StopReason::Other{reason};
}

void SseAssembler::note_unknown(std::string_view name) {
    if (name.empty()) {
        return;
    }
    std::string note = "ignored SSE event: " + std::string(name);
    if (std::ranges::find(state_.notes, note) == state_.notes.end()) {
        state_.notes.push_back(std::move(note));
    }
}

} // namespace leash::drivers::anthropic

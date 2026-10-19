// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

/// Every constant the Anthropic transport depends on. Nothing else in
/// `src/drivers/anthropic` spells an endpoint, header, event or field name
/// inline; tests use the same constants so a rename cannot drift.
///
/// The "verified" dates record when the value was last checked against the
/// official reference. Re-check before changing a model default or a beta.
namespace leash::drivers::anthropic::consts {

// ---------------------------------------------------------------------------
// Endpoint
// ---------------------------------------------------------------------------

/// Verified 2026-10-19 against the Create a Message reference
/// (platform.claude.com/docs/en/api/messages/create), whose curl example posts
/// to this URL with `anthropic-version: 2023-06-01`.
inline constexpr std::string_view kBaseUrl = "https://api.anthropic.com";
inline constexpr std::string_view kMessagesPath = "/v1/messages";

// ---------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------

/// Verified 2026-10-19 against the Create a Message reference.
inline constexpr std::string_view kHeaderApiKey = "x-api-key";
inline constexpr std::string_view kHeaderAnthropicVersion = "anthropic-version";
inline constexpr std::string_view kHeaderBeta = "anthropic-beta";
inline constexpr std::string_view kHeaderContentType = "content-type";
inline constexpr std::string_view kHeaderAccept = "accept";
inline constexpr std::string_view kHeaderHost = "host";
inline constexpr std::string_view kHeaderUserAgent = "user-agent";

/// The current Messages API version string. Verified 2026-10-19; the reference
/// still shows `2023-06-01` in its curl example.
inline constexpr std::string_view kApiVersion = "2023-06-01";

/// Beta header values recorded 2026-10-19 from docs/09 §2 (task budgets,
/// server-side fallbacks, compaction). They are not re-checked against the
/// public beta list because the `ask` spike never sends them; WP-13 must
/// re-check before turning them on.
inline constexpr std::string_view kBetaTaskBudgets = "task-budgets-2026-03-13";
inline constexpr std::string_view kBetaServerSideFallback = "server-side-fallback-2026-07-01";
inline constexpr std::string_view kBetaCompact = "compact-2026-01-12";

/// Product identification; not an API constant, but kept here so the transport
/// has no inline header values.
inline constexpr std::string_view kUserAgent = "leash-drivers/0.1";

// ---------------------------------------------------------------------------
// Defaults (docs/09 §2)
// ---------------------------------------------------------------------------

/// Default model for `ask`. Verified 2026-10-19 against docs/09 §2.
inline constexpr std::string_view kDefaultModel = "claude-opus-5";

/// `max_tokens` for `ask`; `run` turns use 64000. Verified 2026-10-19 against
/// docs/09 §2.
inline constexpr std::uint32_t kAskMaxTokens = 16000;
inline constexpr std::uint32_t kRunMaxTokens = 64000;

/// Default effort for `ask`; `run` uses `xhigh`. Verified 2026-10-19 against
/// docs/09 §2.
inline constexpr std::string_view kAskEffort = "high";
inline constexpr std::string_view kRunEffort = "xhigh";

/// Response bodies above this size go to the run's `PayloadSink` instead of
/// travelling inline in `AskResponse::raw` (WP-05 scope, 8 KiB).
inline constexpr std::size_t kInlinePayloadLimit = std::size_t{8} * 1024U;

// ---------------------------------------------------------------------------
// Request fields
// ---------------------------------------------------------------------------

/// Field names of the Messages API request. Verified 2026-10-19 against the
/// Create a Message reference.
inline constexpr std::string_view kFieldModel = "model";
inline constexpr std::string_view kFieldMaxTokens = "max_tokens";
inline constexpr std::string_view kFieldStream = "stream";
inline constexpr std::string_view kFieldSystem = "system";
inline constexpr std::string_view kFieldMessages = "messages";
inline constexpr std::string_view kFieldThinking = "thinking";
inline constexpr std::string_view kFieldOutputConfig = "output_config";
inline constexpr std::string_view kFieldEffort = "effort";
inline constexpr std::string_view kFieldFormat = "format";
inline constexpr std::string_view kFieldSchema = "schema";
inline constexpr std::string_view kFieldCacheControl = "cache_control";
inline constexpr std::string_view kFieldContent = "content";
inline constexpr std::string_view kFieldRole = "role";
inline constexpr std::string_view kFieldType = "type";
inline constexpr std::string_view kFieldText = "text";

/// Request values. `thinking: {"type": "adaptive"}` and the `output_config`
/// effort levels were verified 2026-10-19 against the Create a Message
/// reference; the `output_config.format` shape follows docs/09 §2.
inline constexpr std::string_view kTypeText = "text";
inline constexpr std::string_view kTypeUser = "user";
inline constexpr std::string_view kTypeAdaptive = "adaptive";
inline constexpr std::string_view kTypeJsonSchema = "json_schema";
inline constexpr std::string_view kTypeEphemeral = "ephemeral";

// ---------------------------------------------------------------------------
// Response and streaming fields
// ---------------------------------------------------------------------------

/// Verified 2026-10-19 against the Messages streaming reference: event names
/// `message_start`, `content_block_start`, `ping`, `content_block_delta`,
/// `content_block_stop`, `message_delta`, `message_stop`, `error`.
inline constexpr std::string_view kEventMessageStart = "message_start";
inline constexpr std::string_view kEventContentBlockStart = "content_block_start";
inline constexpr std::string_view kEventContentBlockDelta = "content_block_delta";
inline constexpr std::string_view kEventContentBlockStop = "content_block_stop";
inline constexpr std::string_view kEventMessageDelta = "message_delta";
inline constexpr std::string_view kEventMessageStop = "message_stop";
inline constexpr std::string_view kEventPing = "ping";
inline constexpr std::string_view kEventError = "error";

/// `content_block_delta.delta.type` values. Verified 2026-10-19.
inline constexpr std::string_view kDeltaText = "text_delta";
inline constexpr std::string_view kDeltaThinking = "thinking_delta";
inline constexpr std::string_view kDeltaSignature = "signature_delta";

/// Response fields. Verified 2026-10-19.
inline constexpr std::string_view kFieldId = "id";
inline constexpr std::string_view kFieldMessage = "message";
inline constexpr std::string_view kFieldDelta = "delta";
inline constexpr std::string_view kFieldIndex = "index";
inline constexpr std::string_view kFieldContentBlock = "content_block";
inline constexpr std::string_view kFieldStopReason = "stop_reason";
inline constexpr std::string_view kFieldStopDetails = "stop_details";
inline constexpr std::string_view kFieldUsage = "usage";
inline constexpr std::string_view kFieldInputTokens = "input_tokens";
inline constexpr std::string_view kFieldOutputTokens = "output_tokens";
inline constexpr std::string_view kFieldCacheReadInputTokens = "cache_read_input_tokens";
inline constexpr std::string_view kFieldCacheCreationInputTokens = "cache_creation_input_tokens";
inline constexpr std::string_view kFieldError = "error";

/// `stop_reason` values. Verified 2026-10-19.
inline constexpr std::string_view kStopEndTurn = "end_turn";
inline constexpr std::string_view kStopMaxTokens = "max_tokens";
inline constexpr std::string_view kStopToolUse = "tool_use";
inline constexpr std::string_view kStopPauseTurn = "pause_turn";
inline constexpr std::string_view kStopRefusal = "refusal";
inline constexpr std::string_view kStopSequence = "stop_sequence";

/// `stop_details` fields. Verified 2026-10-19 against the refusal stop reason
/// documentation.
inline constexpr std::string_view kFieldCategory = "category";
inline constexpr std::string_view kFieldExplanation = "explanation";

/// Content type the streaming endpoint returns. Verified 2026-10-19.
inline constexpr std::string_view kContentTypeSse = "text/event-stream";

/// Error bodies are diagnostics, not content; cap how much is kept.
inline constexpr std::size_t kErrorBodyLimit = std::size_t{64} * 1024U;

/// A single SSE event larger than this is treated as a protocol error rather
/// than growing the frame without bound.
inline constexpr std::size_t kMaxEventBytes = std::size_t{4} * 1024U * 1024U;

} // namespace leash::drivers::anthropic::consts

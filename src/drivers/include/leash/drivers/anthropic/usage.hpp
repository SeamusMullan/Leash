// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <nlohmann/json.hpp>

#include "leash/drivers/driver.hpp"

namespace leash::drivers::anthropic {

/// Fill `usage` from `message_start.message` (model, input tokens and the two
/// cache counters). Counters the message does not carry are left untouched, so
/// a missing field never resets an earlier value.
void apply_message_start(Usage& usage, const nlohmann::json& message);

/// Fill `usage` from a `message_delta` `usage` object.
///
/// `output_tokens` is cumulative across the message, so the last reported value
/// wins. Values are assigned, never added: summing the deltas double-counts.
void apply_message_delta(Usage& target, const nlohmann::json& usage);

} // namespace leash::drivers::anthropic

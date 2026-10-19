// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "leash/drivers/driver.hpp"

namespace leash::drivers {

namespace mock_detail {
struct Script;
struct State;
} // namespace mock_detail

/// BLAKE3 over the exact prompt bytes, rendered as "b3:<lowercase hex>".
/// Scripts use the same spelling in `match.prompt_hash`.
[[nodiscard]] std::string prompt_hash(std::string_view prompt);

/// In-process driver that replays a JSON script. Script version 1:
///
/// {
///   "version": 1,
///   "model": "mock-large",
///   "entries": [
///     {"match": {"seq": 0},
///      "response": {"json": [{"name": "Leash"}]},
///      "usage": {"input_tokens": 120, "output_tokens": 40}},
///     {"match": {"prompt_contains": "Suggest three"},
///      "response": {"text": "not json"}},
///     {"match": {"prompt_hash": "b3:..."},
///      "fail": {"kind": "http", "status": 429, "times": 1},
///      "latency_ms": 50},
///     {"match": {"any": true},
///      "fail": {"kind": "refusal", "category": "cyber"}}
///   ]
/// }
///
/// The first entry whose match holds wins. `seq` counts ask calls on this
/// driver instance from zero. `fail.times` limits how often an entry fails
/// before later entries are considered. `latency_ms` waits on an
/// asio::steady_timer on the caller's io_context so cancellation is testable.
class MockDriver final : public Driver {
  public:
    static std::expected<MockDriver, DriverError> from_script(const std::filesystem::path& path);

    [[nodiscard]] std::string id() const override;
    [[nodiscard]] DriverCaps caps() const override;

    asio::awaitable<std::expected<AskResponse, DriverError>> ask(const AskRequest& req,
                                                                 DriverCtx& ctx) override;

    /// Always DriverError::Unsupported{"run"}; tools arrive with WP-13.
    asio::awaitable<std::expected<RunResponse, DriverError>>
    run(const RunRequest& req, DriverCtx& ctx, EventSink& sink) override;

  private:
    MockDriver(std::shared_ptr<const mock_detail::Script> script, std::string config_hash);

    std::shared_ptr<const mock_detail::Script> script_;
    std::shared_ptr<mock_detail::State> state_;
    std::string config_hash_;
    std::string id_;
};

} // namespace leash::drivers

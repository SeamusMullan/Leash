// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/spec/usage.hpp"

namespace leash::spec {

std::uint64_t Usage::total_tokens() const noexcept {
    return input_tokens + output_tokens + cache_read_input_tokens + cache_creation_input_tokens;
}

} // namespace leash::spec

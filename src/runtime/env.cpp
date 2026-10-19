// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/runtime/env.hpp"

#include <utility>

namespace leash::runtime {

Env Env::child() const {
    auto frame = std::make_shared<Frame>();
    frame->parent = frame_;
    return Env{std::move(frame)};
}

Env Env::define(std::string name, Value value) const {
    auto frame = std::make_shared<Frame>();
    if (frame_) {
        frame->bindings = frame_->bindings;
        frame->parent = frame_->parent;
    }
    frame->bindings.insert_or_assign(std::move(name), std::move(value));
    return Env{std::move(frame)};
}

std::optional<Value> Env::lookup(std::string_view name) const {
    for (const Frame* frame = frame_.get(); frame != nullptr; frame = frame->parent.get()) {
        const auto found = frame->bindings.find(name);
        if (found != frame->bindings.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

bool Env::contains(std::string_view name) const { return lookup(name).has_value(); }

} // namespace leash::runtime

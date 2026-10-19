// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/runtime/failure.hpp"

#include <string>

namespace leash::runtime {

std::string Site::to_string() const {
    std::string out;
    if (!module.empty()) {
        out += module + "::";
    }
    if (!item.empty()) {
        out += item;
    }
    if (!node_kind.empty()) {
        out += "#" + node_kind;
    }
    if (line != 0) {
        out += "@L" + std::to_string(line);
    }
    return out;
}

} // namespace leash::runtime

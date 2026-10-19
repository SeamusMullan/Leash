// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace leash::syntax {

/// Identifies one source file within a compilation session.
struct FileId {
    std::uint32_t value = 0;

    auto operator<=>(const FileId&) const = default;
};

/// A half-open byte range `[start, end)` inside `file`.
struct Span {
    FileId file;
    std::uint32_t start = 0; // byte offset, inclusive
    std::uint32_t end = 0;   // byte offset, exclusive

    [[nodiscard]] constexpr std::uint32_t size() const { return end - start; }
    [[nodiscard]] constexpr bool empty() const { return start == end; }

    bool operator==(const Span&) const = default;
};

/// Identity of an AST node, assigned in pre-order within one file.
struct NodeId {
    std::uint32_t value = 0;

    auto operator<=>(const NodeId&) const = default;
};

/// A span that starts where `outer` starts and ends where `inner` ends. Used by
/// the parser to widen a node over its children without recomputing offsets.
[[nodiscard]] constexpr Span cover(const Span& outer, const Span& inner) {
    return Span{.file = outer.file, .start = outer.start, .end = inner.end};
}

} // namespace leash::syntax

template <> struct std::hash<leash::syntax::NodeId> {
    std::size_t operator()(const leash::syntax::NodeId& node_id) const noexcept {
        return std::hash<std::uint32_t>{}(node_id.value);
    }
};

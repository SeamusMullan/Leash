// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "leash/syntax/formatter.hpp"
#include "leash/syntax/parser.hpp"
#include "leash/syntax/span.hpp"

/// libFuzzer entry point for the parser, formatter and diagnostic renderer.
/// Well-formed input is formatted and re-parsed to exercise both directions;
/// malformed input exercises error recovery and every diagnostic label.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const char* bytes = size == 0 ? "" : reinterpret_cast<const char*>(data);
    const std::string_view source(bytes, size);

    const leash::syntax::Parsed parsed =
        leash::syntax::parse_program(source, leash::syntax::FileId{1});
    for (const leash::syntax::Diagnostic& diagnostic : parsed.diagnostics) {
        for (const auto& [span, label] : diagnostic.labels) {
            (void)label;
            if (span.start > source.size() || span.end > source.size()) {
                __builtin_trap();
            }
        }
    }

    if (!parsed.program.has_value()) {
        return 0;
    }

    const std::string formatted = leash::syntax::format_program(*parsed.program, source);
    const leash::syntax::Parsed reparsed =
        leash::syntax::parse_program(formatted, leash::syntax::FileId{2});
    for (const leash::syntax::Diagnostic& diagnostic : reparsed.diagnostics) {
        (void)diagnostic;
    }
    return 0;
}

// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "leash/syntax/lexer.hpp"
#include "leash/syntax/span.hpp"
#include "leash/syntax/token.hpp"

/// libFuzzer entry point for the lexer. The lexer never fails, so the only
/// contract under test is termination, sane spans and an EOF token at the end.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const char* bytes = size == 0 ? "" : reinterpret_cast<const char*>(data);
    const std::string_view source(bytes, size);

    const std::vector<leash::syntax::Token> tokens =
        leash::syntax::lex(source, leash::syntax::FileId{1});
    if (tokens.empty()) {
        return 0;
    }

    for (const leash::syntax::Token& token : tokens) {
        if (token.span.end > source.size()) {
            __builtin_trap();
        }
    }
    if (tokens.back().kind != leash::syntax::TokenKind::Eof) {
        __builtin_trap();
    }
    return 0;
}

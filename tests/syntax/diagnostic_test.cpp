// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "leash/syntax/diagnostic.hpp"
#include "leash/syntax/parser.hpp"

namespace {

using leash::syntax::Diagnostic;
using leash::syntax::FileId;
using leash::syntax::Severity;
using leash::syntax::Span;

} // namespace

TEST_CASE("renderer prints a snippet, underline and help", "[syntax][diagnostic]") {
    const std::string source = "let x = }\n";
    Diagnostic diagnostic;
    diagnostic.code = "E0102";
    diagnostic.severity = Severity::Error;
    diagnostic.message = "unexpected token";
    diagnostic.labels.emplace_back(Span{.file = FileId{1}, .start = 8, .end = 9}, "here");
    diagnostic.help = "remove this token";

    const std::string rendered = leash::syntax::render_to_string(diagnostic, source);
    CHECK(rendered == "error[E0102]: unexpected token\n"
                      "  --> <input>:1:9\n"
                      "  |\n"
                      "1 | let x = }\n"
                      "  |         ^ here\n"
                      "  = help: remove this token\n");
}

TEST_CASE("renderer reports line and column across lines", "[syntax][diagnostic]") {
    const std::string source = "module m\n\nlet x =\n";
    Diagnostic diagnostic;
    diagnostic.code = "E0105";
    diagnostic.severity = Severity::Error;
    diagnostic.message = "expected an expression";
    diagnostic.labels.emplace_back(Span{.file = FileId{1}, .start = 17, .end = 17},
                                   "expected here");

    const std::string rendered = leash::syntax::render_to_string(diagnostic, source);
    CHECK(rendered.find("--> <input>:3:8") != std::string::npos);
    CHECK(rendered.find("3 | let x =") != std::string::npos);
    CHECK(rendered.find("^ expected here") != std::string::npos);
}

TEST_CASE("warnings use the severity name", "[syntax][diagnostic]") {
    Diagnostic diagnostic;
    diagnostic.code = "W0101";
    diagnostic.severity = Severity::Warning;
    diagnostic.message = "something to look at";
    const std::string rendered = leash::syntax::render_to_string(diagnostic, "let x = 1\n");
    CHECK(rendered.find("warning[W0101]") == 0);
}

TEST_CASE("parser diagnostics can be rendered", "[syntax][diagnostic]") {
    const std::string source = "flow f(input) {\n  let x = 1 +\n}\n";
    const auto parsed = leash::syntax::parse_program(source, FileId{1});
    REQUIRE_FALSE(parsed.diagnostics.empty());
    const std::string rendered =
        leash::syntax::render_to_string(parsed.diagnostics.front(), source);
    CHECK(rendered.find("error[E0105]") != std::string::npos);
    CHECK(rendered.find("expected an expression") != std::string::npos);
}

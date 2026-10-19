// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include "leash/runtime/eval.hpp"
#include "leash/runtime/program.hpp"
#include "leash/runtime/value.hpp"
#include "leash/syntax/parser.hpp"

namespace {

using namespace leash::runtime;
namespace spec = leash::spec;

[[nodiscard]] Program compile(std::string_view source) {
    const leash::syntax::Parsed parsed =
        leash::syntax::parse_program(source, leash::syntax::FileId{.value = 1});
    for (const leash::syntax::Diagnostic& diagnostic : parsed.diagnostics) {
        INFO("parser diagnostic: " << diagnostic.message);
    }
    REQUIRE(parsed.program.has_value());
    auto lowered = from_ast(*parsed.program); // NOLINT(bugprone-unchecked-optional-access)
    REQUIRE(lowered.has_value());
    return std::move(*lowered);
}

[[nodiscard]] std::expected<Value, Failure> run(Program& program, Value input = unit()) {
    Interpreter interpreter(program);
    boost::asio::io_context context;
    auto future = boost::asio::co_spawn(context, interpreter.run_flow("main", std::move(input)),
                                        boost::asio::use_future);
    context.run();
    return future.get();
}

[[nodiscard]] std::int64_t as_int(const std::expected<Value, Failure>& result) {
    if (!result) {
        INFO("runtime failure: " << result.error().message);
    }
    REQUIRE(result.has_value());
    const auto* value = std::get_if<Int>(&*result);
    REQUIRE(value != nullptr);
    return value->v;
}

[[nodiscard]] std::string as_string(const std::expected<Value, Failure>& result) {
    if (!result) {
        INFO("runtime failure: " << result.error().message);
    }
    REQUIRE(result.has_value());
    const auto* value = std::get_if<Str>(&*result);
    REQUIRE(value != nullptr);
    REQUIRE(value->v != nullptr);
    return *value->v;
}

[[nodiscard]] Value point_input() { return record("", {{"name", string("ada")}}); }

} // namespace

TEST_CASE("evaluator applies operator precedence and if expressions", "[runtime][eval]") {
    Program program = compile(R"(flow main(input) -> Int {
  let value = 1 + 2 * 3
  if value == 7 { 10 } else { 20 }
}
)");
    CHECK(as_int(run(program)) == 10);
}

TEST_CASE("string interpolation reads fields from the input record", "[runtime][eval]") {
    Program program = compile(R"(flow main(input) -> String {
  "hello {input.name}\n"
}
)");
    CHECK(as_string(run(program, point_input())) == "hello ada\n");
}

TEST_CASE("calls bind named arguments and fill defaults", "[runtime][eval]") {
    Program program = compile(R"(fn greet(name: String, punctuation: String = "!") -> String {
  "hi {name}{punctuation}"
}

flow main(input) -> String {
  greet(name: "ada")
}
)");
    CHECK(as_string(run(program)) == "hi ada!");
}

TEST_CASE("closures power filter and map", "[runtime][eval]") {
    Program program = compile(R"(flow main(input) -> [Int] {
  let items = [1, 2, 3, 4]
  let evens = items.filter(x => x % 2 == 0)
  evens.map(x => x * 10)
}
)");
    const auto result = run(program);
    REQUIRE(result.has_value());
    CHECK(*result == list({integer(20), integer(40)}));
}

TEST_CASE("any, all and first short-circuit over lists", "[runtime][eval]") {
    Program program = compile(R"(flow main(input) -> [Bool] {
  let items = [1, 2, 3]
  [items.any(x => x > 2), items.all(x => x > 0), first(items) == 1]
}
)");
    const auto result = run(program);
    REQUIRE(result.has_value());
    CHECK(*result == list({boolean(true), boolean(true), boolean(true)}));
}

TEST_CASE("string methods contain, split lines and trim", "[runtime][eval]") {
    Program program = compile(R"(flow main(input) -> [Bool] {
  let text = "  a\nb  "
  let cleaned = text.trim()
  [cleaned.contains("a"), len(cleaned.lines()) == 2, to_string(3) == "3"]
}
)");
    const auto result = run(program);
    REQUIRE(result.has_value());
    CHECK(*result == list({boolean(true), boolean(true), boolean(true)}));
}

TEST_CASE("usd builds exact Money and to_string uses four decimals", "[runtime][eval]") {
    Program program = compile(R"(flow main(input) -> String {
  let total = usd(2.0) - usd(0.5)
  to_string(total)
}
)");
    CHECK(as_string(run(program)) == "$1.5000");
}

TEST_CASE("UFCS dispatches a method call to a user function", "[runtime][eval]") {
    Program program = compile(R"(fn twice(n: Int) -> Int {
  n * 2
}

flow main(input) -> Int {
  let n = 3
  n.twice()
}
)");
    CHECK(as_int(run(program)) == 6);
}

TEST_CASE("fail becomes a Failure with its kind and message", "[runtime][eval]") {
    Program program = compile(R"(flow main(input) -> Int {
  fail Model("boom")
}
)");
    const auto result = run(program);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == spec::FailureKind::Model);
    CHECK(result.error().message == "boom");
}

TEST_CASE("complete exits the flow with a value", "[runtime][eval]") {
    Program program = compile(R"(flow main(input) -> Int {
  complete 7
}
)");
    CHECK(as_int(run(program)) == 7);
}

TEST_CASE("recursive functions resolve through the program", "[runtime][eval]") {
    Program program = compile(R"(fn fact(n: Int) -> Int {
  if n <= 1 {
    1
  } else {
    n * fact(n - 1)
  }
}

flow main(input) -> Int {
  fact(5)
}
)");
    CHECK(as_int(run(program)) == 120);
}

TEST_CASE("record fields and enum literals evaluate structurally", "[runtime][eval]") {
    Program program = compile(R"(flow main(input) -> [Bool] {
  let point = { x: 1, y: 2 }
  let effort = .High
  [point.x == 1, effort == .High]
}
)");
    const auto result = run(program);
    REQUIRE(result.has_value());
    CHECK(*result == list({boolean(true), boolean(true)}));
}

TEST_CASE("division by zero is a runtime failure", "[runtime][eval]") {
    Program program = compile(R"(flow main(input) -> Int {
  1 / 0
}
)");
    const auto result = run(program);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == "division by zero");
}

TEST_CASE("a missing required argument names the parameter", "[runtime][eval]") {
    Program program = compile(R"(fn one(a: Int) -> Int {
  a
}

flow main(input) -> Int {
  one()
}
)");
    const auto result = run(program);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("missing argument 'a'") != std::string::npos);
}

TEST_CASE("unknown names fail with the name in the message", "[runtime][eval]") {
    Program program = compile(R"(flow main(input) -> Int {
  missing_name
}
)");
    const auto result = run(program);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("undefined name 'missing_name'") != std::string::npos);
}

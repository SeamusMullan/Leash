// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>

#include "leash/runtime/program.hpp"
#include "leash/syntax/parser.hpp"

namespace {

using namespace leash::runtime;
namespace spec = leash::spec;

[[nodiscard]] std::expected<Program, Failure> lower(std::string_view source) {
    const leash::syntax::Parsed parsed = leash::syntax::parse_program(source, {.value = 1});
    if (!parsed.program) {
        Failure failure;
        failure.kind = spec::FailureKind::Unknown;
        failure.message = "parser produced no program";
        return std::unexpected(std::move(failure));
    }
    return from_ast(parsed.program.value());
}

} // namespace

TEST_CASE("from_ast lowers the hello example", "[runtime][program]") {
    std::ifstream stream(std::filesystem::path{LEASH_SOURCE_DIR} / "examples" / "01_hello.leash");
    REQUIRE(stream.good());
    const std::string source{std::istreambuf_iterator<char>{stream},
                             std::istreambuf_iterator<char>{}};
    const auto program = lower(source);
    REQUIRE(program.has_value());
    CHECK(program->module == "examples.hello");
    CHECK(program->types.contains("Suggestion"));
    CHECK(program->agents.size() == 1);
    CHECK(program->tasks.size() == 1);
    REQUIRE(program->flows.size() == 1);
    CHECK(program->flows.front().name == "main");
}

TEST_CASE("from_ast lowers module, types, functions, agents, tasks and flows",
          "[runtime][program]") {
    const std::string source = R"(module test.prog

type Point = { x: Int, y: Int }

let answer: Int = 42

fn add(a: Int, b: Int = 2) -> Int { a + b }

agent helper = Agent { system: "hi" }

task main {
  input { topic: String }
  output [Point]
  budget { cost: usd(1.0) }
}

flow main(input) -> Int {
  let p = Point { x: 1, y: 2 }
  add(p.x, b: 3)
}
)";
    const auto program = lower(source);
    REQUIRE(program.has_value());
    CHECK(program->module == "test.prog");

    REQUIRE(program->types.contains("Point"));
    const TypeRef& point = *program->types.at("Point");
    CHECK(point.kind == TypeRef::Kind::Record);
    CHECK(point.name == "Point");
    REQUIRE(point.fields.size() == 2);
    CHECK(point.fields.at(0).first == "x");
    CHECK(point.fields.at(1).second->kind == TypeRef::Kind::Int);

    REQUIRE(program->consts.size() == 1);
    CHECK(program->consts.front().name == "answer");
    REQUIRE(program->consts.front().value);
    CHECK(std::get<IntLit>(program->consts.front().value->node).value == 42);

    REQUIRE(program->fns.size() == 1);
    const FnDecl& add = program->fns.front();
    REQUIRE(add.params.size() == 2);
    CHECK(add.params.at(0).name == "a");
    REQUIRE(add.params.at(1).default_value);
    CHECK(std::get<IntLit>(add.params.at(1).default_value->node).value == 2);
    CHECK(add.result->kind == TypeRef::Kind::Int);

    REQUIRE(program->agents.size() == 1);
    CHECK(program->agents.front().name == "helper");

    REQUIRE(program->tasks.size() == 1);
    const Task& task = program->tasks.front();
    REQUIRE(task.input.has_value());
    CHECK((*task.input)->fields.size() == 1); // NOLINT(bugprone-unchecked-optional-access)
    REQUIRE(task.output);
    CHECK(task.output->kind == TypeRef::Kind::List);
    CHECK(task.output->element->kind == TypeRef::Kind::Named);
    CHECK(task.budget != nullptr);

    REQUIRE(program->flows.size() == 1);
    const Flow& flow = program->flows.front();
    CHECK(flow.name == "main");
    CHECK(flow.param.name == "input");
    REQUIRE(flow.result);
    CHECK(flow.result->kind == TypeRef::Kind::Int);
    CHECK(flow.body.statements.size() == 1);
}

TEST_CASE("from_ast keeps durations and interpolation structure", "[runtime][program]") {
    const std::string source = R"(flow main(input) -> Duration {
  let greeting = "hi {input.name}"
  let wait = 250ms
  wait
}
)";
    const auto program = lower(source);
    REQUIRE(program.has_value());
    REQUIRE(program->flows.size() == 1);
    const Block& body = program->flows.front().body;
    REQUIRE(body.statements.size() == 2);
    const auto& greeting = std::get<LetStmt>(body.statements.at(0).node);
    const auto& literal = std::get<StringLit>(greeting.value->node);
    REQUIRE(literal.parts.size() == 2);
    CHECK(std::get<std::string>(literal.parts.at(0)) == "hi ");
    CHECK(std::holds_alternative<ExprPtr>(literal.parts.at(1)));
    const auto& wait = std::get<LetStmt>(body.statements.at(1).node);
    CHECK(std::get<DurationLit>(wait.value->node).value == std::chrono::milliseconds{250});
}

TEST_CASE("from_ast reports an overflowing integer literal", "[runtime][program]") {
    const auto program = lower("flow main(input) -> Int { 9223372036854775808 }");
    REQUIRE_FALSE(program.has_value());
    CHECK(program.error().kind == spec::FailureKind::Unknown);
    CHECK(program.error().message.find("integer") != std::string::npos);
}

// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "leash/runtime/env.hpp"
#include "leash/runtime/value.hpp"

namespace {

using namespace leash::runtime;
namespace spec = leash::spec;

// NOLINTBEGIN(readability-named-parameter)
class CountingVisitor final : public ValueVisitor {
  public:
    void visit(const Unit&) override { seen.emplace_back("Unit"); }
    void visit(const Null&) override { seen.emplace_back("Null"); }
    void visit(const Bool&) override { seen.emplace_back("Bool"); }
    void visit(const Int&) override { seen.emplace_back("Int"); }
    void visit(const Float&) override { seen.emplace_back("Float"); }
    void visit(const Str&) override { seen.emplace_back("String"); }
    void visit(const std::chrono::nanoseconds&) override { seen.emplace_back("Duration"); }
    void visit(const spec::Money&) override { seen.emplace_back("Money"); }
    void visit(const List&) override { seen.emplace_back("List"); }
    void visit(const Record&) override { seen.emplace_back("Record"); }
    void visit(const Enum&) override { seen.emplace_back("Enum"); }
    void visit(const Fn&) override { seen.emplace_back("Fn"); }
    void visit(const Opaque&) override { seen.emplace_back("Opaque"); }

    std::vector<std::string> seen;
};
// NOLINTEND(readability-named-parameter)

class DummyOpaque final : public OpaqueValue {
  public:
    [[nodiscard]] std::string_view type_name() const noexcept override { return "Dummy"; }
};

} // namespace

TEST_CASE("canonical JSON sorts keys and preserves scalar shapes", "[runtime][value]") {
    const Value value =
        record("Point", {{"y", integer(2)}, {"x", integer(1)}, {"label", string("a")}});
    CHECK(to_canonical_json(value) == R"({"label":"a","x":1,"y":2})");
    CHECK(to_json(value).dump() == R"({"label":"a","x":1,"y":2})");
}

TEST_CASE("canonical JSON formats floats with the shortest round-trip form", "[runtime][value]") {
    CHECK(to_canonical_json(number(0.1)) == "0.1");
    CHECK(to_canonical_json(number(1.0)) == "1");
    CHECK(to_canonical_json(number(-2.5)) == "-2.5");
    const std::string third = to_canonical_json(number(1.0 / 3.0));
    CHECK(std::stod(third) == 1.0 / 3.0);
}

TEST_CASE("canonical JSON renders Money as a decimal string and duration as nanoseconds",
          "[runtime][value]") {
    CHECK(to_canonical_json(money(spec::Money::usd(3.12))) == R"("$3.1200")");
    CHECK(to_canonical_json(duration(std::chrono::milliseconds{500})) == "500000000");
}

TEST_CASE("canonical JSON emits enum variants and lists", "[runtime][value]") {
    CHECK(to_canonical_json(enum_value("", "High")) == R"({"payload":[],"variant":"High"})");
    CHECK(to_canonical_json(enum_value("Effort", "Low")) ==
          R"({"payload":[],"type":"Effort","variant":"Low"})");
    const Value values = list({integer(1), string("two"), null()});
    CHECK(to_canonical_json(values) == R"([1,"two",null])");
}

TEST_CASE("non-finite floats are rejected before they reach a value", "[runtime][value]") {
    CHECK_THROWS_AS(number(std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    CHECK_THROWS_AS(number(std::numeric_limits<double>::infinity()), std::invalid_argument);
}

TEST_CASE("functions and opaque values refuse serialisation", "[runtime][value]") {
    const Value function_value = function(std::make_shared<const Closure>());
    CHECK_THROWS_AS(to_canonical_json(function_value), std::invalid_argument);
    const Value opaque_value = opaque(std::make_shared<const DummyOpaque>());
    CHECK_THROWS_AS(to_canonical_json(opaque_value), std::invalid_argument);
}

TEST_CASE("structural equality compares payloads and identity compares handles",
          "[runtime][value]") {
    CHECK(record("A", {{"x", integer(1)}}) == record("B", {{"x", integer(1)}}));
    CHECK(list({integer(1), string("a")}) == list({integer(1), string("a")}));
    CHECK(enum_value("E", "A", {integer(1)}) == enum_value("F", "A", {integer(1)}));
    CHECK(list({integer(1)}) != list({integer(2)}));

    const auto first = std::make_shared<Closure>();
    const auto second = std::make_shared<Closure>();
    CHECK(function(first) != function(second));
    CHECK(function(first) == function(first));

    const auto handle = std::make_shared<const DummyOpaque>();
    const auto other_handle = std::make_shared<const DummyOpaque>();
    CHECK(opaque(handle) == opaque(handle));
    CHECK(opaque(handle) != opaque(other_handle));
}

TEST_CASE("ValueVisitor visits every alternative", "[runtime][value]") {
    CountingVisitor visitor;
    const std::vector<Value> values = {
        unit(),
        null(),
        boolean(true),
        integer(1),
        number(1.5),
        string("x"),
        duration(std::chrono::milliseconds{1}),
        money(spec::Money::usd(1.0)),
        list({}),
        record("", {}),
        enum_value("", "V"),
        function(std::make_shared<const Closure>()),
        opaque(std::make_shared<const DummyOpaque>()),
    };
    for (const Value& value : values) {
        accept(value, visitor);
    }
    REQUIRE(visitor.seen.size() == values.size());
    CHECK(visitor.seen == std::vector<std::string>{"Unit", "Null", "Bool", "Int", "Float", "String",
                                                   "Duration", "Money", "List", "Record", "Enum",
                                                   "Fn", "Opaque"});
}

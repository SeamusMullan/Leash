// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "leash/runtime/env.hpp"

using namespace leash::runtime;

TEST_CASE("environments resolve through lexical parents", "[runtime][env]") {
    const Env root = Env{}.define("x", integer(1));
    const Env child = root.child().define("y", integer(2));
    REQUIRE(child.lookup("x").has_value());
    REQUIRE(child.lookup("y").has_value());
    CHECK(child.lookup("x") == integer(1));
    CHECK(child.lookup("y") == integer(2));
    CHECK_FALSE(root.lookup("y").has_value());
}

TEST_CASE("inner bindings shadow outer ones without mutating them", "[runtime][env]") {
    const Env outer = Env{}.define("x", integer(1));
    const Env inner = outer.define("x", integer(9));
    CHECK(outer.lookup("x") == integer(1));
    CHECK(inner.lookup("x") == integer(9));
}

TEST_CASE("define returns a new environment so captured values do not change", "[runtime][env]") {
    const Env captured = Env{}.define("x", integer(1));
    const Env extended = captured.define("x", integer(2));
    CHECK(captured.lookup("x") == integer(1));
    CHECK(extended.lookup("x") == integer(2));

    Closure closure;
    closure.env = captured;
    CHECK(closure.env.lookup("x") == integer(1));
}

TEST_CASE("a child scope does not leak into its parent", "[runtime][env]") {
    const Env parent = Env{}.define("a", string("a"));
    {
        const Env child = parent.child().define("b", string("b"));
        CHECK(child.lookup("b").has_value());
    }
    CHECK_FALSE(parent.lookup("b").has_value());
    CHECK(parent.lookup("a").has_value());
}

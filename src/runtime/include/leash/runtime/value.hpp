// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "leash/spec/money.hpp"

namespace leash::runtime {

struct RecordVal;
struct ListVal;
struct EnumVal;
struct Closure;

/// The alternatives are deliberately small value types. Compound payloads are
/// behind `std::shared_ptr<const T>` so a copy is a reference-count bump, never
/// a deep copy (docs/03-runtime-architecture.md §6.1). Scalars stay inline;
/// their payload is the number itself.
struct Unit {};
struct Null {};
struct Bool {
    bool v = false;
};
struct Int {
    std::int64_t v = 0;
};
struct Float {
    double v = 0.0;
};
struct Str {
    std::shared_ptr<const std::string> v;
};
struct List {
    std::shared_ptr<const ListVal> v;
};
struct Record {
    std::shared_ptr<const RecordVal> v;
};
struct Enum {
    std::shared_ptr<const EnumVal> v;
};
struct Fn {
    std::shared_ptr<const Closure> v;
};

/// Base class for runtime-owned values (`Agent`, `Repo`, `Handle`, ...). They
/// are never journaled and compare by identity.
class OpaqueValue {
  public:
    OpaqueValue() = default;
    OpaqueValue(const OpaqueValue&) = delete;
    OpaqueValue& operator=(const OpaqueValue&) = delete;
    OpaqueValue(OpaqueValue&&) = delete;
    OpaqueValue& operator=(OpaqueValue&&) = delete;
    virtual ~OpaqueValue() = default;

    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
};

struct Opaque {
    std::shared_ptr<const OpaqueValue> v;
};

using Value = std::variant<Unit, Null, Bool, Int, Float, Str, std::chrono::nanoseconds, spec::Money,
                           List, Record, Enum, Fn, Opaque>;

/// The payload of a list. Defined after `Value` so the recursive type is
/// complete before `std::vector<Value>` is instantiated.
struct ListVal {
    std::vector<Value> items;
};

/// A record value. `type` is the nominal type name for `Name { ... }` and
/// empty for a structural literal. Fields are sorted so serialisation and
/// equality never depend on construction order.
struct RecordVal {
    std::string type;
    std::map<std::string, Value, std::less<>> fields;
};

/// An enum variant, with an optional payload.
struct EnumVal {
    std::string type;
    std::string variant;
    std::vector<Value> payload;
};

/// Abstract visitor declared beside the variant (ADR-0011). Adding an
/// alternative to `Value` breaks every subclass, which is the point: the unit
/// test visits all thirteen alternatives.
class ValueVisitor {
  public:
    ValueVisitor() = default;
    ValueVisitor(const ValueVisitor&) = default;
    ValueVisitor& operator=(const ValueVisitor&) = default;
    ValueVisitor(ValueVisitor&&) = default;
    ValueVisitor& operator=(ValueVisitor&&) = default;
    virtual ~ValueVisitor() = default;

    virtual void visit(const Unit& value) = 0;
    virtual void visit(const Null& value) = 0;
    virtual void visit(const Bool& value) = 0;
    virtual void visit(const Int& value) = 0;
    virtual void visit(const Float& value) = 0;
    virtual void visit(const Str& value) = 0;
    virtual void visit(const std::chrono::nanoseconds& value) = 0;
    virtual void visit(const spec::Money& value) = 0;
    virtual void visit(const List& value) = 0;
    virtual void visit(const Record& value) = 0;
    virtual void visit(const Enum& value) = 0;
    virtual void visit(const Fn& value) = 0;
    virtual void visit(const Opaque& value) = 0;
};

void accept(const Value& value, ValueVisitor& visitor);

// ------------------------------------------------------------------ factories

[[nodiscard]] Value unit();
[[nodiscard]] Value null();
[[nodiscard]] Value boolean(bool value);
[[nodiscard]] Value integer(std::int64_t value);
/// Throws `std::invalid_argument` for NaN and infinities: they have no
/// canonical JSON representation and must never enter the interpreter.
[[nodiscard]] Value number(double value);
[[nodiscard]] Value string(std::string value);
[[nodiscard]] Value duration(std::chrono::nanoseconds value);
[[nodiscard]] Value money(spec::Money value);
[[nodiscard]] Value list(std::vector<Value> value);
[[nodiscard]] Value record(std::string type, std::map<std::string, Value, std::less<>> fields);
[[nodiscard]] Value enum_value(std::string type, std::string variant,
                               std::vector<Value> payload = {});
[[nodiscard]] Value function(std::shared_ptr<const Closure> closure);
[[nodiscard]] Value opaque(std::shared_ptr<const OpaqueValue> value);

// -------------------------------------------------------------------- queries

[[nodiscard]] std::string_view kind_name(const Value& value);
[[nodiscard]] bool structurally_equal(const Value& lhs, const Value& rhs);
[[nodiscard]] bool operator==(const Value& lhs, const Value& rhs);
[[nodiscard]] bool operator!=(const Value& lhs, const Value& rhs);

/// Human-readable rendering used by `to_string`, interpolation and messages.
/// Strings render as their contents (no quotes).
[[nodiscard]] std::string display(const Value& value);

/// Shortest round-trip decimal rendering of a finite double.
[[nodiscard]] std::string format_float(double value);

/// Canonical JSON: object keys sorted, floats shortest round-trip, Money as a
/// decimal string, durations as integer nanoseconds. Throws
/// `std::invalid_argument` for functions and opaque values, which are never
/// journaled.
[[nodiscard]] std::string to_canonical_json(const Value& value);

/// Structural JSON form for journal payloads. Throws for functions and opaque
/// values.
[[nodiscard]] nlohmann::json to_json(const Value& value);

} // namespace leash::runtime

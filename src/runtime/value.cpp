// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/runtime/value.hpp"

#include "leash/runtime/env.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace leash::runtime {
namespace {

[[nodiscard]] std::string escape_json_string(std::string_view text) {
    return nlohmann::json(std::string{text}).dump();
}

[[nodiscard]] std::string format_duration(std::chrono::nanoseconds value) {
    const std::int64_t count = value.count();
    constexpr std::array<std::pair<std::int64_t, std::string_view>, 5> units = {
        std::pair{3'600'000'000'000LL, "h"},
        std::pair{60'000'000'000LL, "m"},
        std::pair{1'000'000'000LL, "s"},
        std::pair{1'000'000LL, "ms"},
        std::pair{1LL, "ns"},
    };
    for (const auto& [scale, suffix] : units) {
        if (count != 0 && count % scale == 0) {
            return std::to_string(count / scale) + std::string{suffix};
        }
    }
    return "0ms";
}

void append_canonical(const Value& value, std::string& out);

void append_canonical_object(const std::map<std::string, Value, std::less<>>& fields,
                             std::string& out) {
    out.push_back('{');
    bool first = true;
    for (const auto& [key, field] : fields) {
        if (!first) {
            out.push_back(',');
        }
        first = false;
        out += escape_json_string(key);
        out.push_back(':');
        append_canonical(field, out);
    }
    out.push_back('}');
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void append_canonical(const Value& value, std::string& out) {
    std::visit(
        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        [&](const auto& alternative) {
            using Alt = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<Alt, Unit> || std::is_same_v<Alt, Null>) {
                out += "null";
            } else if constexpr (std::is_same_v<Alt, Bool>) {
                out += alternative.v ? "true" : "false";
            } else if constexpr (std::is_same_v<Alt, Int>) {
                out += std::to_string(alternative.v);
            } else if constexpr (std::is_same_v<Alt, Float>) {
                out += format_float(alternative.v);
            } else if constexpr (std::is_same_v<Alt, Str>) {
                out += escape_json_string(alternative.v ? *alternative.v : std::string{});
            } else if constexpr (std::is_same_v<Alt, std::chrono::nanoseconds>) {
                out += std::to_string(alternative.count());
            } else if constexpr (std::is_same_v<Alt, spec::Money>) {
                out += escape_json_string(alternative.display());
            } else if constexpr (std::is_same_v<Alt, List>) {
                out.push_back('[');
                if (alternative.v) {
                    for (std::size_t index = 0; index < alternative.v->items.size(); ++index) {
                        if (index != 0) {
                            out.push_back(',');
                        }
                        append_canonical(alternative.v->items.at(index), out);
                    }
                }
                out.push_back(']');
            } else if constexpr (std::is_same_v<Alt, Record>) {
                if (!alternative.v) {
                    out += "{}";
                } else {
                    append_canonical_object(alternative.v->fields, out);
                }
            } else if constexpr (std::is_same_v<Alt, Enum>) {
                const EnumVal empty;
                const EnumVal& enum_value = alternative.v ? *alternative.v : empty;
                out.push_back('{');
                bool first = true;
                const auto append_field = [&](std::string_view key, const auto& append) {
                    if (!first) {
                        out.push_back(',');
                    }
                    first = false;
                    out += escape_json_string(key);
                    out.push_back(':');
                    append();
                };
                append_field("payload", [&] {
                    out.push_back('[');
                    for (std::size_t index = 0; index < enum_value.payload.size(); ++index) {
                        if (index != 0) {
                            out.push_back(',');
                        }
                        append_canonical(enum_value.payload.at(index), out);
                    }
                    out.push_back(']');
                });
                if (!enum_value.type.empty()) {
                    append_field("type", [&] { out += escape_json_string(enum_value.type); });
                }
                append_field("variant", [&] { out += escape_json_string(enum_value.variant); });
                out.push_back('}');
            } else {
                throw std::invalid_argument("functions and opaque values are not serialisable");
            }
        },
        value);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] nlohmann::json to_json_impl(const Value& value) {
    return std::visit(
        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        [&](const auto& alternative) -> nlohmann::json {
            using Alt = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<Alt, Unit> || std::is_same_v<Alt, Null>) {
                return nullptr;
            } else if constexpr (std::is_same_v<Alt, Bool>) { // NOLINT(bugprone-branch-clone)
                return alternative.v;
            } else if constexpr (std::is_same_v<Alt, Int>) {
                return alternative.v;
            } else if constexpr (std::is_same_v<Alt, Float>) {
                return alternative.v;
            } else if constexpr (std::is_same_v<Alt, Str>) {
                return alternative.v ? *alternative.v : std::string{};
            } else if constexpr (std::is_same_v<Alt, std::chrono::nanoseconds>) {
                return alternative.count();
            } else if constexpr (std::is_same_v<Alt, spec::Money>) {
                return alternative.display();
            } else if constexpr (std::is_same_v<Alt, List>) {
                nlohmann::json array = nlohmann::json::array();
                if (alternative.v) {
                    for (const Value& element : alternative.v->items) {
                        array.push_back(to_json_impl(element));
                    }
                }
                return array;
            } else if constexpr (std::is_same_v<Alt, Record>) {
                nlohmann::json object = nlohmann::json::object();
                if (alternative.v) {
                    for (const auto& [key, field] : alternative.v->fields) {
                        object.emplace(key, to_json_impl(field));
                    }
                }
                return object;
            } else if constexpr (std::is_same_v<Alt, Enum>) {
                const EnumVal empty;
                const EnumVal& enum_value = alternative.v ? *alternative.v : empty;
                nlohmann::json array = nlohmann::json::array();
                for (const Value& element : enum_value.payload) {
                    array.push_back(to_json_impl(element));
                }
                nlohmann::json object = nlohmann::json::object();
                object.emplace("variant", enum_value.variant);
                object.emplace("payload", std::move(array));
                if (!enum_value.type.empty()) {
                    object.emplace("type", enum_value.type);
                }
                return object;
            } else {
                throw std::invalid_argument("functions and opaque values are not serialisable");
            }
        },
        value);
}

} // namespace

void accept(const Value& value, ValueVisitor& visitor) {
    std::visit([&visitor](const auto& alternative) { visitor.visit(alternative); }, value);
}

Value unit() { return Unit{}; }
Value null() { return Null{}; }
Value boolean(bool value) { return Bool{.v = value}; }
Value integer(std::int64_t value) { return Int{.v = value}; }

Value number(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("NaN and infinity are not valid Leash floats");
    }
    return Float{.v = value};
}

Value string(std::string value) {
    return Str{.v = std::make_shared<const std::string>(std::move(value))};
}

Value duration(std::chrono::nanoseconds value) { return value; }

Value money(spec::Money value) { return value; }

Value list(std::vector<Value> value) {
    return List{.v = std::make_shared<const ListVal>(ListVal{.items = std::move(value)})};
}

Value record(std::string type, std::map<std::string, Value, std::less<>> fields) {
    return Record{
        .v = std::make_shared<const RecordVal>(
            RecordVal{.type = std::move(type), .fields = std::move(fields)}),
    };
}

Value enum_value(std::string type, std::string variant, std::vector<Value> payload) {
    return Enum{
        .v = std::make_shared<const EnumVal>(EnumVal{
            .type = std::move(type),
            .variant = std::move(variant),
            .payload = std::move(payload),
        }),
    };
}

Value function(std::shared_ptr<const Closure> closure) { return Fn{.v = std::move(closure)}; }

Value opaque(std::shared_ptr<const OpaqueValue> value) { return Opaque{.v = std::move(value)}; }

std::string_view kind_name(const Value& value) {
    return std::visit(
        [](const auto& alternative) -> std::string_view {
            using Alt = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<Alt, Unit>) {
                return "Unit";
            } else if constexpr (std::is_same_v<Alt, Null>) {
                return "Null";
            } else if constexpr (std::is_same_v<Alt, Bool>) {
                return "Bool";
            } else if constexpr (std::is_same_v<Alt, Int>) {
                return "Int";
            } else if constexpr (std::is_same_v<Alt, Float>) {
                return "Float";
            } else if constexpr (std::is_same_v<Alt, Str>) {
                return "String";
            } else if constexpr (std::is_same_v<Alt, std::chrono::nanoseconds>) {
                return "Duration";
            } else if constexpr (std::is_same_v<Alt, spec::Money>) {
                return "Money";
            } else if constexpr (std::is_same_v<Alt, List>) {
                return "List";
            } else if constexpr (std::is_same_v<Alt, Record>) {
                return "Record";
            } else if constexpr (std::is_same_v<Alt, Enum>) {
                return "Enum";
            } else if constexpr (std::is_same_v<Alt, Fn>) {
                return "Fn";
            } else {
                return "Opaque";
            }
        },
        value);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity, bugprone-branch-clone)
bool structurally_equal(const Value& lhs, const Value& rhs) {
    return std::visit(
        // NOLINTNEXTLINE(readability-function-cognitive-complexity, bugprone-branch-clone)
        [&](const auto& left, const auto& right) -> bool {
            using Left = std::decay_t<decltype(left)>;
            using Right = std::decay_t<decltype(right)>;
            if constexpr (!std::is_same_v<Left, Right>) {
                return false;
            } else if constexpr (std::is_same_v<Left, Unit> || std::is_same_v<Left, Null>) {
                return true;
            } else if constexpr (std::is_same_v<Left, Bool>) { // NOLINT(bugprone-branch-clone)
                return left.v == right.v;
            } else if constexpr (std::is_same_v<Left, Int>) {
                return left.v == right.v;
            } else if constexpr (std::is_same_v<Left, Float>) {
                return left.v == right.v;
            } else if constexpr (std::is_same_v<Left, Str>) {
                const std::string empty;
                return (left.v ? *left.v : empty) == (right.v ? *right.v : empty);
            } else if constexpr (std::is_same_v<
                                     Left,
                                     std::chrono::nanoseconds>) { // NOLINT(bugprone-branch-clone)
                return left == right;
            } else if constexpr (std::is_same_v<Left, spec::Money>) {
                return left == right;
            } else if constexpr (std::is_same_v<Left, List>) {
                if (static_cast<bool>(left.v) != static_cast<bool>(right.v)) {
                    return false;
                }
                if (!left.v) {
                    return true;
                }
                if (left.v->items.size() != right.v->items.size()) {
                    return false;
                }
                for (std::size_t index = 0; index < left.v->items.size(); ++index) {
                    if (!structurally_equal(left.v->items.at(index), right.v->items.at(index))) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<Left, Record>) {
                if (static_cast<bool>(left.v) != static_cast<bool>(right.v)) {
                    return false;
                }
                if (!left.v) {
                    return true;
                }
                if (left.v->fields.size() != right.v->fields.size()) {
                    return false;
                }
                auto left_it = left.v->fields.begin();
                auto right_it = right.v->fields.begin();
                for (; left_it != left.v->fields.end(); ++left_it, ++right_it) {
                    if (left_it->first != right_it->first ||
                        !structurally_equal(left_it->second, right_it->second)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<Left, Enum>) {
                if (static_cast<bool>(left.v) != static_cast<bool>(right.v)) {
                    return false;
                }
                if (!left.v) {
                    return true;
                }
                if (left.v->variant != right.v->variant ||
                    left.v->payload.size() != right.v->payload.size()) {
                    return false;
                }
                for (std::size_t index = 0; index < left.v->payload.size(); ++index) {
                    if (!structurally_equal(left.v->payload.at(index),
                                            right.v->payload.at(index))) {
                        return false;
                    }
                }
                return true;
            } else {
                // Functions and opaque handles compare by identity.
                return left.v == right.v;
            }
        },
        lhs, rhs);
}

bool operator==(const Value& lhs, const Value& rhs) { return structurally_equal(lhs, rhs); }
bool operator!=(const Value& lhs, const Value& rhs) { return !structurally_equal(lhs, rhs); }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string display(const Value& value) {
    return std::visit(
        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        [](const auto& alternative) -> std::string {
            using Alt = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<Alt, Unit>) {
                return "()";
            } else if constexpr (std::is_same_v<Alt, Null>) {
                return "null";
            } else if constexpr (std::is_same_v<Alt, Bool>) {
                return alternative.v ? "true" : "false";
            } else if constexpr (std::is_same_v<Alt, Int>) {
                return std::to_string(alternative.v);
            } else if constexpr (std::is_same_v<Alt, Float>) {
                return format_float(alternative.v);
            } else if constexpr (std::is_same_v<Alt, Str>) {
                return alternative.v ? *alternative.v : std::string{};
            } else if constexpr (std::is_same_v<Alt, std::chrono::nanoseconds>) {
                return format_duration(alternative);
            } else if constexpr (std::is_same_v<Alt, spec::Money>) {
                return alternative.display();
            } else if constexpr (std::is_same_v<Alt, List>) {
                std::string out = "[";
                if (alternative.v) {
                    for (std::size_t index = 0; index < alternative.v->items.size(); ++index) {
                        if (index != 0) {
                            out += ", ";
                        }
                        out += display(alternative.v->items.at(index));
                    }
                }
                return out + "]";
            } else if constexpr (std::is_same_v<Alt, Record>) {
                if (!alternative.v) {
                    return "{}";
                }
                std::string out = "{";
                bool first = true;
                for (const auto& [key, field] : alternative.v->fields) {
                    if (!first) {
                        out += ", ";
                    }
                    first = false;
                    out += key + ": " + display(field);
                }
                return out + "}";
            } else if constexpr (std::is_same_v<Alt, Enum>) {
                if (!alternative.v) {
                    return ".<enum>";
                }
                std::string out = "." + alternative.v->variant;
                if (!alternative.v->payload.empty()) {
                    out += '(';
                    for (std::size_t index = 0; index < alternative.v->payload.size(); ++index) {
                        if (index != 0) {
                            out += ", ";
                        }
                        out += display(alternative.v->payload.at(index));
                    }
                    out += ')';
                }
                return out;
            } else if constexpr (std::is_same_v<Alt, Fn>) {
                if (alternative.v && !alternative.v->name.empty()) {
                    return "<fn " + alternative.v->name + ">";
                }
                return "<fn>";
            } else {
                return alternative.v ? "<opaque " + std::string{alternative.v->type_name()} + ">"
                                     : "<opaque>";
            }
        },
        value);
}

std::string format_float(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("NaN and infinity have no canonical JSON form");
    }
    if (value == 0.0) {
        return "0";
    }
    std::array<char, 64> buffer{};
    const auto [ptr, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                            std::chars_format::general);
    if (error != std::errc{}) {
        throw std::invalid_argument("float could not be formatted");
    }
    return std::string{buffer.data(), ptr};
}

std::string to_canonical_json(const Value& value) {
    std::string out;
    append_canonical(value, out);
    return out;
}

nlohmann::json to_json(const Value& value) { return to_json_impl(value); }

} // namespace leash::runtime

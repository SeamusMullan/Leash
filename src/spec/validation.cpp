// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "validation.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <valijson/adapters/nlohmann_json_adapter.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validation_results.hpp>
#include <valijson/validator.hpp>

#include "embedded.hpp"

namespace leash::spec::detail {
namespace {

std::string escape_token(std::string_view token) {
    std::string escaped;
    escaped.reserve(token.size());
    for (const char character : token) {
        if (character == '~') {
            escaped += "~0";
        } else if (character == '/') {
            escaped += "~1";
        } else {
            escaped += character;
        }
    }
    return escaped;
}

constexpr std::string_view kDefinitionsPrefix = "#/definitions/";

/// Records the first failing RFC 6901 pointer by walking the schema and
/// re-validating one subschema at a time. Valijson reports a message but the
/// pointer is derived here, in schema order, so the result is stable across
/// valijson versions.
class Locator {
  public:
    explicit Locator(const nlohmann::json& root) : root_(&root) {}

    [[nodiscard]] std::string locate(const nlohmann::json& schema,
                                     const nlohmann::json& instance) const {
        return locate(schema, instance, std::string{});
    }

  private:
    /// Follow local #/definitions/<name> references. Unknown references are
    /// left in place.
    [[nodiscard]] const nlohmann::json& resolve(const nlohmann::json& schema) const {
        const nlohmann::json* current = &schema;
        std::string previous;
        for (int depth = 0; depth < 32; ++depth) {
            if (!current->is_object()) {
                break;
            }
            const auto ref = current->find("$ref");
            if (ref == current->end() || !ref->is_string()) {
                break;
            }
            const std::string target = ref->get<std::string>();
            if (target == previous || !target.starts_with(kDefinitionsPrefix)) {
                break;
            }
            previous = target;
            const std::string name = target.substr(kDefinitionsPrefix.size());
            const auto definitions = root_->find("definitions");
            if (definitions == root_->end() || !definitions->is_object() ||
                !definitions->contains(name)) {
                break;
            }
            current = &definitions->at(name);
        }
        return *current;
    }

    /// Whether an instance satisfies one subschema on its own. The subschema is
    /// wrapped with the root definitions so local references keep resolving.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] bool valid(const nlohmann::json& schema, const nlohmann::json& instance) const {
        if (schema.is_boolean()) {
            return schema.get<bool>();
        }

        nlohmann::json wrapper = schema;
        const auto definitions = root_->find("definitions");
        if (definitions != root_->end()) {
            wrapper.emplace("definitions", *definitions);
        }

        try {
            valijson::Schema compiled;
            valijson::SchemaParser parser;
            valijson::adapters::NlohmannJsonAdapter schema_adapter(wrapper);
            parser.populateSchema(schema_adapter, compiled);

            valijson::Validator validator;
            valijson::adapters::NlohmannJsonAdapter instance_adapter(instance);
            return validator.validate(compiled, instance_adapter, nullptr);
        } catch (const std::exception&) {
            // A locator must never turn a real schema failure into a crash or a
            // different diagnosis; leave the pointer at the current level.
            return true;
        }
    }

    [[nodiscard]] std::string locate_properties(const nlohmann::json& schema,
                                                const nlohmann::json& instance,
                                                const std::string& pointer) const {
        const auto properties = schema.find("properties");
        if (properties == schema.end() || !properties->is_object()) {
            return {};
        }

        for (const auto& [key, subschema] : properties->items()) {
            const auto member = instance.find(key);
            if (member == instance.end()) {
                continue;
            }
            if (!valid(subschema, *member)) {
                return locate(subschema, *member, pointer + "/" + escape_token(key));
            }
        }
        return {};
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] static std::string locate_required(const nlohmann::json& schema,
                                                     const nlohmann::json& instance,
                                                     const std::string& pointer) {
        const auto required = schema.find("required");
        if (required == schema.end() || !required->is_array()) {
            return {};
        }

        for (const auto& name : *required) {
            if (!name.is_string()) {
                continue;
            }
            const std::string key = name.get<std::string>();
            if (!instance.contains(key)) {
                return pointer + "/" + escape_token(key);
            }
        }
        return {};
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] static std::string locate_extra(const nlohmann::json& schema,
                                                  const nlohmann::json& instance,
                                                  const std::string& pointer) {
        const auto additional = schema.find("additionalProperties");
        if (additional == schema.end() || !additional->is_boolean() || additional->get<bool>()) {
            return {};
        }

        const auto properties = schema.find("properties");
        for (const auto& [key, unused] : instance.items()) {
            static_cast<void>(unused);
            const bool declared =
                properties != schema.end() && properties->is_object() && properties->contains(key);
            if (!declared) {
                return pointer + "/" + escape_token(key);
            }
        }
        return {};
    }

    [[nodiscard]] std::string locate_object(const nlohmann::json& schema,
                                            const nlohmann::json& instance,
                                            const std::string& pointer) const {
        if (auto failure = locate_properties(schema, instance, pointer); !failure.empty()) {
            return failure;
        }
        if (auto failure = locate_required(schema, instance, pointer); !failure.empty()) {
            return failure;
        }
        return locate_extra(schema, instance, pointer);
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] std::string locate(const nlohmann::json& schema, const nlohmann::json& instance,
                                     const std::string& pointer) const {
        const nlohmann::json& resolved = resolve(schema);
        if (!resolved.is_object()) {
            return pointer;
        }

        for (const char* combinator : {"allOf", "oneOf", "anyOf"}) {
            const auto branches = resolved.find(combinator);
            if (branches == resolved.end() || !branches->is_array()) {
                continue;
            }
            for (const auto& branch : *branches) {
                if (!valid(branch, instance)) {
                    return locate(branch, instance, pointer);
                }
            }
        }

        if (instance.is_object()) {
            return locate_object(resolved, instance, pointer);
        }

        const auto items = resolved.find("items");
        if (instance.is_array() && items != resolved.end() && !items->is_array()) {
            for (std::size_t index = 0; index < instance.size(); ++index) {
                if (!valid(*items, instance.at(index))) {
                    return locate(*items, instance.at(index),
                                  pointer + "/" + std::to_string(index));
                }
            }
        }

        return pointer;
    }

    const nlohmann::json* root_;
};

} // namespace

CompiledSchema::CompiledSchema(std::string_view path) {
    const auto text = embedded_file(path);
    if (!text.has_value()) {
        throw std::logic_error("embedded schema not found: " + std::string(path));
    }

    document = nlohmann::json::parse(*text);

    valijson::SchemaParser parser;
    valijson::adapters::NlohmannJsonAdapter adapter(document);
    parser.populateSchema(adapter, schema);
}

const CompiledSchema& journal_schema() {
    static const CompiledSchema compiled("spec/journal.schema.json");
    return compiled;
}

const CompiledSchema& manifest_schema() {
    static const CompiledSchema compiled("spec/manifest.schema.json");
    return compiled;
}

const CompiledSchema& lir_schema() {
    static const CompiledSchema compiled("spec/lir.schema.json");
    return compiled;
}

const CompiledSchema& trace_schema(traces::TraceKind kind) {
    switch (kind) {
    case traces::TraceKind::action: {
        static const CompiledSchema compiled("spec/traces/action.schema.json");
        return compiled;
    }
    case traces::TraceKind::tool: {
        static const CompiledSchema compiled("spec/traces/tool.schema.json");
        return compiled;
    }
    case traces::TraceKind::context: {
        static const CompiledSchema compiled("spec/traces/context.schema.json");
        return compiled;
    }
    case traces::TraceKind::verification: {
        static const CompiledSchema compiled("spec/traces/verification.schema.json");
        return compiled;
    }
    case traces::TraceKind::attribution: {
        static const CompiledSchema compiled("spec/traces/attribution.schema.json");
        return compiled;
    }
    case traces::TraceKind::intervention: {
        static const CompiledSchema compiled("spec/traces/intervention.schema.json");
        return compiled;
    }
    case traces::TraceKind::entropy: {
        static const CompiledSchema compiled("spec/traces/entropy.schema.json");
        return compiled;
    }
    case traces::TraceKind::outcome: {
        static const CompiledSchema compiled("spec/traces/outcome.schema.json");
        return compiled;
    }
    }
    std::unreachable();
}

std::optional<SchemaError> validate(const CompiledSchema& compiled, const nlohmann::json& value) {
    valijson::Validator validator;
    valijson::adapters::NlohmannJsonAdapter adapter(value);
    valijson::ValidationResults results;
    if (validator.validate(compiled.schema, adapter, &results)) {
        return std::nullopt;
    }

    std::string message = "value does not match schema";
    if (results.numErrors() > 0) {
        message = results.begin()->description;
    }

    const Locator locator(compiled.document);
    return SchemaError{
        .message = std::move(message),
        .json_pointer = locator.locate(compiled.document, value),
    };
}

} // namespace leash::spec::detail

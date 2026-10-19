// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/drivers/anthropic/wire.hpp"

#include "leash/drivers/anthropic/consts.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace leash::drivers::anthropic {

namespace {

[[nodiscard]] std::string lower(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return out;
}

[[nodiscard]] bool is_default_port(std::string_view scheme, std::string_view port) {
    return (scheme == "https" && port == "443") || (scheme == "http" && port == "80");
}

/// `json::operator[]` is unchecked; `emplace` is the checked insertion for the
/// build-only use here (every key is inserted exactly once).
template <class T> void set_field(nlohmann::json& object, std::string_view key, T&& value) {
    object.emplace(std::string(key), std::forward<T>(value));
}

[[nodiscard]] std::expected<std::string, std::string> parse_port(std::string_view port) {
    std::uint32_t parsed = 0;
    const auto* begin = port.data();
    const auto* end = begin + port.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end || parsed == 0 || parsed > 65535) {
        return std::unexpected("base_url has an invalid port: " + std::string(port));
    }
    return std::string(port);
}

[[nodiscard]] std::expected<Endpoint, std::string> parse_authority(std::string_view authority,
                                                                   const std::string& scheme) {
    Endpoint endpoint;
    endpoint.scheme = std::string(scheme);

    if (authority.empty()) {
        return std::unexpected("base_url has no host");
    }

    // IPv6 literals arrive as [::1]:port; keep the brackets out of the host.
    if (authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) {
            return std::unexpected("base_url has an unterminated IPv6 literal");
        }
        endpoint.host = std::string(authority.substr(1, close - 1));
        if (close + 1 < authority.size()) {
            if (authority.at(close + 1) != ':') {
                return std::unexpected("base_url has an invalid authority");
            }
            auto port = parse_port(authority.substr(close + 2));
            if (!port) {
                return std::unexpected(port.error());
            }
            endpoint.port = std::move(*port);
        }
    } else {
        const std::size_t colon = authority.rfind(':');
        if (colon == std::string_view::npos) {
            endpoint.host = std::string(authority);
        } else {
            endpoint.host = std::string(authority.substr(0, colon));
            auto port = parse_port(authority.substr(colon + 1));
            if (!port) {
                return std::unexpected(port.error());
            }
            endpoint.port = std::move(*port);
        }
    }

    if (endpoint.host.empty()) {
        return std::unexpected("base_url has no host");
    }
    if (endpoint.port.empty()) {
        endpoint.port = endpoint.tls() ? "443" : "80";
    }
    return endpoint;
}

[[nodiscard]] std::string normalize_target(std::string_view path) {
    std::string target(path);
    while (!target.empty() && target.back() == '/') {
        target.pop_back();
    }
    if (!target.ends_with(consts::kMessagesPath)) {
        target += consts::kMessagesPath;
    }
    if (target.empty() || target.front() != '/') {
        target.insert(target.begin(), '/');
    }
    return target;
}

} // namespace

bool Endpoint::tls() const noexcept { return scheme == "https"; }

std::string Endpoint::host_header() const {
    std::string name = host.contains(':') ? "[" + host + "]" : host;
    if (is_default_port(scheme, port)) {
        return name;
    }
    return name + ":" + port;
}

std::expected<Endpoint, std::string> parse_base_url(std::string_view base_url) {
    if (base_url.empty()) {
        return std::unexpected("base_url is empty");
    }

    const std::size_t scheme_end = base_url.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::unexpected("base_url must start with https:// or http://");
    }

    const std::string scheme = lower(base_url.substr(0, scheme_end));
    if (scheme != "https" && scheme != "http") {
        return std::unexpected("unsupported base_url scheme: " + scheme);
    }

    const std::string_view rest = base_url.substr(scheme_end + 3);
    if (rest.empty()) {
        return std::unexpected("base_url has no host");
    }

    const std::size_t authority_end = rest.find_first_of("/?#");
    const std::string_view authority =
        authority_end == std::string_view::npos ? rest : rest.substr(0, authority_end);

    std::string_view path;
    if (authority_end != std::string_view::npos) {
        // Drop query/fragment: the spike only ever posts.
        path = rest.substr(authority_end);
        const std::size_t stop = path.find_first_of("?#");
        if (stop != std::string_view::npos) {
            path = path.substr(0, stop);
        }
    }

    auto endpoint = parse_authority(authority, scheme);
    if (!endpoint) {
        return std::unexpected(endpoint.error());
    }
    endpoint->target = normalize_target(path);
    return endpoint;
}

WireAsk build_ask(const AskRequest& request) {
    WireAsk out;
    out.model = request.model.has_value() && !request.model->empty()
                    ? *request.model
                    : std::string(consts::kDefaultModel);
    out.max_tokens = consts::kAskMaxTokens;
    if (request.max_tokens.has_value() && *request.max_tokens > 0) {
        out.max_tokens = *request.max_tokens;
    } else if (request.max_tokens.has_value()) {
        out.notes.emplace_back("max_tokens 0 is not usable; the ask default of " +
                               std::to_string(consts::kAskMaxTokens) + " was sent");
    }

    nlohmann::json body = nlohmann::json::object();
    set_field(body, consts::kFieldModel, out.model);
    set_field(body, consts::kFieldMaxTokens, out.max_tokens);
    set_field(body, consts::kFieldStream, true);

    // Tools -> system -> messages is the caching order of docs/09 §2. The
    // spike sends no tools; the system breakpoint is on the last block only.
    if (!request.system.empty()) {
        nlohmann::json blocks = nlohmann::json::array();
        for (std::size_t index = 0; index < request.system.size(); ++index) {
            nlohmann::json block = {
                {std::string(consts::kFieldType), std::string(consts::kTypeText)},
                {std::string(consts::kFieldText), request.system.at(index).text},
            };
            if (index + 1 == request.system.size()) {
                nlohmann::json cache;
                cache.emplace(std::string(consts::kFieldType), std::string(consts::kTypeEphemeral));
                block.emplace(std::string(consts::kFieldCacheControl), std::move(cache));
            }
            blocks.push_back(std::move(block));
        }
        set_field(body, consts::kFieldSystem, std::move(blocks));
    }

    nlohmann::json message = nlohmann::json::object();
    set_field(message, consts::kFieldRole, std::string(consts::kTypeUser));
    if (request.context.has_value() && !request.context->empty()) {
        // The rendered Context is a stable leading block; the prompt follows so
        // a repeated run on the same context hits the prompt cache.
        nlohmann::json content = nlohmann::json::array();
        content.push_back(nlohmann::json{
            {std::string(consts::kFieldType), std::string(consts::kTypeText)},
            {std::string(consts::kFieldText), *request.context},
        });
        content.push_back(nlohmann::json{
            {std::string(consts::kFieldType), std::string(consts::kTypeText)},
            {std::string(consts::kFieldText), request.prompt},
        });
        set_field(message, consts::kFieldContent, std::move(content));
    } else {
        set_field(message, consts::kFieldContent, request.prompt);
    }
    set_field(body, consts::kFieldMessages, nlohmann::json::array({std::move(message)}));

    set_field(
        body, consts::kFieldThinking,
        nlohmann::json{{std::string(consts::kFieldType), std::string(consts::kTypeAdaptive)}});

    nlohmann::json output = nlohmann::json::object();
    const Effort effort = request.effort.value_or(Effort::high);
    set_field(output, consts::kFieldEffort, std::string(to_string(effort)));
    if (request.schema.has_value()) {
        set_field(output, consts::kFieldFormat,
                  nlohmann::json{
                      {std::string(consts::kFieldType), std::string(consts::kTypeJsonSchema)},
                      {std::string(consts::kFieldSchema), *request.schema},
                  });
    }
    set_field(body, consts::kFieldOutputConfig, std::move(output));

    if (!request.attachments.empty()) {
        out.notes.emplace_back("ask spike does not send attachments; " +
                               std::to_string(request.attachments.size()) + " ignored");
    }

    out.body = std::move(body);
    return out;
}

} // namespace leash::drivers::anthropic

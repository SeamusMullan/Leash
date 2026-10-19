// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/syntax/diagnostic.hpp"

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace leash::syntax {
namespace {

struct SourceMap {
    std::string_view source;
    std::vector<std::uint32_t> line_starts;

    explicit SourceMap(std::string_view text) : source(text) {
        line_starts.push_back(0);
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (text.at(index) == '\n') {
                line_starts.push_back(static_cast<std::uint32_t>(index + 1));
            }
        }
    }

    [[nodiscard]] std::size_t line_of(std::uint32_t offset) const {
        const auto iterator = std::ranges::upper_bound(line_starts, offset);
        if (iterator == line_starts.begin()) {
            return 0;
        }
        return static_cast<std::size_t>(std::distance(line_starts.begin(), iterator) - 1);
    }

    [[nodiscard]] std::uint32_t line_start(std::size_t line) const { return line_starts.at(line); }

    [[nodiscard]] std::uint32_t line_end(std::size_t line) const {
        if (line + 1 < line_starts.size()) {
            return line_starts.at(line + 1) - 1;
        }
        return static_cast<std::uint32_t>(source.size());
    }

    [[nodiscard]] std::string_view line_text(std::size_t line) const {
        const std::uint32_t start = line_start(line);
        const std::uint32_t end = line_end(line);
        return source.substr(start, end - start);
    }
};

[[nodiscard]] std::string expand_tabs(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char character : text) {
        if (character == '\t') {
            result.append(4, ' ');
        } else if (character != '\r') {
            result.push_back(character);
        }
    }
    return result;
}

[[nodiscard]] std::string gutter(std::size_t width) {
    std::string padding(width + 1, ' ');
    return padding;
}

} // namespace

std::string_view severity_name(Severity severity) {
    switch (severity) {
    case Severity::Error:
        return "error";
    case Severity::Warning:
        return "warning";
    case Severity::Note:
        return "note";
    }
    return "error";
}

namespace {

void render_label(const Diagnostic& diagnostic, const SourceMap& map, const Span& span,
                  const std::string& label, std::ostream& out) {
    const std::uint32_t start = std::min<std::uint32_t>(span.start, map.source.size());
    const std::uint32_t end = std::min<std::uint32_t>(span.end, map.source.size());
    const std::size_t line = map.line_of(start);
    const std::uint32_t line_start = map.line_start(line);
    const std::uint32_t line_end = map.line_end(line);

    std::string text = expand_tabs(map.line_text(line));
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    const std::size_t visual_column =
        expand_tabs(map.source.substr(line_start, start - line_start)).size();
    const std::size_t visual_end =
        expand_tabs(map.source.substr(line_start, std::min(end, line_end) - line_start)).size();
    const std::size_t carets = std::max<std::size_t>(1, visual_end - visual_column);
    const std::size_t width = std::to_string(line + 1).size();

    out << gutter(width) << "|\n";
    out << (line + 1) << " |";
    if (!text.empty()) {
        out << ' ' << text;
    }
    out << '\n';
    out << gutter(width) << "| " << std::string(visual_column, ' ') << std::string(carets, '^');
    if (!label.empty()) {
        out << ' ' << label;
    }
    out << '\n';
    (void)diagnostic;
}

} // namespace

void render(const Diagnostic& diagnostic, std::string_view src, std::ostream& out,
            std::string_view filename) {
    const SourceMap map(src);
    out << severity_name(diagnostic.severity) << '[' << diagnostic.code
        << "]: " << diagnostic.message << '\n';

    if (diagnostic.labels.empty()) {
        out << "  --> " << filename << '\n';
    } else {
        const Span& primary = diagnostic.labels.front().first;
        const std::uint32_t start = std::min<std::uint32_t>(primary.start, src.size());
        const std::size_t line = map.line_of(start);
        const std::size_t column = start - map.line_start(line) + 1;
        const std::size_t width = std::to_string(line + 1).size();
        out << gutter(width) << "--> " << filename << ':' << (line + 1) << ':' << column << '\n';

        for (const auto& [span, label] : diagnostic.labels) {
            render_label(diagnostic, map, span, label, out);
        }
    }

    if (diagnostic.help.has_value()) {
        const std::size_t width =
            diagnostic.labels.empty()
                ? 2
                : std::to_string(map.line_of(std::min<std::uint32_t>(
                                     diagnostic.labels.front().first.start, src.size())) +
                                 1)
                      .size();
        out << gutter(width) << "= help: " << *diagnostic.help << '\n';
    }
}

std::string render_to_string(const Diagnostic& diagnostic, std::string_view src,
                             std::string_view filename) {
    std::ostringstream stream;
    render(diagnostic, src, stream, filename);
    return stream.str();
}

} // namespace leash::syntax

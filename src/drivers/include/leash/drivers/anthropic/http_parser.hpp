// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace leash::drivers::anthropic::http {

/// One step of feeding bytes to Beast's HTTP response parser.
struct ParseStep {
    /// Bytes the parser consumed. The caller keeps the rest and feeds it again.
    std::size_t consumed = 0;
    /// Body bytes produced by this call (moved out of the parser).
    std::string body;
    /// Status code once the header is complete; 0 before that.
    int status = 0;
    bool header_done = false;
    bool done = false;
    /// Parser diagnostic; empty on success. `need_buffer` is not an error.
    std::string error;
};

/// `boost::beast::http::response_parser<http::buffer_body>` behind a pimpl.
///
/// Beast is a Boost.Asio library and cannot be included in the same
/// translation unit as the standalone Asio this repository uses, so the parser
/// lives in its own TU and exchanges raw bytes with the client. The parser is
/// the real Beast `buffer_body` parser: it never buffers a whole body and stops
/// at `need_buffer` when the body chunk is full, exactly like
/// `http::read_some` does.
class ResponseParser {
  public:
    ResponseParser();
    ~ResponseParser();
    ResponseParser(ResponseParser&& other) noexcept;
    ResponseParser& operator=(ResponseParser&& other) noexcept;
    ResponseParser(const ResponseParser&) = delete;
    ResponseParser& operator=(const ResponseParser&) = delete;

    [[nodiscard]] ParseStep put(std::span<const char> bytes);

    [[nodiscard]] bool header_done() const noexcept;
    [[nodiscard]] bool done() const noexcept;
    [[nodiscard]] int status() const noexcept;

  private:
    class Impl;
    Impl* impl_ = nullptr;
};

} // namespace leash::drivers::anthropic::http

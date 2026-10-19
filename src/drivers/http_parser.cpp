// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/drivers/anthropic/http_parser.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace leash::drivers::anthropic::http {

class ResponseParser::Impl {
  public:
    Impl() {
        // SSE answers can be megabytes; never let Beast cap the body.
        parser.body_limit(std::numeric_limits<std::uint64_t>::max());
    }

    boost::beast::http::response_parser<boost::beast::http::buffer_body> parser;
    std::vector<char> body_buffer;
};

ResponseParser::ResponseParser() : impl_(new Impl()) {}

ResponseParser::~ResponseParser() { delete impl_; }

ResponseParser::ResponseParser(ResponseParser&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

ResponseParser& ResponseParser::operator=(ResponseParser&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

bool ResponseParser::header_done() const noexcept {
    return impl_ != nullptr && impl_->parser.is_header_done();
}

bool ResponseParser::done() const noexcept { return impl_ != nullptr && impl_->parser.is_done(); }

int ResponseParser::status() const noexcept {
    return impl_ == nullptr ? 0 : static_cast<int>(impl_->parser.get().result_int());
}

ParseStep ResponseParser::put(std::span<const char> bytes) {
    ParseStep step;
    if (impl_ == nullptr) {
        step.error = "parser was moved from";
        return step;
    }

    impl_->body_buffer.assign(bytes.size(), '\0');
    impl_->parser.get().body().data =
        impl_->body_buffer.empty() ? nullptr : impl_->body_buffer.data();
    impl_->parser.get().body().size = impl_->body_buffer.size();

    boost::beast::error_code error;
    step.consumed = impl_->parser.put(boost::asio::buffer(bytes.data(), bytes.size()), error);

    const std::size_t produced = impl_->body_buffer.size() - impl_->parser.get().body().size;
    step.body.assign(impl_->body_buffer.data(), produced);
    step.header_done = impl_->parser.is_header_done();
    step.status = step.header_done ? static_cast<int>(impl_->parser.get().result_int()) : 0;
    step.done = impl_->parser.is_done();

    if (error == boost::beast::http::error::need_buffer) {
        error = {};
    }
    if (error) {
        step.error = error.message();
    }
    return step;
}

} // namespace leash::drivers::anthropic::http

// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

/// Shared source-location types for the front end.
///
/// `FileId`, `Span` and `NodeId` are defined in `span.hpp` so that every
/// syntax header can include one small file without pulling in the lexer or
/// the AST. This header is the documented entry point named by WP-03:
///
///     #include "leash/syntax/source.hpp"
///
/// and exists so callers do not need to know the concrete header layout.
/// New source-mapping types belong in `span.hpp`; this file only re-exports
/// them.

#include "leash/syntax/span.hpp"

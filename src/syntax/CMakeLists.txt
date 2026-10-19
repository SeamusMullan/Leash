# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_library(leash_syntax STATIC syntax.cpp)
target_include_directories(leash_syntax PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(leash_syntax PRIVATE leash_warnings)

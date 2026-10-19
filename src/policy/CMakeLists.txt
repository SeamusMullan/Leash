# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_library(leash_policy STATIC policy.cpp)
target_include_directories(leash_policy PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(leash_policy PRIVATE leash_warnings)

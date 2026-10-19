# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_library(leash_store STATIC store.cpp)
target_include_directories(leash_store PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(leash_store PRIVATE leash_warnings)

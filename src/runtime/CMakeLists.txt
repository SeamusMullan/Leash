# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_library(leash_runtime STATIC
    program.cpp
    value.cpp
    env.cpp
    eval.cpp
    failure.cpp
    runtime.cpp)
target_include_directories(leash_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
find_package(Boost CONFIG REQUIRED)
target_link_libraries(leash_runtime
    PUBLIC
        leash_syntax
        leash_spec
        Boost::headers
    PRIVATE
        leash_warnings)

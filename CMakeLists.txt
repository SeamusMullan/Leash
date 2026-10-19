# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
cmake_minimum_required(VERSION 3.30)
project(leash VERSION 0.1.0 LANGUAGES CXX)

set(LEASH_LICENSE "Apache-2.0")
set(LEASH_AUTHORS "The Leash Authors")
set(LEASH_REPOSITORY "https://github.com/SeamusMullan/Leash")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

option(LEASH_BUILD_FUZZERS "Build the libFuzzer targets (Clang only)" OFF)

add_library(leash_warnings INTERFACE)
target_compile_options(leash_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Werror -Wswitch-enum)

set(LEASH_MODULES
    spec
    syntax
    types
    ir
    runtime
    store
    policy
    effects
    drivers
    trace
    std
    cli
    lsp
    eval
    testkit)

foreach(module IN LISTS LEASH_MODULES)
    add_subdirectory(src/${module})
endforeach()

add_subdirectory(tests)

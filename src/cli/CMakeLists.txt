# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
find_package(CLI11 CONFIG REQUIRED)

add_library(leash_cli_core STATIC
    cmd/doctor.cpp
    exit.cpp)
target_include_directories(leash_cli_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(leash_cli_core PRIVATE leash_warnings)

add_executable(leash_cli main.cpp)
set_target_properties(leash_cli PROPERTIES OUTPUT_NAME leash)
target_link_libraries(leash_cli PRIVATE leash_cli_core leash_warnings CLI11::CLI11)

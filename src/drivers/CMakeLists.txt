# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors

find_package(Boost 1.83 REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(blake3 CONFIG REQUIRED)

set(LEASH_BLAKE3_TARGET BLAKE3::blake3)
if(NOT TARGET BLAKE3::blake3 AND TARGET blake3::blake3)
    set(LEASH_BLAKE3_TARGET blake3::blake3)
elseif(NOT TARGET BLAKE3::blake3 AND TARGET blake3)
    set(LEASH_BLAKE3_TARGET blake3)
endif()
if(NOT TARGET ${LEASH_BLAKE3_TARGET})
    message(FATAL_ERROR "blake3 CMake target not found")
endif()

add_library(leash_drivers STATIC
    drivers.cpp
    mock.cpp
    wire.cpp
    sse.cpp
    usage.cpp
    http_parser.cpp
    spike.cpp)

target_include_directories(leash_drivers PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(leash_drivers
    PRIVATE
        leash_warnings
        ${LEASH_BLAKE3_TARGET}
        OpenSSL::SSL
        OpenSSL::Crypto
    PUBLIC
        leash_spec
        nlohmann_json::nlohmann_json
        Boost::headers
        Boost::system)

# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors

find_package(nlohmann_json CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(valijson CONFIG REQUIRED)

# Homebrew exports `valijson`; the vcpkg port exports `valijson::valijson`.
set(LEASH_VALIJSON_TARGET valijson)
if(TARGET valijson::valijson)
    set(LEASH_VALIJSON_TARGET valijson::valijson)
endif()

set(LEASH_TOML_TARGET tomlplusplus::tomlplusplus)
if(NOT TARGET tomlplusplus::tomlplusplus AND TARGET tomlplusplus)
    set(LEASH_TOML_TARGET tomlplusplus)
endif()

# Every normative file is embedded so the installed binary carries the exact
# schema it was built against. std/prices.toml lives outside spec/ but belongs
# to the same contract.
file(GLOB_RECURSE LEASH_SPEC_EMBED_FILES RELATIVE "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/spec/*")
list(FILTER LEASH_SPEC_EMBED_FILES EXCLUDE REGEX "/$")
list(APPEND LEASH_SPEC_EMBED_FILES "std/prices.toml")
list(SORT LEASH_SPEC_EMBED_FILES)

set(LEASH_SPEC_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(LEASH_SPEC_EMBED_CPP "${LEASH_SPEC_GENERATED_DIR}/spec_embedded.cpp")
file(MAKE_DIRECTORY "${LEASH_SPEC_GENERATED_DIR}")

set(LEASH_SPEC_EMBED_DEPENDS "")
foreach(relative IN LISTS LEASH_SPEC_EMBED_FILES)
    list(APPEND LEASH_SPEC_EMBED_DEPENDS "${PROJECT_SOURCE_DIR}/${relative}")
endforeach()

add_custom_command(
    OUTPUT "${LEASH_SPEC_EMBED_CPP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LEASH_SPEC_GENERATED_DIR}"
    COMMAND "${CMAKE_COMMAND}"
        "-DSPEC_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT=${LEASH_SPEC_EMBED_CPP}"
        "-DFILES=${LEASH_SPEC_EMBED_FILES}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spec.cmake"
    DEPENDS ${LEASH_SPEC_EMBED_DEPENDS} "${CMAKE_CURRENT_SOURCE_DIR}/embed_spec.cmake"
    COMMENT "Embedding spec/ and std/prices.toml into leash_spec"
    VERBATIM)

add_library(leash_spec STATIC
    spec.cpp
    spec_error.cpp
    money.cpp
    usage.cpp
    journal.cpp
    manifest.cpp
    lir.cpp
    traces.cpp
    profile.cpp
    prices.cpp
    validation.cpp
    "${LEASH_SPEC_EMBED_CPP}")

target_include_directories(leash_spec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${LEASH_SPEC_GENERATED_DIR}")

target_link_libraries(leash_spec
    PRIVATE
        leash_warnings
        ${LEASH_TOML_TARGET}
        ${LEASH_VALIJSON_TARGET}
    PUBLIC
        nlohmann_json::nlohmann_json)

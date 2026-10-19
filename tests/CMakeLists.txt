# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
find_package(Catch2 3 CONFIG REQUIRED)

add_subdirectory(spec)
add_subdirectory(syntax)
add_subdirectory(types)
add_subdirectory(ir)
add_subdirectory(runtime)
add_subdirectory(store)
add_subdirectory(policy)
add_subdirectory(effects)
add_subdirectory(drivers)
add_subdirectory(trace)
add_subdirectory(std)
add_subdirectory(lsp)
add_subdirectory(eval)
add_subdirectory(testkit)
add_subdirectory(cli)

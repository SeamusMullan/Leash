# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
set shell := ["bash", "-uc"]

check: fmt-check tidy test

fmt:
    clang-format -i $(git ls-files '*.cpp' '*.hpp')

fmt-check:
    clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.hpp')

tidy:
    run-clang-tidy -p build/dev -warnings-as-errors '*'

test:
    ctest --preset dev

headers:
    scripts/check-headers.sh

licences:
    scripts/check-licences.sh

lint: fmt-check tidy headers licences

doc:
    @echo "API documentation arrives in WP-24 with the Doxygen target."

configure:
    cmake --preset dev

build: configure
    cmake --build build/dev

run-example FILE:
    build/dev/src/cli/leash run {{FILE}}

ci: fmt-check
    cmake --preset ci
    cmake --build build/ci
    run-clang-tidy -p build/ci -warnings-as-errors '*'
    ctest --preset ci

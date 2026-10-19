# WP-01: Repository scaffold, CMake build, CI

Milestone: M0 · Size: S · Status: see `docs/status.md`

## Goal
After this WP the repository is a buildable C++23 CMake project with a target
for every module the architecture names, a green CI pipeline, a task runner,
contribution rules and a `leash` binary that prints its version and runs a
`doctor` stub. Every later WP starts from this skeleton instead of inventing
layout, warnings, formatting or tooling.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/03-runtime-architecture.md` §2 (module layout, dependency direction)
- `docs/adr/0011-implementation-language-cpp.md` (compiler floor, library choices, ownership and error rules)
- `docs/10-cli-and-tooling.md` §1 (command list; only `--version` and `doctor` are built here)
- `docs/subagents/README.md` and `CLAUDE.md` (conventions, definition of done)
- `docs/14-licensing.md` (header policy, dependency allowlist)
- `docs/adr/0010-licensing.md` (why Apache-2.0)

## Scope
- Root `CMakeLists.txt`: `cmake_minimum_required(VERSION 3.30)`,
  `project(leash VERSION 0.1.0 LANGUAGES CXX)`, C++23 with compiler extensions
  off, `include(CTest)`, and a `leash_warnings` INTERFACE target carrying
  `-Wall -Wextra -Wpedantic -Werror -Wswitch-enum`. Every target links it.
  The file adds the 15 module directories and `tests/`.
- One `CMakeLists.txt` per module for `spec`, `syntax`, `types`, `ir`,
  `runtime`, `store`, `policy`, `effects`, `drivers`, `trace`, `std`, `cli`,
  `lsp`, `eval` and `testkit`. Each defines `leash_<module>` with its public
  include directory; `src/cli` defines the `leash_cli` executable with output
  name `leash`. `spec` and `store` are empty shells filled by WP-02 and WP-06;
  `policy` and `testkit` are empty shells filled by WP-09/WP-11 and WP-25.
- `CMakePresets.json`: `dev` (Debug, Ninja, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`,
  binary directory `build/dev`), `ci` (RelWithDebInfo, `build/ci`) and `asan`
  (Debug, `-fsanitize=address,undefined -fno-omit-frame-pointer`,
  `build/asan`) configure, build and test presets. Every configure preset takes
  the toolchain from `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`. This
  replaces a pinned toolchain file: the floor is Clang 19+ or GCC 14+, and CI
  pins the LLVM version.
- `vcpkg.json`: manifest with `"version-string": "0.1.0"`, a pinned
  `builtin-baseline` commit, and `boost-asio`, `boost-beast`, `boost-process`,
  `boost-uuid`, `openssl`, `nlohmann-json`, `cli11`, `spdlog`, `tomlplusplus`,
  `blake3`, `valijson`, `catch2` and `rapidcheck`. The manifest is the lock
  file: commit it, bump the baseline deliberately, and use a binary cache in
  CI.
- Each library module: one public header at
  `src/<module>/include/leash/<module>/<name>.hpp` with `#pragma once` and a
  one-line responsibility comment copied from docs/03 §2, one `.cpp`, and one
  passing Catch2 test in `tests/<module>/<name>_test.cpp` registered in
  `tests/<module>/CMakeLists.txt` with CTest and the module name as its label.
  The `cli` module tests the exit-code mapping and the doctor table renderer.
- `.clang-format` committed: LLVM base, four-space indent, 100 columns.
  `.clang-tidy` committed: `bugprone-*`, `cppcoreguidelines-*`, `modernize-*`,
  `performance-*` and `readability-*`, with `WarningsAsErrors: '*'` and
  `HeaderFilterRegex: 'src/.*'`.
- `.github/workflows/ci.yml` with a matrix of `ubuntu-latest` and
  `macos-latest` and four jobs: `format` (`clang-format --dry-run --Werror`
  over every tracked `.hpp` and `.cpp`), `tidy` (`run-clang-tidy -p build/ci
  -warnings-as-errors '*'`), `sanitizers` (`cmake --preset asan`, build,
  `ctest --preset asan`) and `test` (configure, build and `ctest --preset ci`
  on both runners). macOS installs Homebrew LLVM and exports `CC` and `CXX`,
  because Apple Clang has partial C++23 library coverage. Cache the vcpkg
  binary cache and ccache, keyed on `vcpkg.json`.
- `justfile` with recipes `check` (format check + clang-tidy + ctest), `test`,
  `fmt`, `lint` (format check + clang-tidy + header check + licence check),
  `doc` (a no-op that states the Doxygen target arrives in WP-24),
  `run-example FILE` (`build/dev/src/cli/leash run {{FILE}}`) and `ci` (what CI
  runs, so it can be reproduced locally). Helper recipes may compose these.
- `CONTRIBUTING.md`: branch naming `wp/NN-slug`, conventional commits, PR
  template contents, how to run `just ci`, where briefs live, and a licensing
  section stating that contributions are accepted under Apache-2.0 section 5
  with a Developer Certificate of Origin sign-off (`git commit -s`), linking
  `docs/14-licensing.md`.
- `.gitignore`: `build/`, `vcpkg_installed/`, `.cache/`, `.leash/runs/`,
  `.leash/cas/`, `.leash/mcp/`, `*.log`, editor files.
- Licensing, per `docs/14-licensing.md`. `LICENSE`, `NOTICE` and `AUTHORS`
  already exist at the repository root; do not rewrite them. Declare
  `LEASH_LICENSE` (`Apache-2.0`), `LEASH_AUTHORS` (`The Leash Authors`) and
  `LEASH_REPOSITORY` once in the root `CMakeLists.txt`; CMake has no per-target
  licence field. Add the two-line SPDX header to every file this WP creates
  that supports comments, including `justfile`, `CMakeLists.txt` and `.cmake`
  files, shell scripts and the CI workflow.
- `scripts/check-licences.sh` plus `scripts/dependency-licences.tsv`: the
  licence gate for the dependency set. The table has one row per direct
  dependency (`port`, SPDX identifier, where the identifier was read from); the
  script fails when a dependency in `vcpkg.json` has no row, when a row names a
  licence outside the allowlist of `docs/14-licensing.md` §5 as extended by
  ADR-0011 (`BSL-1.0`, the OpenSSL licence, `CC0-1.0` for blake3), or when a row
  has no matching dependency. Copyleft and proprietary terms fail the build.
  `--manifest <path>` checks an alternative manifest, and a checked-in fixture
  under `scripts/fixtures/` supplies the GPL case. Wire it into `just lint` and
  CI.
- `scripts/check-headers.sh` (under fifty lines, no dependencies) implementing
  `docs/14-licensing.md` §10: walk tracked files, skip the exclusions of §3,
  fail on a missing or malformed header, and support `--fix` to insert one.
  Wire it into `just lint` and CI.
- `THIRD-PARTY-NOTICES.md` with a header row and no entries yet.
- Leave `LICENSE`, `NOTICE`, `AUTHORS` and `.mailmap` exactly as they are; they
  are already correct (`docs/14-licensing.md` §8).
- `src/cli/main.cpp`: CLI11 root with subcommands `doctor` and a hidden
  `__unimplemented` catch; `leash --version` prints `leash 0.1.0` through
  `set_version_flag`. `doctor` prints a table: `hermes` (found/version or
  missing), `claude` (same), `codex` (same), `ANTHROPIC_API_KEY` (set/unset),
  `git` version, `cmake` version; exits 0 always in this WP.
- `README.md` "Building" section: toolchain, `VCPKG_ROOT`, `just ci`, module
  map.

## Out of scope
Any language, runtime or driver code. Dependency versions beyond pinning.
Release/packaging workflows. Benchmarks. Adopting the full REUSE specification with a `LICENSES/` directory; the header format is already REUSE compatible and the migration can happen later.

## Interfaces
Every module target is `leash_<module>`; `src/cli` builds `leash_cli` with
output name `leash`. Headers live at
`src/<module>/include/leash/<module>/<name>.hpp`, start with `#pragma once`,
include what they use, and declare their contents in `namespace leash::<module>`.

```cmake
# root CMakeLists.txt
add_library(leash_warnings INTERFACE)
target_compile_options(leash_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Werror -Wswitch-enum)
```

Dependency allowlist (vcpkg ports, pinned by the manifest baseline):
`boost-asio`, `boost-beast`, `boost-process`, `boost-uuid`, `openssl`,
`nlohmann-json`, `cli11`, `spdlog`, `tomlplusplus`, `blake3`, `valijson`,
`catch2` and `rapidcheck`. Anything else needs a one-line justification in the
PR.

`leash_cli` layout other WPs extend: `src/cli/main.cpp` (CLI11 root),
`src/cli/cmd/<name>.cpp` with `src/cli/include/leash/cli/cmd/<name>.hpp` one
pair per subcommand, `src/cli/exit.cpp` mapping outcomes to the exit codes in
docs/10 §1.

## Deliverables
- Build files listed in Scope: root `CMakeLists.txt`, `CMakePresets.json`,
  `vcpkg.json`, one `CMakeLists.txt` per module, `tests/CMakeLists.txt` plus
  the per-module test lists, `.clang-format` and `.clang-tidy`; 15 module
  targets and the `leash` executable that build.
- `.github/workflows/ci.yml`, `justfile`, `CONTRIBUTING.md`, `.gitignore`,
  `scripts/check-headers.sh`, `scripts/check-licences.sh` with
  `scripts/dependency-licences.tsv`, `THIRD-PARTY-NOTICES.md`.
- `README.md` Building section.

## Acceptance criteria
- `cmake --preset dev && cmake --build build/dev` succeeds on a clean checkout
  with Clang 19+ or GCC 14+ and the pinned vcpkg baseline.
- `ctest --preset dev` reports at least 15 passed tests, 0 failed.
- `run-clang-tidy -p build/dev -warnings-as-errors '*'` exits 0.
- `clang-format --dry-run --Werror` over every tracked `.hpp` and `.cpp` exits
  0.
- `just ci` reproduces the CI steps (configure, build, format check,
  clang-tidy, ctest) and exits 0.
- `build/dev/src/cli/leash --version` prints `leash 0.1.0`.
- `just lint` fails when a header is removed from any tracked source file, and
  `scripts/check-headers.sh --fix` restores it so that `just lint` passes again.
- `scripts/check-licences.sh` exits 0, and
  `scripts/check-licences.sh --manifest scripts/fixtures/gpl-vcpkg.json` exits
  non-zero.
- `build/dev/src/cli/leash doctor` prints one row per probe and exits 0 even
  when `hermes`/`claude`/`codex` are missing.
- `grep -RInE '\bnew\b|\bdelete\b|reinterpret_cast' src/ --include='*.cpp'
  --include='*.hpp'` finds nothing outside comments.
- CI workflow passes on a push to a branch (link the run in the PR).

## Notes and pitfalls
- Keep `vcpkg.json` and its `builtin-baseline` committed. It is the lock file:
  a floating baseline makes CI non-reproducible, and Boost 1.86 or later is
  required for Boost.Process v2.
- Configure a vcpkg binary cache locally and in CI
  (`VCPKG_DEFAULT_BINARY_CACHE` plus `actions/cache`); without it every job
  rebuilds Boost and OpenSSL from source and CI time grows without bound.
- Do not use `nlohmann::ordered_json` as the project-wide JSON type; WP-04
  needs canonical (sorted) serialisation and will handle ordering explicitly.
  The default `nlohmann::json` already keeps object keys sorted.
- HTTP is Boost.Beast over `asio::ssl`; never add a blocking client, because it
  would stall the `io_context`.
- macOS builds use Homebrew LLVM because Apple Clang has partial C++23 library
  coverage. Put it first on `PATH` and set `CC`/`CXX` in CI.
- `doctor` must not spawn `hermes`/`claude`/`codex` with prompts; `--version`
  only, with a 5 s timeout per probe.
- Every variant gets one `overloaded` visitor header and a unit test that
  visits every alternative; `-Wswitch-enum` covers enum switches. WP-01 only
  fixes the convention and the warning flag; later WPs add the variants.
- The dependency direction in docs/03 §2 is `spec ← syntax ← types ← ir ←
  runtime ← {effects, drivers, trace} ← cli/lsp/eval`, with `store` and `policy`
  beside `spec` as leaves. Apart from `leash_warnings`, no module links another
  in this WP, so later WPs choose their edges. `drivers` stays independent of
  `runtime` (runtime depends on drivers) per WP-05.
- No `doc` recipe in WP-01. WP-24 adds the Doxygen target and the recipe.
- No CPack metadata in v0. `LEASH_LICENSE`, `LEASH_AUTHORS` and
  `LEASH_REPOSITORY` are plain CMake strings; the licence surface is `LICENSE`,
  `NOTICE` and the SPDX headers. WP-24 revisits packaging.

## Dependencies
Hard: none. The licence decision is made; see `docs/adr/0010-licensing.md`.

## Estimated size
S (≤ 1 day).

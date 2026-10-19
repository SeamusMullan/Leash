# Contributing

Leash accepts contributions under section 5 of the Apache-2.0 licence, with a
Developer Certificate of Origin sign-off. Sign every commit with
`git commit -s`; by doing so you certify that you wrote the change or have the
right to submit it under the project licence. See `docs/14-licensing.md`.

## Work packages

Work is split into work packages (WP-01 … WP-27) with briefs in
`docs/subagents/`. Read `docs/subagents/README.md` first. Check
`docs/status.md`: a WP must be `todo` or assigned to you, and its hard
dependencies must be `done`.

Branch naming is `wp/NN-short-slug`. Commit messages follow the conventional
form: `feat(runtime): …`, `fix(parser): …`, `docs(spec): …`.

## Before you open a PR

Run `just ci`. It configures and builds the project, checks formatting, runs
clang-tidy and runs the test suite. `just lint` adds the header and dependency
licence checks.

Every source file starts with the two-line SPDX header from
`docs/14-licensing.md`. New dependencies need one line of justification in the
PR and a licence on the allowlist.

## PR description

State what was delivered, how it was verified (commands and their results),
deviations from the brief or specs, and follow-ups. A WP without the tests
named in its acceptance criteria is not done.

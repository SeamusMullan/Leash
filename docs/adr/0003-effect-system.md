# ADR-0003 Static effect system

Status: accepted.

## Context
The determinism guarantee needs a static line between pure code and effects.
Options: dynamic tracking only (every effect goes through the gateway, but pure
code could still read the clock via a library), or a type-level effect set.

## Decision
Every function has an inferred effect set drawn from a fixed vocabulary
(`llm tool shell fs.read fs.write net spawn human clock rand env`). Declared
clauses (`! {…}`) are upper bounds. Higher-order std functions are
effect-polymorphic. User-defined effects are out of scope for v1.

## Alternatives
Algebraic effects with handlers in user code (more general, more complex);
monadic encoding (hostile to the target audience); no static tracking (loses
the guarantee and the "explain this program's surface" tooling).

## Decision on declaration strictness (2026-09-12)
Effect declarations on `pub` items are optional by default and mandatory for the
standard library. `leash check --strict` enforces the rule, runs against `std`
in CI from WP-09 onward, and can be turned on per package with `strict = true`
under `[check]` in `leash.toml`. The default is expected to flip to on before
1.0. Rationale and the flip condition are in `docs/02-language-spec.md` §5.3.

## Consequences
- Simple fixed vocabulary keeps inference a set-union fixed point.
- `leash check --explain-effects` becomes a security review tool: the effect
  set of a flow is its attack surface.
- Some convenience is lost (no reading time in pure helpers); acceptable.

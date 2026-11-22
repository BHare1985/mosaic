# Tasks

## 1. Mechanical families

- [x] 1.1 Unused includes and unused using-declarations removed (`misc-include-cleaner`, `--fix`). Verified by: build green, suite
      counts unchanged.
- [x] 1.2 `nodiscard` on accessors that return a value the caller must use; designated initialisers for aggregate literals.
      Verified by: suite counts unchanged.
- [x] 1.3 `contains` instead of `find() != end()`; `data()` instead of `&v[0]`. Verified by: suite counts unchanged.
- [x] 1.4 Braces on single-statement bodies (the largest family), one pass, one commit. Verified by: suite counts and assertion
      totals identical before and after.

## 2. The opinionated ones

- [x] 2.1 Implicit bool conversions fixed site by site, with `NOLINT` plus a reason where the check's fix reads worse. Verified
      by: no implicit-bool findings remain, and each suppressed site has a reason.
- [x] 2.2 Remaining naming residuals in tests and tools. Verified by: no naming findings remain.
- [x] 2.3 `performance-*` findings triaged: fixed, or recorded as deliberate with a reason. Verified by: the count reaches zero.

## 3. Gate

- [x] 3.1 Remove `MOSAIC_LINT_STYLE` and make the CI lint step fail on style findings as well as correctness ones. Verified by:
      introducing a deliberate style violation turns CI red; reverting it turns it green.
- [x] 3.2 `conventions.md` gains the "adding a check" order (enable, fix, gate) and states that the lint step is not advisory.
      Verified by: the doc names the order and the gate.
- [x] 3.3 Record in `decisions.md` that the style backlog reached zero and the gate closed, with the date and the count that was
      paid off. Verified by: the dated entry.

## 4. Close out

- [x] 4.1 The site's contributing page states the enforced style and the two review rules. Verified by: the page names
      `clang-format`, `clang-tidy` and the no-`using namespace` rule.
- [x] 4.2 Close STYLE-2 on the board; `./scripts/check_todo_ids.sh`; `./scripts/ci.sh` green in all four configurations with style
      gated.

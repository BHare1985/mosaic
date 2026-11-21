# Design

## Context

`.clang-tidy` carries a check list where correctness families (`bugprone-*`, `clang-analyzer-*`) are errors and style families
are reported. `scripts/ci.sh` runs `run-clang-tidy -j` and greps for correctness findings only; `MOSAIC_LINT_STYLE=1` switches the
grep to the style count (681 today). `.clang-format` is LLVM-based, 100 columns, `UseTab: Never`.

The families, from the last count:

| Family | Shape of the fix |
|---|---|
| `readability-braces-around-statements` | mechanical, largest |
| `modernize-use-nodiscard` | mechanical on accessors |
| `readability-identifier-naming` | already mostly done; residuals in tests |
| `modernize-use-designated-initializers` | mechanical for aggregate literals |
| `modernize-use-contains` | mechanical (`find() != end()` → `contains`) |
| `readability-container-data-pointer`, `-avoid-c-arrays` | a handful |
| `performance-*` (unused includes, copies) | a handful, some are real wins |
| `readability-implicit-bool-conversion` | the opinionated one: decides where `== 0` is clarity |

## Decisions

### D1 - Fix in family order, one pass per family, each pass buildable

Order: unused includes (shrinks everything else) → `nodiscard` → designated initialisers → `contains` → braces → implicit bool
conversions → naming residuals. Braces late because a brace change touches lines that earlier families also touch, and a single
diff per file is easier to review than an interleaved one.

Each pass ends with `./scripts/ci.sh fast` and a commit. That gives a bisectable history and means a single family can be reverted
without losing the others.

### D2 - Implicit bool conversion is a decision per site, not a sed

`readability-implicit-bool-conversion` fires on `if (ptr)` and `if (count)`. For pointers the project already prefers `!= nullptr`;
for counts, `if (count > 0)` is the honest spelling. But in a few places `if (!found)` on an `optional` is clearly right, and the
check can be silenced with an explicit `bool`. The pass is therefore the slowest and comes last, and a site that reads worse after
the fix is a site where the check was wrong - which is recorded in a `NOLINT` with a reason, or by turning that check off and
saying why in the config's comment.

### D3 - Gate last, and gate everything

The gate flips only after the count is zero, then style is treated exactly like correctness: `MOSAIC_LINT_STYLE` is removed and
the CI step fails on any finding. A gate that goes red during the work would block unrelated changes, which is the reason the split
existed.

### D4 - The un-automatable rules stay in prose, checked in review

"No `std::` in headers" is enforced by `google-global-names-in-headers`, so it is automatic. "No `using namespace`" mostly shows up
as `using namespace std;`, which `google-build-using-namespace` catches; the rest is review. `conventions.md` carries both, and the
new "adding a check" note tells the next contributor the order: enable, fix, gate.

## Risks

- **A 681-finding sweep is a large diff with no behaviour change**, which is exactly the diff reviewers skim. Mitigated by one
  commit per family, by the suite's counts being identical before and after, and by `.clang-format` having already made formatting
  uniform. (`.git-blame-ignore-revs` was deliberately not adopted - decision D6 in `decisions.md` - so the mitigation is commit
  granularity, not blame-skipping.)
- **The vendored/test code may contain genuinely unidiomatic code** where the check is wrong. Recorded as `NOLINT` with a reason
  rather than by disabling the family.

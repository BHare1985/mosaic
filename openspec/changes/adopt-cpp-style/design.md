# Design

## Context

See proposal.md - Why. What is actually there today: `.clang-format` is WebKit-based with `ColumnLimit: 0`, `PointerAlignment:
Left`, `AccessModifierOffset: -4`; `clang-format` 23.1.5 is installed (user-local); **no `clang-tidy` is installed anywhere**;
`scripts/ci.sh` already gates formatting on every tracked source; the codebase is ~5k lines across ten modules plus tests, and its
naming already matches the table this change writes down.

## Goals / Non-Goals

**Goals:** one written style, enforced by two configs and a CI step; formatting that cannot drift; the naming table discoverable
without reading 5k lines.

**Non-Goals:** renaming anything, a house dialect nobody else uses, `WARNINGS_AS_ERRORS` from day one, or a new tool in the
critical path when it is not installed.

## Decisions

**D1 - LLVM as the base, not WebKit.**
WebKit was inherited and then set to `ColumnLimit: 0`, which means no line limit at all - the one setting that matters most for
reviewable diffs. LLVM plus explicit overrides (4 spaces, 100 columns, attach braces, left pointers, no namespace indentation) is
the consensus profile and the least surprising to a reader arriving from elsewhere.
*Alternative:* keep WebKit and add overrides - rejected: the base's remaining defaults (short-function handling, initializer
indentation) are Apple-flavoured and would need more overrides than LLVM does.

**D2 - `ColumnLimit: 100`.**
Long enough for the longest contract comment sentences without reflowing them constantly, short enough to read in a split pane.
The existing code mostly sits under 120 and will reflow once; that is the one-time cost of a limit.

**D3 - Include categories follow this project, not a template.**
Project headers are included with angle brackets (`#include <mosaic/core/record.h>`), so the obvious `^"..."` first category would
never match. The categories are `^<mosaic/` (this project), then `<...>` (everything else), sorted case-sensitively within each -
and clang-format's `IncludeBlocks: Regroup` keeps the groups apart.

**D4 - `std::` noise: Option 1.**
`using namespace ...` stays banned in every file (`google-build-using-namespace`), and a header may never carry a
using-declaration (`google-global-names-in-headers`) because it would import a name into `namespace mosaic` for every includer.
A `.cpp` may import what it uses:

```cpp
#include <mosaic/node/node.h>

using std::string;
using std::vector;

// ... string and vector used unqualified from here
```

*Why not `using namespace std;` in `.cpp` files:* it pulls in hundreds of short names (`count`, `distance`, `min`, `max`, `byte`,
`move`) that collide with ours, and the collisions appear later, on a standard-library upgrade, in code nobody edited. Targeted
imports give the same typing relief without that.
*Scope of the mechanical pass:* one `.cpp` at a time, tests green after each, and only names actually used in that file. Headers
keep `std::` everywhere.

**D5 - clang-tidy findings are triaged before the gate tightens.**
`WarningsAsErrors` starts empty: the first run over an existing codebase always has findings, and a gate that is red from the
moment it lands teaches people to ignore it. Findings are fixed where the fix is behaviour-preserving, checks are excluded with a
written reason where they are noise, and only then does the CI step start failing.

**D6 - The reformat is one commit, named in `.git-blame-ignore-revs`.**
A mechanical change touching every file destroys `git blame` unless it is demarcated. One commit, one line in the ignore file,
documented in `conventions.md`.

**D7 - clang-tidy is optional, like doxygen.**
It is not installed here and it is not needed to build or test. The CI step reports `SKIPPED - clang-tidy not found` and says how
to install it, exactly as the documentation step does. A gate that silently passes when its tool is missing is worse than no gate.

**D8 - The naming table is aspirational only where the code already complies.**
Every construct in the table was checked against the tree before writing it down: types `PascalCase`, functions and variables
`snake_case`, private members with a trailing `_`, constants `kPascalCase`, scoped enum values `PascalCase`. So the table
documents the status quo and clang-tidy enforces it. Any mismatch found later is a bug in the code, not a reason to weaken the
table.

## Risks / Trade-offs

- **A very large diff** -> the reformat is isolated in its own commit and excluded from `git blame`; the behavioural diff for
  every other change stays readable.
- **clang-tidy noise erodes trust** -> the excluded checks are listed with reasons in `.clang-tidy`, and `WarningsAsErrors` is
  turned on only after a clean run.
- **Include regrouping can break a build** by reordering a header that was silently providing a symbol -> the tree is built in
  both configurations after the reformat, which is what the CI gate is for.
- **Two tools, two versions, one style** -> both versions are recorded in `conventions.md`; a mismatch shows up as a formatting
  diff, not a silent style change.

## Open Questions

- Whether the `std::` import pass should also cover test sources (it should - they are the noisiest) or stop at the library. Doing
  the tests is more work for less reviewer value; deferred to whichever comes first.
- Whether to add a `clang-format` version pin to CI so a version bump does not reformat unrelated code. Deferrable: CI installs the
  distro package today.

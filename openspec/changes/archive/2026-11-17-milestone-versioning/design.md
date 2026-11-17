# Design

## Context

See proposal.md - Why. Today: `project(Mosaic VERSION 0.1.0)`, PackageProject generating `include/mosaic/version.h` with four
`#define`s, `standalone/source/main.cpp` printing `MOSAIC_VERSION`, and nothing in the documentation that says which version it
describes.

## Goals / Non-Goals

**Goals:** a version whose number means something; one source of truth; a header a C++20 program can use without preprocessor
tricks; documentation that states what it describes.

**Non-Goals:** semver tooling, git tags, release automation, an ABI stability promise, or renaming the project.

## Decisions

**D1 - The minor number counts closed milestones: `0.<n>.0`.**
The board already numbers milestones M0..M6 and each one is a coherent, demonstrable increment. Tying the minor to that count means
the version needs no separate release ceremony to be meaningful, and the rule is checkable by reading `TODO.md`.
*Alternatives:* "bump when it feels right" (what happened: nothing); calendar versioning (says nothing about capability);
`0.19.0` counting capabilities instead of milestones - rejected: capabilities accrete without closing anything.

**D2 - CMake is the source of truth; the header is generated from it.**
`project(... VERSION 0.6.0)` feeds a `configure_file` of `cmake/version.h.in`. PackageProject's macro header is replaced because we
need `inline constexpr` values, not only macros; the `MOSAIC_VERSION` macro is kept inside our own header as an alias so CMake
`install`/`packageProject` references keep working.

**D3 - The header carries the *reason* for the number.**
`kMilestonesClosed` sits next to the components, so the next person bumping the version sees the rule in the code rather than only in
a Markdown file. A `static_assert` compares the parsed components against `kVersion` at compile time, so a hand-edit that leaves the
string and components disagreeing fails the build.

**D4 - The compile fails if the header stops being generated from CMake.**
Two `static_assert`s in the header: the parsed `kVersion` string must equal the numeric components, and the `MOSAIC_VERSION`
macro must equal `kVersion`. Verified by hardcoding a version in `cmake/version.h.in` - the build stops with both assertions
failing.
*What this does not catch, and what does:* a bump in `CMakeLists.txt` alone is **correct** - CMake is the single source and both
the header and the test definition are generated from it. Drift between the number and the prose (CHANGELOG, the "Describes
0.6.0" lines) cannot be a test; it is a review item, stated once in `conventions.md`.

**D5 - The changelog is written from the archive, not from memory.**
Every archived change has a summary; the 0.6.0 entry lists what those changes delivered, in the language the proposals used. That
keeps the changelog honest and makes the next one cheap.

**D6 - Design docs state the version they describe.**
One line under the title of each `docs/design/*.md`: "Describes 0.6.0". It is a claim that can be checked, and when a doc is revised
for a later version the line moves with it.

## Risks / Trade-offs

- **A version in six places drifts** -> D2 makes CMake authoritative, D4 makes drift a failing test, D6 makes documentation state
  which version it is.
- **`0.6.0` implies an API promise** -> the changelog entry says plainly that `0.x` minors may break and that the C ABI is not yet
  frozen.
- **Replacing PackageProject's header could break install packaging** -> the macro alias keeps the names it generates; the
  full-stack build and `packageProject` are both part of the definition of done.

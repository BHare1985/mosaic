# Tasks

## 1. One source of truth

- [x] 1.1 Set `project(Mosaic VERSION 0.6.0)` and add `cmake/version.h.in` generating the idiomatic header: `inline constexpr
      std::string_view kVersion`, `kVersionMajor/Minor/Patch` as `std::uint32_t`, `kMilestonesClosed`, a `static_assert` tying
      components to the string, and `MOSAIC_VERSION` as a macro alias. Verified by: the header is generated during configure and
      the tree builds with no PackageProject macro header in the include path.
- [x] 1.2 Stop generating PackageProject's macro-only version header and keep everything that referenced it working.
      Verified by: `cmake -S all -B build` configures without the old header, and `packageProject(...)` still installs.
- [x] 1.3 Guard against a hand-maintained header: two `static_assert`s (components vs string, macro alias vs `kVersion`), plus a
      doctest case comparing `kVersion` with the version CMake passes into the test target. Verified by: hardcoding a version in
      `cmake/version.h.in` fails the build with both assertions; bumping CMake alone does **not** fail, and should not - CMake is
      the single source and everything else is generated from it. Recorded in design D4 rather than pretending the test catches
      more than it does.

## 2. Show it

- [x] 2.1 `Mosaic version` prints `mosaic::kVersion`; `mosaicd` prints it at startup. Verified by: both binaries report 0.6.0.
- [x] 2.2 Show the version on the documentation site (the about page and the footer). Verified by: the rendered page contains
      0.6.0.

## 3. Record it

- [x] 3.1 Write `CHANGELOG.md` with a 0.6.0 entry assembled from the archived changes, and a line stating that `0.x` minors may
      break and the C ABI is not frozen. Verified by: every archived change is represented, and the entry names no feature that a
      spec does not.
- [x] 3.2 Add the numbering rule to `docs/design/conventions.md` and a dated entry to `docs/design/decisions.md` (the minor counts
      closed milestones; the patch carries fixes). Verified by: the rule is stated once, and `TODO.md`'s milestone table agrees.
- [x] 3.3 Add "Describes 0.6.0" under the title of each `docs/design/*.md`. Verified by: `rg -c "Describes 0.6.0" docs/design`
      matches the number of documents.

## 4. Close out

- [x] 4.1 `./scripts/ci.sh` green: four configurations, sanitizers, documentation and format, plus the drift test. Verified by: the
      CI run.
- [x] 4.2 Add and close a board row for versioning. Verified by: `./scripts/check_todo_ids.sh` exits 0.

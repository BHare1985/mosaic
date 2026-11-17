# Tasks

## 1. One source of truth

- [ ] 1.1 Set `project(Mosaic VERSION 0.6.0)` and add `cmake/version.h.in` generating the idiomatic header: `inline constexpr
      std::string_view kVersion`, `kVersionMajor/Minor/Patch` as `std::uint32_t`, `kMilestonesClosed`, a `static_assert` tying
      components to the string, and `MOSAIC_VERSION` as a macro alias. Verified by: the header is generated during configure and
      the tree builds with no PackageProject macro header in the include path.
- [ ] 1.2 Stop generating PackageProject's macro-only version header and keep everything that referenced it working.
      Verified by: `cmake -S all -B build` configures without the old header, and `packageProject(...)` still installs.
- [ ] 1.3 Add the drift test: CMake passes its `PROJECT_VERSION` to the test target and a doctest case compares it with
      `mosaic::kVersion`. Verified by: temporarily bumping CMake alone makes the test fail.

## 2. Show it

- [ ] 2.1 `Mosaic version` prints `mosaic::kVersion`; `mosaicd` prints it at startup. Verified by: both binaries report 0.6.0.
- [ ] 2.2 Show the version on the documentation site (the about page and the footer). Verified by: the rendered page contains
      0.6.0.

## 3. Record it

- [ ] 3.1 Write `CHANGELOG.md` with a 0.6.0 entry assembled from the archived changes, and a line stating that `0.x` minors may
      break and the C ABI is not frozen. Verified by: every archived change is represented, and the entry names no feature that a
      spec does not.
- [ ] 3.2 Add the numbering rule to `docs/design/conventions.md` and a dated entry to `docs/design/decisions.md` (the minor counts
      closed milestones; the patch carries fixes). Verified by: the rule is stated once, and `TODO.md`'s milestone table agrees.
- [ ] 3.3 Add "Describes 0.6.0" under the title of each `docs/design/*.md`. Verified by: `rg -c "Describes 0.6.0" docs/design`
      matches the number of documents.

## 4. Close out

- [ ] 4.1 `./scripts/ci.sh` green: four configurations, sanitizers, documentation and format, plus the drift test. Verified by: the
      CI run.
- [ ] 4.2 Add and close a board row for versioning. Verified by: `./scripts/check_todo_ids.sh` exits 0.

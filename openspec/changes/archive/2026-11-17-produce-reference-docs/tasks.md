# Tasks

## 1. Make it build

- [x] 1.1 Install `doxygen` and confirm the existing `GenerateDocs` target runs end to end, fixing whatever it needs (the config
      has never been executed). Verified by: a site of 164 pages appears under the build directory with the landing page, the
      design set and the namespaces. Installed user-locally (release tarball in `~/.local/opt`, symlinked into `~/.local/bin`)
      because `sudo` needs a password here - and that is the honest option for a documented prerequisite anyway. Two config bugs
      surfaced: the design docs were outside `INPUT`, so every cross-reference in a module README failed, and `M_SHOW_UNDOCUMENTED`
      is an m.css tag that plain doxygen warns about.
- [x] 1.2 Write `scripts/docs.sh`: configure, generate, and report where the site landed; skip with a clear warning and a
      non-zero-free message when `doxygen` is absent, matching how `scripts/ci.sh` treats a missing `clang-format`. Verified by:
      running it on this machine produces a site and a one-line report.
- [x] 1.3 Add the reference site to `CITATION`-free documentation only — no deployment, no network at build time beyond the
      existing m.css fetch. Verified by: regenerating offline works after the first fetch.

## 2. Make it worth reading

- [x] 2.1 Turn the module READMEs into site pages through `Doxyfile`'s input list, one page per module. Verified by: the
      READMEs beside the headers are in the site's file tree, and the Modules page names each one and the capability it owns.
- [x] 2.2 Fix the navigation so it leads with the landing page, then the modules, then the API. Verified by: a reader landing on
      the site can reach any module in one click.
- [x] 2.3 Add the authority note: the specs are normative, the design set is rationale, and the site links to both rather than
      restating them. Verified by: the landing page states it and links resolve.

## 3. Make it stay honest

- [x] 3.1 Set `WARN_AS_ERROR` for the public headers and fix whatever it exposes. Verified by: the build now fails on any
      documentation warning and passes on the current tree - with `WARN_IF_UNDOCUMENTED` on, the public headers produce zero
      warnings, so the contract comments are genuinely complete. It caught two real problems while being set up: a duplicate
      section label between two pages, and an unresolvable reference into the fetched reference trees.
- [x] 3.2 Add the documentation step to `scripts/ci.sh` so it runs in the same gate as the tests, reporting with them.
      Verified by: `./scripts/ci.sh` ends with the docs result alongside the four test configurations and the format check.
- [x] 3.3 Add a CI job that installs the prerequisites and runs the same script. Verified by: the job's command is the script,
      and a broken comment fails it.

## 4. Close out

- [x] 4.1 Document the prerequisites and the command in `README.md`, and note in `docs/design/README.md` that the site is
      generated rather than edited by hand. Verified by: both files name `doxygen` and the script.
- [x] 4.2 Add and close a board row for the documentation build. Verified by: `./scripts/check_todo_ids.sh` exits 0.
- [x] 4.3 Definition of done: `./scripts/ci.sh` green including the documentation step, clang-format clean, capability spec rows
      updated. Verified by: the CI run.

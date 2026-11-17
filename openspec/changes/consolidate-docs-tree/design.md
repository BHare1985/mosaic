# Design

## Context

See proposal.md - Why. The generator lives in `documentation/` because that is where the C++ template put it; the design set lives
in `docs/` because that is the convention GitHub renders. The site is built by `scripts/docs.sh` → `cmake --build --target
GenerateDocs` → m.css's `doxygen.py`, and it reads `README.md` plus `include/` only.

## Goals / Non-Goals

**Goals:** one folder for documentation and its generator; no dangling paths; the site still builds and lands somewhere obvious.

**Non-Goals:** moving the design set, changing the site's content, renaming the target, or touching the m.css version.

## Decisions

**D1 - The generator becomes `docs/site/`, not `docs/documentation/`.**
`site` says what the folder produces. `docs/documentation/` would repeat the word that caused the confusion.
*Alternative:* put the Doxyfile at the repository root - rejected: it is one of three files that belong together, and the root
already carries enough configuration.

**D2 - The output moves with it, to `build/all/site/doxygen/`.**
The build directory mirrors the source layout, so a reader who finds `docs/site/` knows where the output is without reading CMake.
*Trade-off:* every existing bookmark to the old path breaks. Acceptable for a path that has existed for one change and is
git-ignored anyway.

**D3 - Relative paths in `docs/site/CMakeLists.txt` become three levels up, not one.**
The file moves from `<root>/documentation/` to `<root>/docs/site/`, so `../cmake/CPM.cmake` becomes `../../cmake/CPM.cmake` and
`SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..` becomes `/../../..`. This is the only part of the move that can silently produce a wrong
build, so it is a named task with the build as its verification.

**D4 - `docs/README.md` becomes the map.**
One short table: `design/` rationale and the board, `site/` the generator and its pages, `references/` fetched upstream material
that is never rendered. The question "where does this document go?" then has an answer that does not depend on remembering history.

## Risks / Trade-offs

- **A stale path survives somewhere** -> `rg -n "documentation/" ` after the move is the check, and it is a task rather than an
  afterthought.
- **The site output path is referenced in two places** -> `scripts/docs.sh` and the README; both are in the same task.
- **Git history for the moved files** -> `git mv` keeps it; the reformat-style ignore file is a separate concern and is unaffected.

## Open Questions

- Whether `docs/design/` should keep its numeric prefixes (`00-overview.md` …). They work; the move does not touch them.

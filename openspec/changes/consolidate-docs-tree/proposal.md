# Proposal

## Why

There are two folders whose names both mean "documentation", and they hold different things:

- `docs/` holds the writing: the design set, the direction note, upstream findings, fetched reference copies.
- `documentation/` holds no documentation at all - it is the *generator*: `Doxyfile`, the m.css `conf.py`, the three hand-written
  pages, and the CMake target that runs them.

The name dates from the project template, where `documentation/` was the docs directory and `docs/` did not exist. A reader
looking for documentation opens `documentation/` and finds a Doxyfile; a reader looking for the site configuration opens `docs/` and
finds Markdown. Neither is wrong, and that is the problem.

## What Changes

- `documentation/` is removed. Its four pieces move to `docs/site/`: the Doxyfile, the m.css config, the pages, and the CMake
  subdirectory.
- Every path that pointed at `documentation/` is updated: the `all/` build's subdirectory, the Doxyfile input list, the site's
  output path, and the references in the docs, the README and the board.
- `docs/README.md` gains a short map of the folder, so "where does this go?" has one answer: `design/` for rationale, `site/` for
  the generator, `references/` for fetched upstream material.
- The generated site's path changes from `build/all/documentation/doxygen/` to `build/all/site/doxygen/`, which is where
  `scripts/docs.sh` and the README already point after this change.
- **Not** in scope: changing what the site contains (it is deliberately the public surface only), or moving the design set into the
  site.

## Capabilities

None. Files move and build paths change; nothing a capability spec describes is different. The change declares `skip_specs: true`.

## Impact

- `documentation/` deleted; `docs/site/` created with the same four items.
- `all/CMakeLists.txt` (subdirectory name and binary dir), `documentation/CMakeLists.txt` → `docs/site/CMakeLists.txt` (relative
  paths to the project root), `docs/site/Doxyfile` (input paths), `scripts/docs.sh` (output path),
  `docs/site/pages/about.dox` (the one line that names the design set).
- Documentation references: `README.md`, `docs/README.md`, `docs/design/README.md`, `AGENTS.md` if it names the folder.

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
- **Publish the site (GitHub Pages).** A workflow job builds with `scripts/docs.sh` and deploys the artifact with
  `actions/deploy-pages`, so the documentation has a URL instead of a build directory. D4 forbids contributing upstream to
  libzmq/czmq/etc.; it says nothing about hosting our own site, so this gets a dated decision entry rather than a quiet bend of D4.
- **Polish the prior art.** Every Dafka and Malamute mention gains its link (repositories, RFC 46, the Malamute whitepaper, the
  ZeroMQ Guide), and a new page records what this project borrowed and what it did differently - in our words, with theirs cited.
- **Borrow their document shapes.** Dafka's README leads with *Guarantees*, *Design*, *Usage*; Malamute's documentation leads with
  principles, then actors, then flow control. Two new pages follow that shape because it is a good one: **Guarantees** (what a
  follower is promised, linking to the capability specs rather than restating them) and **Design at a glance** (writer, replica,
  responder, the log, repair, credits).
- **Not** in scope: changing what the site contains beyond those two pages (it is deliberately the public surface only), moving the
  design set into the site, or vendoring upstream text.

## Capabilities

None. Files move and build paths change; nothing a capability spec describes is different. The change declares `skip_specs: true`.

## Impact

- `documentation/` deleted; `docs/site/` created with the same four items.
- `all/CMakeLists.txt` (subdirectory name and binary dir), `documentation/CMakeLists.txt` → `docs/site/CMakeLists.txt` (relative
  paths to the project root), `docs/site/Doxyfile` (input paths), `scripts/docs.sh` (output path),
  `docs/site/pages/about.dox` (the one line that names the design set).
- Documentation references: `README.md`, `docs/README.md`, `docs/design/README.md`, `AGENTS.md` if it names the folder.
- `.github/workflows/pages.yml` (new), `docs/site/pages/guarantees.dox` and `docs/site/pages/design-at-a-glance.dox` (new),
  `docs/site/conf.py` navbar, `docs/design/decisions.md` (a dated entry for hosting).
- **Licensing:** Malamute, czmq and Dafka are MPL-2.0. Their structure and ideas are fair game; their paragraphs are not. Nothing in
  this change copies upstream text - the two new pages are written from this project's own specs.

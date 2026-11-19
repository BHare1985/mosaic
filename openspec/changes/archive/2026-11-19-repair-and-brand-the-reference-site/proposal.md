# Proposal

## Why

The published reference site is a shell with a working navbar. `annotated.html`, `namespaces.html` and `files.html` are
generated at ~6 KB each and contain an **empty** `<ul class="m-doc">` - no classes, no namespaces, no files - while the
individual class pages underneath them are complete. So the three navigation entries a reader uses to find their way around the
API lead to blank pages. The site also carries no identity: no logo, no project brief, the default theme, and a footer that says
only "Mosaic. Created with Doxygen and m.css".

This is a defect against the project's own requirement that the site contain "an entry per module and per public namespace"
(`docs/reference-site`), and it is the reason the site reads as boring: there is nothing on it to look at except prose pages.

## What Changes

- **Document the namespaces.** m.css only indexes a compound that has a brief or a detailed description, and it nests members
  under their parent; every public type lives in the `mosaic` namespace, which is undocumented, so the namespace is dropped and
  takes all of its members with it. A documented `@namespace mosaic` (and `mosaic::daemon`) is what makes Classes and Namespaces
  list anything - verified locally: the class list goes from 0 entries to 31.
- **Stop offering a page that cannot have content.** No header carries an `@file` brief, so m.css renders **no file pages at
  all** (zero `*_8h.html`), which means the Files tree can only ever point at nothing. The Files entry leaves the navbar, with
  the reason recorded rather than the page silently kept as a dead end.
- **Give the site a face.** The project logo in the navbar (the same historical asset URL the README uses, which m.css renders
  without copying or downloading anything), a one-line project brief under the title, and the logo's purple as the theme colour.
- **Check that the site is not empty.** `scripts/check_site.sh` asserts the generated index pages list entries, and runs in the
  same gate as the tests, so "the site builds" stops meaning "the site has content".

No requirement changes to what the site *contains*, only to whether it is actually reachable and identified.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `docs/reference-site`: adds requirements that the site indexes the public surface it documents, that no navigation entry is a
  dead end, that the site carries the project's identity, and that an empty index fails the check. The existing requirements
  (builds from the public headers, warnings fail the build, guides link rather than copy, the build is part of the check) are
  unchanged.

## Impact

- `docs/site/Doxyfile` (identity options), `docs/site/conf.py` (navigation), a documented namespace block in `docs/site/pages/`.
- `scripts/check_site.sh` (new) and its call sites in `scripts/ci.sh` / `scripts/docs.sh`.
- `docs/site/README.md` and `docs/design/08-testing-strategy.md` gate list updated to match.
- No library, header, protocol or dependency change: the generator, its pinned revision and the public headers are untouched.

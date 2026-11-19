# Design

## Context

See `proposal.md` - Why. What the diagnosis established, because the fix follows from it:

- The generator is m.css (`mosra/m.css`, pinned commit) driving Doxygen 1.11 through `documentation/doxygen.py`, which reads the
  Doxyfile template in `docs/site/Doxyfile` (configured by `docs/site/CMakeLists.txt` and rendered by `scripts/docs.sh`).
- m.css includes a compound in an index **only if it has a brief or a detailed description**
  (`doxygen.py`: `compound.has_details = ... compound.brief or compounddef.find('detaileddescription') ...`) and nests compounds
  under their parent. Everything public lives in `namespace mosaic`, whose compound is undocumented, so it fails that test and is
  dropped - and its orphaned members go with it. Measured on the current tree: index symbols **0**; after documenting the
  namespace, **1 namespace with 31 member links** in the rendered class page.
- Files are the same story one level down, and worse: no header carries an `@file` brief, so m.css renders **zero** `*_8h.html`
  pages. Documenting every directory produces a directory tree whose leaves link nowhere, which is a different dead end.
- The identity options are **Doxyfile** options, not `conf.py` ones: `PROJECT_LOGO`, `PROJECT_BRIEF`, `M_THEME_COLOR` (m.css reads
  them from the Doxyfile; `doxygen.py` even maps `M_THEME_COLOR` onto its `THEME_COLOR`). `PROJECT_LOGO` is emitted through a
  `basename_or_url` filter and absolute URLs are skipped by the copy step, so a URL in the Doxyfile renders the logo with no file
  in the repository - verified in a probe build.
- The site is published only when `main` is pushed (`.github/workflows/pages.yml`); it has never deployed, which is why the
  defect went unseen.

## Goals / Non-Goals

**Goals**

- Classes and Namespaces list what the site documents, and every navigation entry opens a page with content.
- The site looks like this project: logo, one-line brief, its colour.
- A regression (empty index, or the logo vanishing) fails the gate rather than being discovered by a reader.

**Non-Goals**

- Upgrading or forking m.css. The pin stays; the fix is the documentation m.css needs, not a new generator.
- Adding `@file` briefs to forty headers so the Files page can exist. The unit of documentation here is the type - the contract
  comments live on types and classes - and duplicating a module README onto each file would be documentation written to satisfy a
  navigation entry.
- Grouping the API into m.css "modules" (`@defgroup`). It would be a nicer map than the namespace tree, and it is a coherent
  follow-up, but it is a larger edit to every header and is not needed to fix a blank page.
- Publishing the site as part of this change (Pages settings are the maintainer's; see `docs/design/09-operations.md`).

## Decisions

### D1 - Document the namespace, not the files

`@namespace mosaic` and `@namespace mosaic::daemon` get briefs, in the site's own page set (`docs/site/pages/`), next to the
prose that already introduces the module map. That single block is what turns the class and namespace indexes from empty shells
into a browsable tree - and it is the truth the site was missing: the namespace is the project's public interface and had no
place saying so.

Rejected: **documenting every directory** (14 `@dir` blocks) because it buys a Files tree whose leaves are empty; and **`@file`
briefs on every header** because it duplicates module READMEs across forty files to satisfy navigation.

### D2 - The Files entry leaves the navigation, with the reason written down

`docs/site/conf.py`'s `LINKS_NAVBAR1` drops `Files`. The comment above it says why: no file pages are generated because no header
is documented as a file, and the site is organised by type. The page is still emitted by the generator (it is in m.css's default
index set); nothing links to it, so it is unreachable rather than a dead end. If a later change documents files, the entry comes
back in the same file.

Rejected: passing `--index-pages` to stop generating `files.html` at all - more machinery (a CMake argument list) for a page
nobody can reach.

### D3 - Identity comes from the Doxyfile, and the logo is referenced

`docs/site/Doxyfile` gains:

```
PROJECT_BRIEF = "Brokerless, verifiable, replicated logs for C++20"
PROJECT_LOGO  = https://github.com/BHare1985/mosaic/assets/2180284/73ac71af-2309-4efd-a24a-bf8d8b021eb1
M_THEME_COLOR = "#4A0380"
```

Three details that cost a build to learn, so they are written down: the options belong in the **Doxyfile** (setting them in
`conf.py` silently does nothing); the colour **must be quoted**, because `#` starts a Doxyfile comment and an unquoted value
becomes empty; and the logo is the same artwork the README uses, referenced by URL, so the site needs no asset and no
build-time download (m.css skips copying absolute URLs by design).

`PROJECT_BRIEF` renders as the thin subtitle beside the project name; `M_THEME_COLOR` sets the page's theme colour meta (and the
compiled theme's accent), which is the logo's purple `#4A0380`.

### D4 - The check reads the generated pages, not the source

`scripts/check_site.sh` inspects the built HTML: `annotated.html` and `namespaces.html` must each contain a
`<ul class="m-doc">` with at least one `<a href>`, and no entry in the navbar's `LINKS_NAVBAR1` may point at a page whose list is
empty. It runs from `scripts/ci.sh` only when the site was built (doxygen present); otherwise it prints the same kind of skip
`scripts/docs.sh` prints, because a check that silently passes when it cannot run is worse than no check.

Rejected: asserting on `docs/site/conf.py` and the Doxyfile contents (proves intent, not rendering), and screenshot diffing
(brittle, and nobody would maintain it).

### D5 - The publishing story is documented, not changed

`docs/site/README.md` gains the two lines a reader needs: the site is deployed only from `main` by the Pages workflow, the
repository setting it needs, and how to preview a branch (run the workflow on it and download the `github-pages` artifact, or
build locally with `./scripts/docs.sh`). Nothing about permissions or workflows changes here.

## Risks / Trade-offs

- **The namespace brief is the whole fix, so it must not drift** → The site's page set is the natural home for it, and the
  emptiness check fails loudly if it is ever removed.
- **The logo depends on a GitHub attachment** → Same exposure as the README already accepted; one `src` swap fixes it, and the
  check does not depend on the image loading.
- **A theme colour is a small change for a big complaint** → Stated honestly: the logo and the brief do the visual work; the
  colour is a meta/accent, not a redesign. A custom stylesheet is the follow-up if more is wanted.
- **`M_THEME_COLOR` is an m.css-specific option on a pinned revision** → Both options were verified against the pinned commit
  before being specified, and the check would catch a silent no-op by looking at the rendered page.

## Migration Plan

Documentation only. After merge: `./scripts/docs.sh` then `./scripts/ci.sh`. The next `main` push publishes the fixed site;
rollback is `git revert` on `docs/site/` and the new script.

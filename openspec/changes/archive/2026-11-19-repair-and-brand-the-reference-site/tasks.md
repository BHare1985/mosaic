# Tasks

## 1. Make the indexes list something

- [x] 1.1 Document the public namespaces (`mosaic`, `mosaic::daemon`) with a brief each, in the site's own page set under
      `docs/site/pages/`, so m.css stops dropping the parent that every class hangs from. Verified by: `annotated.html` lists the
      documented classes after a rebuild (31 entries today) and `namespaces.html` lists `mosaic`.
- [x] 1.2 Remove the `Files` entry from `docs/site/conf.py`'s `LINKS_NAVBAR1` and record why in a comment there: no header is
      documented as a file, so no file pages are generated and the tree can only point at nothing. Verified by: no navigation
      entry opens a page with an empty list, and the comment states the reason.
- [x] 1.3 Rebuild and read every navigation entry by hand. Verified by: Classes, Namespaces, Pages (about, guarantees, design,
      authority, modules) each list at least one item, and nothing links to an empty page.

## 2. Give it a face

- [x] 2.1 Set `PROJECT_BRIEF`, `PROJECT_LOGO` (the historical asset URL, quoted or bare as the format allows) and
      `M_THEME_COLOR` (quoted - an unquoted `#` starts a Doxyfile comment) in `docs/site/Doxyfile`. Verified by: the rendered
      header carries `<img src="https://github.com/BHare1985/mosaic/assets/...">`, the brief appears beside the project name, and
      the page carries a theme-colour meta with `#4A0380`.
- [x] 2.2 Confirm the site needs no asset and no network: build with the logo URL in place on a machine with no image file in
      the repository. Verified by: the build succeeds, m.css skips copying the absolute URL, and the header still references it.
- [x] 2.3 Write down how the site is published and how to preview it from a branch - the Pages setting, the workflow that deploys
      only from `main`, `workflow_dispatch` for an on-demand run, and `./scripts/docs.sh` for a local build - in
      `docs/site/README.md`. Verified by: a reader can follow it without reading `.github/workflows/pages.yml`.

## 3. Make it impossible to regress silently

- [x] 3.1 Add `scripts/check_site.sh`: the generated `annotated.html` and `namespaces.html` each contain a `<ul class="m-doc">`
      with at least one entry, and no `LINKS_NAVBAR1` entry points at an empty list. Skip with a printed reason when the site has
      not been built. Verified by: the script passes on the fixed site, and fails naming the page when the namespace
      documentation is removed and the site rebuilt.
- [x] 3.2 Call it from `scripts/ci.sh` beside the documentation step, so the gate reports it. Verified by: a full
      `./scripts/ci.sh` run prints the site check and its result.

## 4. Close out

- [x] 4.1 Update `docs/site/README.md`'s description of what the site contains, and the gate list in
      `docs/design/08-testing-strategy.md`, to name the site check. Verified by: the names match what the scripts print, and
      `./scripts/docs.sh` and `./scripts/ci.sh` are green.
- [x] 4.2 Look at the built site as a first-time reader: the logo, the brief, and a route from the front page to any class in
      three clicks. Verified by: that route exists and the pages it crosses are not empty.

## Workflow follow-up

- Publish the fixed site by pushing `main` with *Settings → Pages → Source: GitHub Actions* set; the Pages workflow does the rest.
- Archive with `openspec-archive-change` once the site is published and verified in the browser.

# docs/site — the documentation generator

This folder builds the published reference site; it is not the documentation itself.

- `Doxyfile` — what to read (root README, public headers, the pages here), how to render it, and the site's identity
  (`PROJECT_BRIEF`, `PROJECT_LOGO`, `M_THEME_COLOR`)
- `conf.py` — m.css configuration: navbar and theme
- `pages/*.dox` — the hand-written pages (about, authority, the module map, guarantees, design, bindings), plus the
  `@namespace` blocks that make the Classes and Namespaces indexes exist
- `CMakeLists.txt` — the `GenerateDocs` target

Build it with `./scripts/docs.sh` (or `cmake --build build --target GenerateDocs`). Output lands under `build/all/site/`, the
site itself in `build/all/site/html/index.html`.

What the site contains is deliberately the *public surface*: the README, the public headers with their contract comments, the
module READMEs beside those headers, and these pages. The design set in `../design/` is rationale for contributors and is not
rendered.

## Three things that look like mistakes and are not

- **The namespaces are documented in `pages/about.dox`.** m.css indexes only a compound that carries a brief or a description,
  and it nests members under their parent; every public type lives in `mosaic`, so an undocumented namespace is dropped and takes
  all of its members out of the Classes and Namespaces indexes with it. The `@namespace` blocks at the end of that page are load
  bearing.
- **There is no `Files` entry in the navbar.** No header carries an `@file` brief, so m.css generates no file pages, and the
  directory tree could only point at nothing. The site is organised by type. Put the entry back if a header ever gains a brief.
- **`PROJECT_LOGO` is a URL, and `M_THEME_COLOR` is quoted.** m.css emits an absolute logo URL unchanged and skips copying it, so
  the site needs no image in the repository; and an unquoted `#` in the colour starts a Doxyfile comment, silently emptying it.

`./scripts/check_site.sh` reads the generated HTML and fails when an index page lists nothing or a navbar entry opens a shell. It
prints `SKIPPED` when the site has not been built, rather than passing quietly.

## Publishing, and previewing a branch

The site is published by `.github/workflows/pages.yml`, which uploads exactly what `scripts/docs.sh` builds - there is no
committed `gh-pages` branch and no second copy of the site in git. It deploys on a push to `main`, and needs one repository
setting a workflow cannot make for you: *Settings → Pages → Source: **GitHub Actions***. Without it the deploy job fails loudly
rather than publishing nothing.

To see a branch's docs without merging it:

1. **Locally, always**: `./scripts/docs.sh && open build/all/site/html/index.html`.
2. **On GitHub, from the branch**: *Actions → Pages → Run workflow*, choosing the branch (`workflow_dispatch` is enabled), then
   download the `github-pages` artifact from the run and open `index.html`. This does not touch the published site.
3. **As the published site**: only a push to `main` does that. If you would rather publish from a branch that keeps the built
   site (`gh-pages`, folder `/`), switch *Settings → Pages → Source* to *Deploy from a branch* - note that a repository has one
   Pages site, so that choice replaces the Actions deployment, and the branch needs a `.nojekyll` file so Jekyll leaves the
   generated paths alone.

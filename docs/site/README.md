# docs/site — the documentation generator

This folder builds the published reference site; it is not the documentation itself.

- `Doxyfile` — what to read (root README, public headers, the pages here) and how to render it
- `conf.py` — m.css configuration: navbar and theme
- `pages/*.dox` — the hand-written pages (about, authority, modules, guarantees, design)
- `CMakeLists.txt` — the `GenerateDocs` target

Build it with `./scripts/docs.sh` (or `cmake --build build --target GenerateDocs`). Output lands under `build/all/site/`.

What the site contains is deliberately the *public surface*: the README, the public headers with their contract comments, the
module READMEs beside those headers, and these pages. The design set in `../design/` is rationale for contributors and is not
rendered.

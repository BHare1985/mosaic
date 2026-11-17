# Tasks

## 1. Move the generator

- [ ] 1.1 `git mv documentation docs/site` so history follows the files. Verified by: `git log --follow docs/site/Doxyfile` shows
      the file's earlier commits.
- [ ] 1.2 Fix the relative paths in `docs/site/CMakeLists.txt`: the CPM include and the `SOURCE_DIR` for the Mosaic package now
      sit three levels up. Verified by: `cmake -S all -B build` configures and `docs/site` is added as a subdirectory.
- [ ] 1.3 Update `all/CMakeLists.txt`: the subdirectory is `../docs/site` and its binary directory is `all/site`. Verified by: the
      build directory contains `all/site/doxygen/` after a build.
- [ ] 1.4 Update `docs/site/Doxyfile`'s input list: the pages live at `@DOXYGEN_PROJECT_ROOT@/docs/site/pages`. Verified by:
      the generated site still contains the about, authority and modules pages.

## 2. Point everything that referred to the old folder

- [ ] 2.1 Update `scripts/docs.sh`: the site path is `build/all/site/doxygen/html/index.html`. Verified by: `./scripts/docs.sh`
      reports a page count and the path it wrote.
- [ ] 2.2 Update prose references in `README.md`, `docs/README.md`, `docs/design/README.md` and `docs/site/pages/about.dox`.
      Verified by: `rg -n "documentation/" --glob '!build/**'` finds nothing outside prose that is deliberately about the word.

## 3. Make the folder self-explanatory

- [ ] 3.1 Rewrite `docs/README.md` as the map: `design/` (rationale and the board), `site/` (the generator and its pages),
      `references/` (fetched, never rendered), and the top-level notes. Verified by: the file answers "where does this go?" for each
      of the three.
- [ ] 3.2 Note in `docs/site/README.md` (new, three lines) what the folder is and the one command that builds it.
      Verified by: the file exists and the command matches `scripts/docs.sh`.

## 4. Publish the site

- [ ] 4.1 Add `.github/workflows/pages.yml`: configure, `./scripts/docs.sh`, upload `build/all/site/doxygen/html` as the Pages
      artifact, deploy it. Needs `pages: write`, `id-token: write` and an `environment: github-pages`. Verified by: the workflow
      file is valid YAML, and a dry run (`act` or a manual dispatch) reaches the deploy step.
- [ ] 4.2 Record the hosting decision as a dated entry in `docs/design/decisions.md`, naming what D4 does and does not forbid.
      Verified by: the entry exists and cites D4.
- [ ] 4.3 Say in `README.md` what the repository setting must be (`Settings -> Pages -> Source: GitHub Actions`) for the job to
      succeed. Verified by: the sentence names the setting.

## 5. Polish the prior art

- [ ] 5.1 Link every Dafka and Malamute mention: repositories, RFC 46, the Malamute whitepaper, the ZeroMQ Guide - across
      `README.md`, `docs/direction.md`, `docs/design/00-overview.md`, `04-flow-control.md`, `06-jobs.md` and the site pages.
      Verified by: `rg -n "(?<!\[)Dafka|Malamute" --pcre2` finds no bare, unlinked mention in published pages.
- [ ] 5.2 Write `docs/site/pages/guarantees.dox` in that shape - what a follower, a writer and a reader are promised - linking to
      the capability specs rather than restating them. Verified by: every claim on the page links to a requirement.
- [ ] 5.3 Write `docs/site/pages/design-at-a-glance.dox`: writer, replica, responder, the log, repair, credits, with a diagram.
      Verified by: the page is reachable from the navbar and contains no requirement text.
- [ ] 5.4 Add a "what we borrowed" section to `about.dox`: which ideas came from Dafka and Malamute, what Mosaic changed, and why
      (durable writer identity, signed chains, credits on repair, no tower). Verified by: the section names each borrowed idea and
      cites the upstream project by link, with no copied prose.

## 6. Close out

- [ ] 6.1 `./scripts/ci.sh` green, including the documentation step, from a clean build directory. Verified by: the CI run.
- [ ] 6.2 Add and close a board row for the move. Verified by: `./scripts/check_todo_ids.sh` exits 0.

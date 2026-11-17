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

## 4. Close out

- [ ] 4.1 `./scripts/ci.sh` green, including the documentation step, from a clean build directory. Verified by: the CI run.
- [ ] 4.2 Add and close a board row for the move. Verified by: `./scripts/check_todo_ids.sh` exits 0.

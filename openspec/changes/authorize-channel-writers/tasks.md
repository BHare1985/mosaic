# Tasks

## 1. Rules and enforcement

- [ ] 1.1 Add a rule set (pattern -> allowed writer ids) parsed from configuration, defaulting to open. Verified by: a rule for
      `audit.>` admits its writer and refuses another, while an unruled channel accepts both.
- [ ] 1.2 Enforce on ingest before storage and before the application callback. Verified by: a refused record leaves the log head
      unchanged and the callback uncalled.
- [ ] 1.3 Enforce on publish, so a writer cannot create history its readers would reject. Verified by: publishing as an unlisted
      identity fails and appends nothing.
- [ ] 1.4 Count rejections per pattern. Verified by: three refused records raise the counter by three.

## 2. Make it usable

- [ ] 2.1 Wire the rules into the CLI (`--allow-writer pattern=id[,id]`), echoing the effective rules at startup. Verified by: the
      flag appears in `Mosaic pub --help`-level usage and in startup output.
- [ ] 2.2 Two-process check: a closed `svc.resize` refuses a request from an unlisted identity and the job never runs. Verified by:
      the demo, run as two processes.
- [ ] 2.3 Document layer 3 as implemented in `docs/design/07-security.md`, with what it does and does not protect against.
      Verified by: the section names both, and points at replicated membership as the follow-up.

## 3. Close out

- [ ] 3.1 Definition of done: four build combinations, clang-format clean, spec row updated. Verified by: the matrix and format
      check.

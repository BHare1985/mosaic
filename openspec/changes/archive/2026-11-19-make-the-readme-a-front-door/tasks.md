# Tasks

## 1. The page

- [x] 1.1 Put the identity block at the top of `README.md` exactly as supplied - the centred `<p align="center">` / `<img>`
      block with the historical asset URL, unmodified - followed by the title and a one-line pitch. Verified by: the rendered
      page on GitHub starts with the logo, and `head -8 README.md` shows the block before any prose.
- [x] 1.2 Rewrite the body into the order in design D2 (why you would use it, who it is for, what it costs, how it fits
      together, guarantees, status, quick start, where to read next, heritage, contributing, licence), keeping the current
      build/test/run commands verbatim. Verified by: `./scripts/docs.sh` builds the site with no new warnings, and every
      section in D2 appears in order.
- [x] 1.3 Write the "why you would use it" section from design D3: each value proposition as one sentence of what the reader
      gets, paired with the mechanism that makes it true and a link to its owner. Verified by: all six propositions appear, and
      each linked spec or design doc exists.
- [x] 1.4 Write the "who it is for" section from design D4: the audience, what they arrive looking for, what they get, and the
      spec that backs it - applications named as the reader's problem, never as feature names. Verified by: every row of the D4
      table appears with a working link, and no application is listed that the design does not implement.
- [x] 1.5 Write the "what it costs" section from design D5: re-measure the numbers with the documented Release-build commands
      rather than copying the values in the design, quote each with its command and caveat (build type, sanitizer skip, one
      machine), state the structural facts as structure, and make no comparison with another product. Verified by: running the
      two commands reproduces the quoted values within noise, and every number in the section has its command next to it.
- [x] 1.6 Keep the status block honest and move it below the pitch: what works, what is open (retention with checkpoints,
      the segment store, key rotation, gossip discovery), each open item linking the work board. Verified by: every open item named and the board link present,
      and nothing listed as open is described as working elsewhere on the page.
- [x] 1.7 Add the heritage and further reading section per design D7: Dafka and RFC 46, Malamute, the ZeroMQ Guide, the
      prior-art note, GoAT (https://eprint.iacr.org/2021/697) and Filecoin proof-of-replication, "not wire-compatible" in the
      same paragraph, and the 2024 vision as one sentence plus a link to `docs/direction.md`. Verified by: each external link
      resolves (`curl -sI` status 200 or 3xx) and the compatibility sentence is in that section.
- [x] 1.8 Add badges only where a file backs them - CI, Pages, MPL-2.0, C++20 (design D6) - with no coverage or version badge.
      Verified by: each badge's target file exists and no badge points at a service the project does not feed.
- [x] 1.9 Open with the maintainer's own definition of the system - brokerless, verifiable, replicated-log library plus daemon,
      in C++20 on ZeroMQ, and what that consists of - and state "not wire-compatible" once, next to the Dafka and Malamute names.
      Verified by: the first paragraph names every component (signed hash-chained logs, pattern subscriptions, peer replication
      and repair, credit-based flow control, the optional job layer) without a reader needing another document.
- [x] 1.10 Add the "How it works" design section per design D10: four Mermaid diagrams - one record's chain, one node's three
      roles, a fleet with no broker, and a larger deployment - each followed by the mechanism it depicts. Verified by: the
      diagrams render on GitHub (mermaid blocks, not images), and a reader can trace one record from writer to a follower's
      store through them alone.
- [x] 1.11 Add the lineage table per design D11: every borrowed mechanism attributed and linked to Dafka / RFC 46, Malamute's
      whitepaper, or the ZeroMQ Guide chapter that describes it, with Mosaic's own additions as their own rows. Verified by:
      each row names a source, each link resolves, and the section says which parts are this project's own.
- [x] 1.12 Take the matcher out of "what it costs" and keep the cost section about a write: durable append measured, plus the
      structural facts. Verified by: no matcher numbers on the page, one benchmark command, and the design doc keeps the
      matcher benchmark where MATCH-1 published it.

## 2. The check

- [x] 2.1 Add `scripts/check_readme.sh`: the identity block is the first thing on the page, every relative link and image
      target exists after stripping `#anchor` (absolute URLs and bare fragments exempt), and the fenced-code count is even.
      Verified by: the script passes on the rewritten README, and fails with a named target when a relative link is changed to a
      path that does not exist.
- [x] 2.2 Wire the check into `scripts/ci.sh` next to `check_todo_ids.sh`, so a broken front page fails the same gate as the
      tests. Verified by: `./scripts/ci.sh` reports the front-page step as part of a full run.
- [x] 2.3 Remove the stray empty code fence the current README carries (the bug 2.1 exists to catch). Verified by: the fence
      check passes and the diff of the removed block shows nothing else changed.

## 3. Close out

- [x] 3.1 Update `docs/README.md` and the design set's doc map if the rewrite changes where a reader is sent, and note the
      front-page check in `docs/design/08-testing-strategy.md`'s gate list. Verified by: `./scripts/docs.sh` and
      `./scripts/ci.sh` green, and the naming matches what the scripts print.
- [ ] 3.2 Read the page as a stranger would, and have someone who does not know the project do the same. Verified by: the first
      screen answers what it is, why you would want it, who it is for and what it costs, and `./scripts/ci.sh` is green in every
      configuration.

## Workflow follow-up

- Archive this change with `openspec-archive-change` once the page is merged, so `docs/front-page` becomes the capability the
  next README edit is measured against.

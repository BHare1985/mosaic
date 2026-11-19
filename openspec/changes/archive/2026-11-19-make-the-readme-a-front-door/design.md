# Design

## Context

See `proposal.md` - Why. The constraints that shape the approach:

- `README.md` is not just the GitHub front page: `docs/site/Doxyfile` sets `USE_MDFILE_AS_MAINPAGE` on it, so it is also the
  generated reference site's landing page. Whatever replaces it must survive both renderers.
- The project's own rules apply to its prose. `docs/design/conventions.md` says behaviour is specified in `openspec/specs/` and
  explained in `docs/design/`; a front page that restates guarantees would be a second, drifting source.
- The honest status block in the current README exists because the project has open items (SEC-1b pinning, discovery, retention
  policy). A pitch page must not lose it - it moves, it does not disappear.
- The material the page needs already exists: the applications are argued in `docs/direction.md`, the borrowed/rejected split is
  in `docs/site/pages/about.dox`, the guarantees are in `docs/site/pages/guarantees.dox` and the capability specs, the cost is
  *measured* rather than claimed (`performance/measured-cost`, `test_matcher.cpp`, `test_sqlite_store.cpp`), and the prior art
  has its own note in `docs/malamute-and-dafka-system-design.md`.
- The repository has no logo asset, and the project does not want one added. The original artwork is referenced by the
  historical GitHub user-asset URL used by the 2024 front page (commit `56d3d20`, "Upload SVG logo").

## Goals / Non-Goals

**Goals**

- A first screen that answers *what is this, why would I want it, who is it for, how fast*.
- Value propositions that each name the mechanism that makes them true.
- Applications written as the reader's problem, tied to the audience and to the spec that backs them.
- Cost quoted as measurement plus structure, so "fast" means something specific.
- A front page that cannot silently break: relative links checked, the logo block pinned, code fences balanced.

**Non-Goals**

- Committing a logo file, redrawing the artwork, or theming the site around it.
- Benchmarks against Kafka, NATS, Dafka or Malamute. The repository measures itself; comparing would need someone else's numbers
  on the same machine and the same durability settings.
- Promising throughput targets. The system's claim is bounded and predictable cost, not a headline number.
- Replacing the design set, the specs, or the site pages - the README links to them.
- Changing `openspec/specs/docs/reference-site`: the generator is untouched.

## Decisions

### D1 - The logo is referenced by its original URL, not owned

Rejected: **committing the SVG** (it would put an 11.5 KB generated artwork into a repo whose only image-bearing surface is the
README, and the project asked explicitly for no download and no asset) and **m.css `PROJECT_LOGO`** (that decorates the site's
chrome, not the page, and would still need an in-repo file). The block is used verbatim:

```html
<p align="center">
  <img src="https://github.com/BHare1985/mosaic/assets/2180284/73ac71af-2309-4efd-a24a-bf8d8b021eb1" height="384" width="auto" />
</p>
```

Consequence, accepted: the logo depends on a GitHub-hosted attachment rather than on the checkout. It resolves today, and it is
the artwork the project has always shown. If it ever stops resolving, the fix is to swap `src` for a committed copy - one line.

Consequence for the checker: absolute URLs are exempt from link checking. Only relative targets are verified, which is where rot
actually happens (a renamed directory).

### D2 - The page is ordered for a reader deciding, not for a contributor reading up

A "front store" is an ordering promise, and the order that matters is the one a reader decides in:

1. centred logo, `# Mosaic`, one-line pitch - *what is this*
2. badges (CI, Pages, MPL-2.0, C++20) - see D6
3. **How it works** - the pictures (D10) and the lineage table (D11) - *what does it actually do*
4. **Why you would use it** - the value propositions (D3) - *why should I care*
5. **Who it is for** - audience plus the problem they arrive with (D4) - *is this for me*
6. **What a write costs** - measured numbers and structural facts (D5) - *is it fast enough*
7. **What it guarantees** - short statements, each linking to the doc that owns it
8. **Status** - what works, what is open, linking the board (kept from the current README)
9. **Quick start** - the verified build/test/run commands, unchanged
10. **Where to read next** - the design set, operations, the site
11. **Heritage and further reading** - prior art and papers (D7)
12. **Contributing**, licence

Sections 3-6 are the new work and must land inside the first two screens; anything longer belongs in `docs/design/`. The
old ASCII module sketch is dropped: picture 2 (the node) and the lineage table say the same thing better, and keeping both
would be two diagrams of one system drifting apart.

### D3 - Value propositions name the mechanism, or they are not stated

Each proposition is one sentence of what the reader gets, followed by the property that makes it true and a link to its owner:

| What you get | The mechanism | Owner |
|---|---|---|
| Nothing to run in the middle | the writer is the source; peers replicate and repair directly | `net/transport`, `node/node-runtime` |
| Nothing to take on trust | every record is signed and hash-chained; a stored log verifies offline | `core/record-integrity` |
| Nothing to elect | the log is single-writer, so replicating it needs no consensus | `log/replicated-log`, `docs/design/decisions.md` D3 |
| Replay from anywhere, even with the writer gone | any holder serves repair; heads are learnable from any peer | `node/node-runtime` |
| Cost that does not grow with history | per-tick work is independent of stored volume; repair is paced by credit | `performance/measured-cost`, `flow/credit-control` |
| One small stack, not a platform | C++20 library, a daemon, a CLI and a C ABI over the same core | `api/c-abi`, `cli/operations`, `daemon/mosaicd` |

Rejected alternatives: a feature list ("signed logs, patterns, credits, jobs") - accurate and useless, because it describes the
implementation rather than the decision the reader is making; and a slogan - unverifiable.

### D4 - Audiences are named with the problem they arrive with

The audience table is the applications section, seen from the reader's side: *who*, *what they are looking for*, *what they get*,
*where it is defined*. The rows come from `docs/direction.md` and the guarantees page, not from imagination:

| Who | Looking for | What they get | Owner |
|---|---|---|---|
| Teams already on ZeroMQ | a durable, replayable log without adding a broker | CURVE-secured links, PUB/SUB live path, ROUTER/DEALER repair | `net/zeromq-transport` |
| C++ systems teams | an embedded event log small enough to own | a library with no broker, no coordinator, no server to operate | `docs/design/00-overview.md` |
| Edge and industrial deployments | progress when the link is flaky | loss, duplication, delay and partitions are the tested case | `harness/simulation` |
| Multi-site platform teams | cross-site replication without a cluster | any holder serves repair; no central point in the data path | `node/node-runtime` |
| Audit, security and compliance | history that cannot be quietly rewritten | signatures, hash chains, kept fork evidence, offline `Mosaic verify` | `log/replicated-log`, `cli/operations` |
| Agent and tool builders | service calls without a coordinator | `svc.<name>` records plus rendezvous ranking, at-least-once by design | `jobs/service-calls` |

The future layers (parity symbols, storage/bandwidth tokens, proof of replication and geolocation) appear **only** under
heritage, marked optional and linked to
`docs/direction.md#future-layers-the-original-mosaic-vision-kept-for-later`.

### D5 - "Fast" is quoted, with the command, or it is structural

The rule: a number appears with the command that produced it, the build type, and the machine class; otherwise the claim is
architectural and says so. Floors measured for this design on the development machine (GCC 15, `-DCMAKE_BUILD_TYPE=Release`,
`-DMOSAIC_WITH_ZMQ=OFF`), to be re-measured rather than copied:

```bash
cmake -S test -B build/bench -DCMAKE_BUILD_TYPE=Release -DMOSAIC_WITH_ZMQ=OFF -DMOSAIC_BUILD_ZMQ_FROM_SOURCE=OFF
cmake --build build/bench -j
./build/bench/MosaicTests -tc="*append throughput*"      # durable append
```

| Measured | Release build | What it means |
|---|---|---|
| durable append, batched commit | ~44 µs per record | ~20k records/s through one writer with batching on |
| durable append, per-record commit (default) | ~639 µs per record | the price of an fsync per record, and it is the default on purpose |

Everything else is structure, and is stated as such: no broker hop on the live path, no consensus round before a write, per-tick
work that does not grow with stored volume (`performance/measured-cost`), and repair bounded by the credit window rather than by
the gap size (`flow/credit-control`).

**The matcher benchmark stays out of the page.** It answers a question about one leaf module ("does local pattern filtering
scale to a hundred thousand subscriptions?") that its own design doc and MATCH-1 already answer; putting a bitmap microbenchmark
next to "what a write costs" made the page look like it was bragging about the least interesting part. The page measures the
thing a reader is deciding about: one durable write.

Caveats the page must carry: timing gates are skipped under sanitizers, and a number from one machine is not a benchmark suite.
What the page must **not** do is compare with another product: the repository has no numbers for those under the same durability
settings, and inventing them is how a project loses the argument it was winning.

### D6 - Only badges that a file can back

`CI` (`.github/workflows/ci.yml`, name "CI"), `Pages` (`.github/workflows/pages.yml`), `MPL-2.0` (`LICENSE`), `C++20`
(`CMakeLists.txt`). No coverage badge: `codecov.yaml` exists but no workflow uploads to it, and a badge for a service the
project does not feed is a lie with a coloured background. No version badge: it would be hardcoded and drift from
`project(Mosaic VERSION ...)`, the single source of truth.

### D7 - The heritage section keeps the original vision behind a link

Dafka (and RFC 46) and Malamute are credited by contribution, with "not wire-compatible" in the same paragraph, and the longer
account is `docs/malamute-and-dafka-system-design.md`. The research the original vision cited stays where it belongs - as
reading, not as features: [GoAT: File Geolocation via Anchor Timestamping](https://eprint.iacr.org/2021/697), Filecoin's
proof-of-replication, and the ZeroMQ Guide for the messaging model. The 2024 vision (fountain-code parity, bandwidth/storage
tokens, a permissioned chain) is one sentence plus a link to `docs/direction.md`.

### D8 - The checker enforces shape, not prose

`scripts/check_readme.sh` (bash, no dependencies, same register as `scripts/check_todo_ids.sh`) asserts three things:

1. **The identity block**: the first non-empty line of `README.md` is `<p align="center">`, and the historical asset URL appears
   within the first eight lines. This is the piece a well-meaning refactor is most likely to delete.
2. **Links resolve**: every `](target)` and `<img src="...">` target that is not absolute (`http://`, `https://`, `mailto:`) and
   not a bare fragment (`#...`) exists as a path from the repository root, after stripping any `#anchor`.
3. **Fences balance**: the number of ``` fences is even - the current README has a stray empty fence, the bug this check catches.

Prose ("does the page name what the reader gets?") stays a review question: a grep proves a heading exists, not that it says
anything useful. The value-proposition, audience and performance tables above are the review checklist.

### D9 - The README is the site's mainpage, so the docs build is a gate

The rewrite keeps GitHub-flavoured Markdown that Doxygen and m.css already handle: HTML `<p>`/`<img>` blocks, headings, tables,
fenced code. Relative repository links (e.g. `docs/design/07-security.md`) stay as they are - they resolve on GitHub, and the
site pages handle their own navigation, which is the exemption `scripts/docs.sh` already documents. `./scripts/docs.sh` must
stay green: it is the check that the new mainpage still renders.

### D10 - The design is shown as Mermaid pictures, in the README

Four diagrams: one record's chain, one node's three roles, a fleet with no broker, and a larger deployment with the parts the
reader supplies. They are written as GitHub-native ```mermaid blocks rather than committed SVG: they are text, so they diff and
review like the prose around them, they render on the front page where readers actually arrive, and they need no asset - the same
reason the logo is referenced rather than copied.

Trade-off, accepted: the generated site does not run Mermaid, so it shows the diagram source in a code block. The README is
already exempt from the documentation gate (its links are repository paths), and the design set holds the same explanation in
prose; a reader on the site is one click from a class contract, not from the pitch.

Rejected: committed SVG per diagram (a binary-ish asset to maintain, and a picture nobody can diff), and an ASCII sketch (which is
what the old module block was, and it duplicated the node diagram).

### D11 - Lineage is a table, not a history paragraph

Each borrowed mechanism gets a row: the mechanism, then its source - Dafka and RFC 46 for the log's shape and message set,
Malamute's whitepaper for credit-paced flow control and subject patterns, ZeroMQ Guide chapters 2 and 5 for the sockets and for
filtering on the subscriber. Mosaic's own additions get their own rows, so the table does not read as modesty or as a claim of
invention. The long account stays in `docs/malamute-and-dafka-system-design.md` and the site's about page.

## Risks / Trade-offs

- **A pitch page oversells** → Every proposition names its mechanism and links its owner; cost is measured or structural; the
  future layers are confined to one labelled section. The "no benefit without a mechanism" requirement makes this a review
  criterion rather than taste.
- **Numbers age or travel badly** → Each is quoted with its command, build type and caveat, and the benchmark is part of the
  suite, so a regression shows up as a failing gate rather than a stale sentence.
- **The logo is a remote dependency** → One `src` swap if GitHub drops the attachment; nothing else depends on it.
- **A long README is its own failure** → The order in D2 puts detail behind links, and the quick start keeps the *verified*
  commands verbatim rather than inventing a shorter path nobody has run.
- **The link checker gives false confidence** → It checks existence, not accuracy. Said in the script's header.

## Migration Plan

Documentation only: merge the rewrite, then run `./scripts/ci.sh` (which now includes the front-page check) and
`./scripts/docs.sh`. Rollback is `git revert` on the README - no state, no format, no compatibility surface.

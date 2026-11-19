# Proposal

## Why

The README is the project's front door and currently reads like a status report written for people who already know what a
replicated log is. It opens with a definition ("a brokerless, verifiable, replicated-log library") rather than an image and a
reason to care, it never says who the library is *for*, it has no value propositions at all, and it says nothing about cost -
the one thing a systems team evaluates before anything else. The 2024 README had the logo and a pitch; the rewrite dropped both,
and the file has also carried a stray empty code fence since.

Someone arriving from a search or a link should be able to answer four questions in the first screen: *what is this*, *why would
I want it*, *is it fast enough*, and *is it for people like me*. Right now none of those are answered - the answers exist, in
`docs/direction.md`, the guarantees page and the measured-cost benchmarks, but nobody reads those first.

## What Changes

- The README opens with the project's original logo (the 2024 SVG, rendered through the historical GitHub asset URL - no asset
  file is added, downloaded or redrawn), a one-line pitch, and badges backed by files that exist.
- A **why you want it** section: the value propositions, each one sentence of what it buys the reader, paired with the design
  property that makes it true (no broker to run, nothing to trust, no consensus, replay from any holder, bounded cost, one
  binary).
- A **who it is for** section written from the reader's side: the audience, what they are looking for when they arrive, and what
  this gives them. That is where the applications live - local-first sync, edge and industrial telemetry, cross-organisation
  audit, service calls for agents and tools, broker-free multi-site replication, forensics and replay - each tied to its
  audience and to the capability spec that backs it.
- A **measured performance** section. "Fast" is never an adjective on its own: every number is one the repository produces
  (`MosaicTests -tc="*matcher cost*"` for matching at 1k/10k/100k subscriptions, the SQLite append benchmark for durable write
  cost), quoted with the command that produced it and the qualifier that matters; the structural claims (no broker hop, no
  consensus round, per-tick work independent of stored volume) are stated as the architecture they are.
- A **heritage and further reading** section: Dafka (RFC 46) and Malamute credited as designs, "not wire-compatible" stated in
  the same breath, the prior-art note linked, and the research the original vision cited (GoAT geolocation timestamping,
  Filecoin's proof-of-replication) kept behind the optional-future-layers link rather than sold as features.
- The page becomes a doorway rather than a manual: what it is, why you want it, who it is for, how fast, what it guarantees, one
  five-command quick start, the honest status line, and a curated map of where to read next.
- `scripts/check_readme.sh` verifies that every relative link and image target in the README exists, and runs inside
  `scripts/ci.sh`, so a front page cannot rot into dead links.
- The stray empty code fence in the current README is removed.

No library, protocol or daemon behaviour changes. **Not breaking:** the README is not an API.

## Capabilities

### New Capabilities

- `docs/front-page`: what the README must do as the project's front door - identity at the top, value propositions, the audience
  it addresses, measured performance rather than adjectives, prior art credited and linked, claims that trace to a spec, and
  links that are checked - so the page can be reviewed against requirements instead of taste. How the reference site is
  generated stays in `docs/reference-site`.

### Modified Capabilities

None. The README is the site's mainpage, but nothing in `docs/reference-site` says what the landing page must contain, and this
change adds no requirement to the generator.

## Impact

- `README.md` (rewritten), with the sections above.
- `scripts/check_readme.sh` (new) and one line in `scripts/ci.sh`.
- No source, header, spec-under-`openspec/specs/` or dependency changes; `docs/direction.md`,
  `docs/malamute-and-dafka-system-design.md` and the measured-cost benchmarks are linked, not edited.
- The published site picks the new README up automatically (`USE_MDFILE_AS_MAINPAGE`), so the docs build is the check that the
  page still renders.

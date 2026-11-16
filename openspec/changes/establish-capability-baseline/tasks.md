# Tasks

## 1. Verify every baseline requirement has evidence

- [ ] 1.1 Map `core/record-integrity` requirements to their tests: canonical bytes and signature-exclusion in
      `test_core.cpp`, chain link and tamper detection in `test_log.cpp`. Record the mapping in the change's final summary.
      Verified by: every requirement in that spec names at least one passing test case.
- [ ] 1.2 Map `store/log-store` requirements to `MemoryLogStore contract` in `test_log.cpp`: contiguity (NotNext), bounded
      ordered reads, empty unknown log, enumeration. Verified by: no requirement left without a case, and `cmake --build
      build/test -j && ./build/test/MosaicTests` still 22/22 green.
- [ ] 1.3 Map `log/replicated-log` requirements to `test_log.cpp` cases (accept/duplicate/gap/fork/bad signature/bad chain,
      seq-0 zero link) plus the `test_node_sim.cpp` once-per-sequence assertions. Verified by: fixed-order rule covered by a
      case that presents two faults at once; add the case if it is missing.
- [ ] 1.4 Map `match/channel-patterns` requirements to `test_matcher.cpp` (pattern validation, literal/star/tail,
      remove and id reuse, >64 subscriptions, randomised agreement, churn). Verified by: a note stating which requirement the
      randomised test guards.
- [ ] 1.5 Map `flow/credit-control` requirements to `test_flow_jobs.cpp` and the repair assertions in `test_node_sim.cpp`.
      Verified by: explicit confirmation of which requirements are **unverified** today (bounded per-peer memory, late
      response does not create credit) so they can be flagged as gaps rather than assumed.
- [ ] 1.6 Map `net/transport` requirements to the `SimTransport` behaviour used in the node scenarios. Verified by: a list of
      the contract rules that have no direct assertion yet, feeding NET-0 in the next change.
- [ ] 1.7 Map `node/node-runtime` requirements to `test_node_sim.cpp` (lossless in-order delivery, non-follower exclusion,
      late joiner, convergence under 30% loss and 10% duplication, forged rejection). Verified by: every requirement in that
      spec names a case; the clock-injection requirement is confirmed by inspection of the pump/tick signatures.

## 2. Record the negative space

- [ ] 2.1 State in each spec's Purpose-adjacent requirements what is explicitly not provided (no durability, no
      cryptographic authenticity, no wire bytes, no discovery, no cross-log ordering). Verified by: grepping the seven specs
      that each of the four exclusions appears where a reader would look for it.
- [ ] 2.2 Check the specs against `README.md` so no shipped requirement contradicts the status banner ("early scaffold").
      Verified by: `rg -n "plaintext\|unauthenticated" README.md docs/design/07-security.md`.

## 3. Reconcile the two sources of truth

- [ ] 3.1 Add one line to `docs/design/README.md` and to `docs/design/conventions.md` pointing at `openspec/specs/` as the
      normative behaviour statement, with `docs/design/` as rationale. Verified by: the sentence exists in both files and
      names no duplicated content.
- [ ] 3.2 Confirm `scripts/check_todo_ids.sh` still passes and that no task ID was copied into a spec. Verified by:
      `./scripts/check_todo_ids.sh` exits 0 and `rg -n "TODO\(" openspec/specs openspec/changes` returns nothing.
- [ ] 3.3 Run the fast loop once more and confirm the baseline change touches no code. Verified by:
      `git status --short` showing only `openspec/` and the two doc lines above, and `./build/test/MosaicTests` 22/22.

## Workflow follow-up

- After archive, `openspec/specs/` holds the seven capabilities and the next change's deltas apply against them.
- `verifiable-records-and-wire-format` and `two-machines-transport-and-durability` were written against these specs; re-read
  the affected requirements before starting either, in case a requirement changed while this baseline was being reviewed.

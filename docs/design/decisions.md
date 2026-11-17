# Decisions

_Describes 0.6.0._

Recorded 2026-11-09 by the maintainer. These are settled; change them only by adding a new dated entry.

| # | Decision | Consequence |
|---|---|---|
| **D1** | **Direction A:** a brokerless, Dafka-inspired replicated-log library plus daemon, with the "ZeroMQ still builds in 2026" CI track alongside it. | The project goal is the log (see [../direction.md](../direction.md)). Chain, tokens, proof-of-replication and parity coding are **optional layers on top**, not part of the core. |
| **D2** | **C++20** core. | A C ABI is only for language bindings (BIND-1), a thin wrapper that adds no behaviour. Plain C is not a goal. |
| **D3** | **Inspired by Dafka, with improvements. Not wire-compatible.** | We keep Dafka's vocabulary (RECORD, HEAD, FETCH, ACK ...) but own the protocol, versioned from byte 0. Improvements over Dafka already in the design: signed hash-chained records, durable writer identity (public key, not a per-run UUID), credits on the repair path, a real `min_replicas` rule (ACK-1), CURVE, no single-tower discovery. **Socket layout B** (PUB/SUB for live traffic, ROUTER/DEALER for unicast) is the working assumption because "improvements" needs identity and back-pressure; revisit only if ZMQ-1 prototyping finds a problem. |
| **D4** | **Keep everything local.** | No pushes, no PRs, no issues, nothing filed or patched upstream. Findings are logged in [../upstream-notes.md](../upstream-notes.md). Local workarounds (e.g. the malamute `distclean` rename) stay in this repo only. |
| **D5** | **Repoint the repo at the new goal** (README, AGENTS.md, docs). | Root README and AGENTS.md rewritten around the replicated-log goal. The old parity/token vision is preserved as a future-layers note in [../direction.md](../direction.md). |
| **D6** | **Keep the local build workaround** for the malamute `distclean` clash. | Stays as a fetch-time patch in `CMakeLists.txt` until the ecosystem track decides otherwise. |

## Defaults taken where the maintainer did not rule (change by adding an entry)
- **Matching** is on the channel name only (no header/field matching). Revisit when a use-case needs it.
- **ZeroMQ is a hard dependency of the product.** Only the core library is ZeroMQ-free, so it can be tested with `SimNetwork`.
  The CMake option `MOSAIC_BUILD_ZMQ_FROM_SOURCE` controls only whether libzmq/czmq/malamute are *compiled from source* in our build,
  which is slow. See ZMQ-0 in [TODO.md](TODO.md).

## Defaults taken where the maintainer did not rule (change by adding an entry)
- **Matching** is on the channel name only (no header/field matching). Revisit when a use-case needs it.
- **ZeroMQ is a hard dependency of the product.** Only the core library is ZeroMQ-free, so it can be tested with `SimNetwork`.
  The CMake option `MOSAIC_BUILD_ZMQ_FROM_SOURCE` controls only whether libzmq/czmq are *compiled from source* in our build,
  which is slow. See ZMQ-0 in [TODO.md](TODO.md).

## 2026-11-17 — the documentation site is published (D4 is not about hosting)
Recorded because it looks like an exception to D4 and is not one. **D4** keeps *contributions upstream* local: no pushes, PRs or
issues to libzmq, czmq, malamute, dafka or this repo's own history rewritten. Publishing **our own** reference documentation to
GitHub Pages is nothing of the sort: it is a build artifact from `scripts/docs.sh` deployed by a workflow, with no branch to
maintain and no upstream repository involved.

Consequence: the site is exactly what the last workflow run produced, so it cannot hold a stale committed copy; and the only
repository setting it needs is *Settings → Pages → Source: GitHub Actions*.

## 2026-11-17 — the version number counts milestones
The project reported `0.1.0` from its template through nineteen capability specs, a transport on a real network, durability,
resilience, jobs and a daemon: a number that told a reader nothing. **The rule is now `0.<closed milestones>.0`**, so six closed
milestones (M0 scaffold through M5 jobs) make this **0.6.0**, and closing M6 makes 0.7.0. Patches carry fixes only.

Consequences: CMake's `project(VERSION ...)` is the only place the number is written; the generated header carries it as
`inline constexpr` values plus `kMilestonesClosed` (so the rule is visible in code); a test fails if the header and CMake disagree;
and each document in this set states the version it describes.

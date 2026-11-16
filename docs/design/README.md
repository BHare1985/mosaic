# Mosaic design set

> **Behaviour is specified in [`openspec/specs/`](../../openspec/specs/); this set is the rationale.** The docs below explain
> *why* and *how it fits together*; when a capability spec and a doc disagree, the spec is normative and the doc is corrected
> in the same change. Work items stay on the [TODO board](TODO.md); task IDs are not copied into specs.

Read in order. Each doc says what is **implemented** (✅), **stubbed** (🟡) or **designed only** (⬜).

| # | Doc | Covers |
|---|---|---|
| 00 | [overview](00-overview.md) | Goals, architecture, module map, dependency rules, one record's journey |
| – | [decisions](decisions.md) | Settled project decisions D1–D6 (read first) |
| – | [conventions](conventions.md) | Code style, errors, time, threading, how to add things |
| 01 | [data model](01-data-model.md) | Record, hash chain, signatures, forks, store contract, retention |
| 02 | [protocol](02-protocol.md) | Messages, state machines, gap repair, versioning, wire encoding |
| 03 | [matching & channels](03-matching-and-channels.md) | Pattern language, bitmap matcher, mapping to ZeroMQ subscriptions |
| 04 | [flow control](04-flow-control.md) | Credits, drop-don't-block, slow followers |
| 05 | [node & transport](05-node-and-transport.md) | Node roles, Transport contract, SimNetwork, ZeroMQ mapping, discovery |
| 06 | [jobs](06-jobs.md) | Calling services over logs, rendezvous ranking, retries |
| 07 | [security](07-security.md) | Identity, signatures, CURVE, threat model |
| 08 | [testing strategy](08-testing-strategy.md) | Test layers, conformance suites, definition of done |
| – | **[TODO](TODO.md)** | **Task board for junior developers, with IDs referenced from the code** |

Background and decisions: [../direction.md](../direction.md), [../decentralized-broker-2026.md](../decentralized-broker-2026.md),
[../malamute-and-dafka-system-design.md](../malamute-and-dafka-system-design.md).

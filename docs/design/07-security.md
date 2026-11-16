# 07 — Security (⬜ placeholders only; read before using in anger)

> **`InsecureTestSigner` offers no security.** It exists so the rest of the system can be built and tested. Until CORE-2
> lands, nothing here is safe for real data.

## Layers
1. **Record signatures** (CORE-2): Ed25519 via libsodium; `WriterId` = hex public key; sign `record_hash`. Independent of
   transport, so a record stays verifiable after being stored and forwarded by untrusted nodes.
2. **Transport security** (SEC-1): ZeroMQ CURVE (libsodium) + ZAP allow-list of peer public keys. Protects confidentiality
   and peer authentication on links.
3. **Authorization** (later): which writer may write which channel; which peer may follow which pattern.

## Threat model (first draft; owner: maintainer to review)
| Actor | Can | Cannot / mitigation |
|---|---|---|
| Malicious replica | withhold or delay records, serve stale heads | forge records (signatures); other replicas serve the data |
| Malicious writer | equivocate, spam | hide equivocation from replicas that compare (fork detection); rate limits (SEC-2) |
| Malicious follower | flood FETCH | bounded by `max_serve`, credits, peer scoring (SEC-2) |
| Network attacker | drop/duplicate/reorder, observe | protocol tolerates loss; CURVE hides content |
| Resource exhaustion | huge records, huge `count`, many logs | max sizes in decoder (ENC-1), `max_serve`, per-peer budgets |

## Rules for code
- Never act on unverified data. Never log payloads. Treat every decoded field as hostile (bounds, sizes).
- Keys never in config files or logs; keystore API (CORE-2). Rotation = new log + link record (SEC-3).

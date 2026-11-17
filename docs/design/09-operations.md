# 09 — Running it

The daemon is `mosaicd`. It reads a ZPL config, opens its store and transport, follows what it was told to follow, and runs
until `SIGINT`/`SIGTERM`. Contract: `openspec/specs/daemon/mosaicd`.

```bash
mosaicd --defaults            # a commented config generated from the daemon's own defaults
mosaicd --check my.zpl        # validate; every problem reported at once; no socket opened
mosaicd my.zpl                # run
mosaicd --no-curve my.zpl     # diagnostic: plaintext links
```

Two things about the format are worth knowing before writing a file:

- **Values containing `#` must be quoted.** A Z85 CURVE key is 40 characters and often contains `#`, which ZPL reads as the
  start of a comment. `curve = "Wu{9<…)#.=j"`. A key that ends up the wrong length is a startup error naming `peer.curve`, so a
  truncated key cannot be deployed silently.
- **`${NAME}` is expanded from the environment**, and an unset variable is an error naming it - keys and paths are exactly what
  does not belong in a repository.

## Reading the daemon

One status line per interval, and `kill -USR1 <pid>` for one on demand:

```
status accepted=… duplicates=… gaps=… forks=… repair_sent=… retained=… shortfall=… throttled=… handshake_rejects=… unauthorized=…
```

| Symptom | Read this | It means |
|---|---|---|
| Nothing arrives at all | `accepted=0`, `repair_sent=0` | not connected: check `peer` entries, keys, and that the peer is running |
| Handshakes refused | `handshake_rejects` rising | a peer announced an unsupported protocol version, or its key is not allowed |
| Fetching but storing nothing | `repair_sent` rising, `accepted=0` | the peer cannot serve the range, or requests are being throttled |
| Repeated repair | `repair_sent` high, `gaps` high | a slow follower: check `slow_follower_threshold` and the link |
| Records retained forever | `retained` grows, `shortfall=0` | `min_replicas` is set and replicas are not acknowledging |
| Loss disguised as success | `shortfall` > 0 | the retention limit was hit without meeting `min_replicas`: durability is unmet |
| Some writers ignored | `unauthorized` rising | a channel rule refuses that writer: check `allow` |

`duplicates` and `gaps` rising together early in a node's life is normal - it is backfilling history. `forks` should be zero; a
non-zero value is evidence of an equivocating writer and is kept (`Node::forks()`).

## What the daemon does not do

No hot reload (restart to change configuration), no discovery beyond the configured peer list, no metrics endpoint. It is
single-threaded: one loop, time read once per iteration, 5 ms pacing - so a quiet daemon is cheap and a busy one is predictable.

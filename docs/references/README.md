# References

Local, read-only copies of the upstream material this project builds on.
They are not committed; run `./fetch.sh` to (re)download them.

| Dir | Upstream | Snapshot used when written | Read first |
|---|---|---|---|
| `zguide/` | https://github.com/booksbyus/zguide (the ZeroMQ Guide) | 2025-05-19 `6752d24` | `chapter4.txt` (reliability), `chapter5.txt` (pub-sub, Clone pattern), `chapter7.txt` (unprotocols, state machines), `chapter8.txt` (Zyre: discovery, testing) |
| `dafka/` | https://github.com/zeromq/dafka | 2020-09-11 `c2dc88d` (no commits since) | `README.md`, `src/dafka_proto.xml`, `src/dafka_store.c`, `src/dafka_consumer.c`; spec: https://rfc.zeromq.org/spec/46 |
| `malamute/` | https://github.com/zeromq/malamute | 2025-06-25 `2c5ec20` | `MALAMUTE.md`, `STREAM.md`, `src/mlm_proto.xml`, `src/mlm_server.c` |

Related upstreams we build on: libzmq (v4.3.5), czmq (v4.2.1), cppzmq.

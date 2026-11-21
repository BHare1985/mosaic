"""Publish a few records, tail them back, print what arrived.

Run it twice in two shells to see replication between processes:

    python demo.py tail --port 6340 --peer-port 6350     # shell 1
    python demo.py pub --port 6350 --peer-port 6340      # shell 2
"""

import argparse
import os
import sys
import time
import uuid

import mosaic


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("role", choices=["pub", "tail"])
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--peer-port", type=int, required=True)
    parser.add_argument("--channel", default="events.demo")
    parser.add_argument("--count", type=int, default=5)
    parser.add_argument("--keys", default=None, help="keystore path (generated if absent)")
    parser.add_argument(
        "--plaintext",
        action="store_true",
        help="diagnostic: run the link unencrypted (never for real data)",
    )
    parser.add_argument(
        "--peer-curve",
        default=None,
        help="the peer's z85 curve key: links are encrypted, so a peer cannot be dialled without it",
    )
    options = parser.parse_args()

    keys = options.keys or f"demo-{options.role}.key"
    store = f"demo-{options.role}.db"
    if not os.path.exists(keys):
        mosaic.generate_keys(keys)
    print("my curve key:", mosaic.curve_public(keys), flush=True)

    peer = {
        # The peer's *node id*, not a label: repair is addressed by identity, so a mismatched id
        # means FETCH requests go nowhere and a gap is never filled.
        "id": "pub" if options.role == "tail" else "tail",
        "pub": f"tcp://127.0.0.1:{options.peer_port}",
        "router": f"tcp://127.0.0.1:{options.peer_port + 1}",
    }
    if options.peer_curve:
        peer["curve"] = options.peer_curve
    with mosaic.Node(
        node_id=options.role,
        keys_path=keys,
        store_path=store,
        pub_bind=f"tcp://127.0.0.1:{options.port}",
        router_bind=f"tcp://127.0.0.1:{options.port + 1}",
        peers=[peer],
        plaintext=options.plaintext,
    ) as node:
        print("mosaic", mosaic.version(), "listening on", node.endpoints()[0], flush=True)
        node.follow("events.>")

        if options.role == "pub":
            # Long enough for a CURVE handshake and the subscription to cross the link: publishing
            # before the subscriber is really joined loses the first records, and the tail then
            # repairs them - which takes longer than this demo runs.
            time.sleep(2.5)
            for i in range(options.count):
                seq = node.publish(options.channel, f"hello #{i} {uuid.uuid4().hex[:6]}".encode())
                print("published seq", seq, flush=True)
                node.pump()
                time.sleep(0.2)
            time.sleep(2.0)  # answer the repair a missed record provokes
        else:
            seen = 0
            for record in node.records(timeout_ms=4000):
                print(f"received seq={record['seq']} channel={record['channel']} payload={record['payload']!r}", flush=True)
                seen += 1
                if seen >= options.count:
                    break
            print("received", seen, "record(s)", flush=True)
            return 0 if seen > 0 else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

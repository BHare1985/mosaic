"""Publish a few records, tail them back, print what arrived.

Run it twice in two shells to see replication between processes:

    python demo.py tail --port 6340 --peer-port 6350     # shell 1
    python demo.py pub --port 6350 --peer-port 6340      # shell 2
"""

import argparse
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
    options = parser.parse_args()

    keys = f"demo-{options.role}.key"
    store = f"demo-{options.role}.db"
    mosaic.generate_keys(keys)

    peer = {
        "id": "peer",
        "pub": f"tcp://127.0.0.1:{options.peer_port}",
        "router": f"tcp://127.0.0.1:{options.peer_port + 1}",
    }
    with mosaic.Node(
        node_id=options.role,
        keys_path=keys,
        store_path=store,
        pub_bind=f"tcp://127.0.0.1:{options.port}",
        router_bind=f"tcp://127.0.0.1:{options.port + 1}",
        peers=[peer],
    ) as node:
        print("mosaic", mosaic.version(), "listening on", node.endpoints()[0], flush=True)
        node.follow("events.>")

        if options.role == "pub":
            time.sleep(1.0)  # let the tail subscribe
            for i in range(options.count):
                seq = node.publish(options.channel, f"hello #{i} {uuid.uuid4().hex[:6]}".encode())
                print("published seq", seq, flush=True)
                node.pump()
                time.sleep(0.2)
            time.sleep(0.5)
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

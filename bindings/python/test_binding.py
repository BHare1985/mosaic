"""The binding, tested the way it is used: one process tails, another publishes.

Run with `python -m pytest test_binding.py` or plain `python test_binding.py`.
"""

import os
import subprocess
import sys
import tempfile
import time

import mosaic

TAIL_PORT = 6340
PUB_PORT = 6350


def run_tail(workdir, channel, key, peer_curve):
    """Tail in this process until something arrives; return the records."""
    store = os.path.join(workdir, "tail.db")
    peer = {
        "id": "pub",
        "pub": f"tcp://127.0.0.1:{PUB_PORT}",
        "router": f"tcp://127.0.0.1:{PUB_PORT + 1}",
        "curve": peer_curve,  # links are encrypted, so the peer has to be authenticated
    }
    with mosaic.Node(
        node_id="tail",
        keys_path=key,
        store_path=store,
        pub_bind=f"tcp://127.0.0.1:{TAIL_PORT}",
        router_bind=f"tcp://127.0.0.1:{TAIL_PORT + 1}",
        peers=[peer],
    ) as node:
        node.follow(channel)
        received = []
        for record in node.records(timeout_ms=15000):
            received.append(record)
            if len(received) >= 3:
                break
        return received


def publish_from_another_process(workdir, channel, keys, peer_curve):
    """A second process, publishing through the same binding."""
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "demo.py")
    env = dict(os.environ)
    return subprocess.Popen(
        [
            sys.executable,
            script,
            "pub",
            "--port",
            str(PUB_PORT),
            "--peer-port",
            str(TAIL_PORT),
            "--channel",
            channel,
            "--count",
            "3",
            "--keys",
            keys,
            "--peer-curve",
            peer_curve,
        ],
        cwd=workdir,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def ping(workdir, channel="events.test"):
    with tempfile.TemporaryDirectory() as temp:
        # Both keystores exist before either process starts, so each side can be told the other's
        # curve key - which is what makes the link encrypted rather than merely offered encryption.
        tail_key = os.path.join(temp, "tail.key")
        pub_key = os.path.join(temp, "pub.key")
        mosaic.generate_keys(tail_key)
        mosaic.generate_keys(pub_key)
        tail_curve = mosaic.curve_public(tail_key)
        pub_curve = mosaic.curve_public(pub_key)
        assert len(tail_curve) == 40 and len(pub_curve) == 40, (tail_curve, pub_curve)

        publisher = publish_from_another_process(temp, channel, pub_key, tail_curve)
        try:
            time.sleep(0.5)
            records = run_tail(temp, channel, tail_key, pub_curve)
        finally:
            output = publisher.communicate(timeout=30)[0]
        assert publisher.returncode == 0, f"publisher failed:\n{output}"
        assert len(records) >= 3, f"expected three records, got {records}"
        assert [record["seq"] for record in records[:3]] == [0, 1, 2], records
        assert all(record["channel"] == channel for record in records)
        assert records[0]["payload"].startswith(b"hello #0"), records[0]
        print("binding ok:", records[0])


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as workdir:
        ping(workdir)

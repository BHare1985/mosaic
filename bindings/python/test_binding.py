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


def run_tail(workdir, channel):
    """Tail in this process until something arrives; return the records."""
    key = os.path.join(workdir, "tail.key")
    store = os.path.join(workdir, "tail.db")
    mosaic.generate_keys(key)
    peer = {"id": "pub", "pub": f"tcp://127.0.0.1:{PUB_PORT}", "router": f"tcp://127.0.0.1:{PUB_PORT + 1}"}
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
        for record in node.records(timeout_ms=6000):
            received.append(record)
            if len(received) >= 3:
                break
        return received


def publish_from_another_process(workdir, channel):
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
        ],
        cwd=workdir,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def ping(workdir, channel="events.test"):
    with tempfile.TemporaryDirectory() as temp:
        publisher = publish_from_another_process(temp, channel)
        try:
            time.sleep(1.0)
            records = run_tail(workdir, channel)
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

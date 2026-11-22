"""Mosaic from Python, over the library's C ABI.

The compiled part (`mosaic._mosaic`) is a thin adapter; this module adds the ergonomics Python callers expect: a record
iterator that pumps for you, and context managers.

    import mosaic, time

    mosaic.generate_keys("writer.key")
    with mosaic.Node(node_id="python", keys_path="writer.key", store_path="python.db") as node:
        node.follow("events.>")
        node.publish("events.demo", b"hello", int(time.time() * 1000))
        for record in node.records(timeout_ms=2000):
            print(record["seq"], record["payload"])
"""

import time

from ._mosaic import Node as _Node
from ._mosaic import curve_public, generate_keys, version

__all__ = ["Node", "generate_keys", "version"]


class Node:
    """A node with a record iterator on top of the raw `drain`/`pump` pair.

    Nothing here holds state the C layer does not: `records()` is a pump-and-drain loop, which is why the timeout is
    the caller's own clock rather than a timer inside the library.
    """

    def __init__(self, **kwargs):
        self._node = _Node(**kwargs)

    def follow(self, pattern):
        self._node.follow(pattern)
        return self

    def publish(self, channel, payload, now_ms=None):
        if isinstance(payload, str):
            payload = payload.encode()
        return self._node.publish(channel, payload, int(now_ms if now_ms is not None else time.time() * 1000))

    def pump(self, now_ms=None):
        self._node.pump(int(now_ms if now_ms is not None else time.time() * 1000))

    def drain(self):
        return self._node.drain()

    def endpoints(self):
        return self._node.endpoints()

    def records(self, timeout_ms=1000, poll_ms=20):
        """Yield records as they arrive, until `timeout_ms` passes with nothing new."""
        deadline = time.monotonic() + timeout_ms / 1000.0
        while time.monotonic() < deadline:
            self.pump()
            batch = self.drain()
            if batch:
                for record in batch:
                    yield record
                deadline = time.monotonic() + timeout_ms / 1000.0
            else:
                time.sleep(poll_ms / 1000.0)

    def close(self):
        self._node.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc_info):
        self.close()
        return False

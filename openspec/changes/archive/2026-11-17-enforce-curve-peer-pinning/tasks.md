# Tasks

## Status: complete

The first attempt was reverted on a wrong conclusion (a malformed reply looked like a broken servicing model). The second found
the cause - a reply of six frames, not seven - and all tasks are done. See design.md, "Implementation findings".

## 1. ZAP handler

- [x] 1.1 Bind an inproc ZAP socket in the transport's own context when CURVE is enabled. Verified by: with CURVE on the
      transport binds `inproc://zeromq.zap.01` and answers handshakes; with CURVE off it creates no handler.
- [x] 1.2 Serve ZAP requests from `poll()` before ordinary traffic, replying allow/deny with the peer's key as User-Id. Verified
      by: the CURVE conformance suite and the impersonation test both pass with handshakes served from `poll()`/`service_handshakes`,
      with no extra thread - and a libzmq-only repro delivers in both thread and in-loop modes.
- [x] 1.3 Route the decision through `PeerPins::observe` keyed by the peer identity from the ZAP request. Verified by: unit
      coverage already exists for the policy; the handler adds no second implementation.

## 2. Enforcement and evidence

- [x] 2.1 Refuse encrypted messages when the handshake could not establish a peer key, instead of counting them as
      unauthenticated. Verified by: `curve_unauthenticated` no longer appears in a passing CURVE run.
- [x] 2.2 Restore the impersonation test withdrawn in the transport change: a peer claiming an identity it does not hold is
      refused at handshake, while the genuine holder of that identity is accepted. Verified by: the test passes over loopback.
- [x] 2.3 Confirm closed mode refuses an unlisted peer end to end and that the refusal is counted. Verified by: a loopback test
      with a pre-pinned list.

## 3. Close out

- [x] 3.1 Update `docs/design/07-security.md` and the board: SEC-1b done, SEC-1 fully done, and remove the "pinned only where
      observable" caveat. Verified by: `./scripts/check_todo_ids.sh` and a grep showing no stale caveat.
- [x] 3.2 Definition of done: all four build combinations green, snapshot clean, `clang-format` clean. Verified by: the matrix
      run and the format check.

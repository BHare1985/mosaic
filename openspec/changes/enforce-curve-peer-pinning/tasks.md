# Tasks

## Status: paused after the first attempt

The handler was implemented, run, and reverted (see design.md, "Implementation findings"): requests were answered `200` but no
traffic flowed, while disabling the handler restored delivery. Nothing below is complete, and the code is back to the state
this change started from. The valuable output so far is the reply/request wire contract and the disproven servicing model.

## 1. ZAP handler

- [ ] 1.1 Bind an inproc ZAP socket in the transport's own context when CURVE is enabled. Verified by: a transport constructed
      with CURVE reports that its handler is listening, and one without CURVE creates no handler.
      *Attempted; binding works and requests arrive (7 frames, mechanism CURVE, 32-byte key). Reverted with the rest.*
- [ ] 1.2 Serve ZAP requests from `poll()` before ordinary traffic, replying allow/deny with the peer's key as User-Id. Verified
      by: a connection completes while `poll()` is being called by the normal pump loop, with no extra thread.
      *Attempted and failed: `200` replies were sent successfully and still no traffic flowed. This task's mechanism is the open
      question, not its goal.*
- [ ] 1.3 Route the decision through `PeerPins::observe` keyed by the peer identity from the ZAP request. Verified by: unit
      coverage already exists for the policy; the handler adds no second implementation.

## 2. Enforcement and evidence

- [ ] 2.1 Refuse encrypted messages when the handshake could not establish a peer key, instead of counting them as
      unauthenticated. Verified by: `curve_unauthenticated` no longer appears in a passing CURVE run.
- [ ] 2.2 Restore the impersonation test withdrawn in the transport change: a peer claiming an identity it does not hold is
      refused at handshake, while the genuine holder of that identity is accepted. Verified by: the test passes over loopback.
- [ ] 2.3 Confirm closed mode refuses an unlisted peer end to end and that the refusal is counted. Verified by: a loopback test
      with a pre-pinned list.

## 3. Close out

- [ ] 3.1 Update `docs/design/07-security.md` and the board: SEC-1b done, SEC-1 fully done, and remove the "pinned only where
      observable" caveat. Verified by: `./scripts/check_todo_ids.sh` and a grep showing no stale caveat.
- [ ] 3.2 Definition of done: all four build combinations green, snapshot clean, `clang-format` clean. Verified by: the matrix
      run and the format check.

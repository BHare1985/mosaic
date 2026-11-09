# match — channel pattern matching

**Purpose:** `Matcher` answers "which subscriptions match this channel?" in time that does not depend on how many
subscriptions *don't* match (inverted bitmap index). Used by `Node` (what do I follow?) and `SimNetwork` (who gets
this publish?), and later by the ZeroMQ transport.

**Files:** `matcher.h`. Pattern language is documented in the header and in
[03-matching-and-channels](../../../docs/design/03-matching-and-channels.md).

**Rules**
- No dependencies at all (not even core).
- Any change must keep `test_matcher.cpp`'s randomized comparison against the naive matcher green.

**Open work:** MATCH-1 (benchmark), MATCH-2, MATCH-3.

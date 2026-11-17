# flow — credit-based flow control primitives

**Purpose:** `CreditWindow` (sender spends credit) and `CreditGranter` (receiver hands credit out in batches).
Pure arithmetic, no I/O. Policy lives in 04-flow-control (`docs/design/04-flow-control.md`).

**Rules**
- The receiver decides how much it can take. The sender never sends without credit. When there is no credit, *wait or
  drop; never block the whole node*.
- No dependencies.

**Where it is used today:** `Node` keeps a `CreditWindow` per peer for FETCH requests, with the credit owed tracked on
each outstanding request so a late response cannot create credit (FLOW-2, done). The live path and jobs are open work:
FLOW-3, FLOW-4, JOBS-4.

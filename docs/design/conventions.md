# Conventions

- **Language:** C++20, GCC and Clang. Warnings are errors on our targets (`-Wall -Wextra -Wpedantic -Werror`).
- **Style:** two configs, both at the repository root, both described here so nobody has to read YAML to know the rule.
  - `.clang-format` (LLVM base): 4 spaces, no tabs, 100 columns, attached braces, left-aligned pointers, no namespace
    indentation, include blocks regrouped with this project's headers (`<mosaic/...>`) first. Run `clang-format -i` on changed
    files; `./scripts/ci.sh` fails if anything is unformatted.
  - `.clang-tidy`: `bugprone`, `clang-analyzer`, `cppcoreguidelines`, `modernize`, `performance` and `readability`, with the
    naming table below enforced per construct. Excluded checks are listed in the file with the reason next to each.

### Naming

| Construct | Convention | Example |
|---|---|---|
| Files | `snake_case` | `sim_network.h`, `sqlite_log_store.cpp` |
| Namespaces | `snake_case` | `mosaic::daemon` |
| Types, concepts, aliases | `PascalCase` | `LogStore`, `IngestResult` |
| Functions and methods | `snake_case` | `request_fetch()`, `permits()` |
| Variables and parameters | `snake_case` | `fetch_batch`, `peer_id` |
| Private / protected members | `snake_case_` | `store_`, `peer_credits_` |
| Public struct fields | `snake_case` | `config.head_interval_ms` |
| Constants (`constexpr`) | `kPascalCase` | `kProtocolVersion`, `kMaxFrameBytes` |
| Scoped enum values | `PascalCase` | `IngestResult::BadSignature` |
| Macros | `UPPER_SNAKE_CASE` | `MOSAIC_VERSION` |

### Semantics

- **Ownership:** no owning raw `new`/`delete`. Values by default; `std::unique_ptr` for exclusive heap ownership;
  `std::shared_ptr` only for genuine sharing; `T*`, `T&`, `std::string_view`, `std::span` for non-owning views.
- **Rule of Zero:** rely on RAII members. If one of the five special members is declared, declare or `= delete` all of them.
- **Type safety:** `nullptr` never `0`; `enum class` never unscoped `enum`; `explicit` on single-argument constructors; `override`
  and no redundant `virtual`; `[[nodiscard]]` where ignoring a result is a bug.
- **`const` and `auto`:** prefer `const`/`constexpr`; use `auto` where the type is obvious from the right-hand side or verbose
  (iterators, lambdas), not to hide a type the reader needs.

### `std::`: no namespace imports, targeted using-declarations in `.cpp` only

- `using namespace ...` is banned everywhere (`google-build-using-namespace`). It drags in hundreds of short names -
  `count`, `distance`, `min`, `max`, `byte`, `move` - that collide and then reappear later, on a library upgrade, in code nobody
  edited.
- A `.cpp` file may import the specific names it uses, which is where most of the typing relief comes from:

```cpp
#include <mosaic/node/node.h>

#include <algorithm>

using std::max;
using std::min;
using std::string;
```

- **Headers never carry using-declarations** (`google-global-names-in-headers`): one at namespace scope would import a name into
  `mosaic` for every includer.
- Names that collide with ours stay qualified. `std::to_string` is the standing example: `mosaic::to_string(IngestResult)` wins
  unqualified lookup in an enclosing namespace, so an imported `to_string` silently resolves to the wrong overload.
- **Namespaces/paths:** `namespace mosaic`; headers `#include <mosaic/<module>/<file>.h>`; sources in `source/<module>/`.
- **Errors:** no exceptions in `core`..`node`. Use `std::optional`, result enums (`IngestResult`, `AppendResult`) or
  `bool`. Programmer errors → `assert`. Network input is never a programmer error: it is always checked.
- **Time:** never call a clock. Take `std::uint64_t now_ms` as a parameter. (Why: deterministic tests.)
- **Randomness:** never use global RNGs. Inject a seeded generator (see `SimNetwork`).
- **Threads:** everything is single-threaded unless a header says otherwise. No locks, no globals, no singletons.
- **Ownership:** components take references to collaborators and **do not own them**; the caller guarantees lifetime
  (documented per class). Prefer value types for messages.
- **Interfaces:** pure virtual classes in headers with a doc comment that states the *contract*, not just signatures.
  Each interface has (or must get) a conformance test usable by every implementation.
- **Normative behaviour:** capability specs in `openspec/specs/` state what the system must do; this doc set and the
  module READMEs explain the reasoning. Change behaviour through an OpenSpec change (`openspec/changes/`), not by editing
  a doc. Work items stay in [TODO.md](TODO.md), and a `TODO(ID)` comment cites that board, never a spec requirement.
- **Where a dependency may live:** `libzmq` only in the ZeroMQ transport, `czmq` only in `MosaicDaemon` and the `mosaicd`
  binary, `libsodium` only behind the crypto seams. Two build configurations (`core`, `no-crypto`) must keep working with none of
  them, which is the check that keeps this honest rather than aspirational.
- **Dependencies:** `core`..`node` use only the standard library. Third-party code needs maintainer approval and
  must sit behind an interface (e.g. libsodium behind `Hasher`/`Signer`).
- **Comments:** explain *why* and cite design docs/TODO ids (`// TODO(FLOW-2): ...`). Every TODO id must exist in
  [TODO.md](TODO.md).
- **Warnings are checked in both build configurations.** The fast loop and the `all/` build compile the same sources with
  different optimisation, and GCC's diagnostics differ too: an aggregate holding a `std::variant` that gets moved as a whole
  trips a `-Werror=maybe-uninitialized` false positive at `-O2` but not at `-O0` (hit in `SimNetwork::Pending`, fixed by
  holding the message directly). A clean fast loop is not evidence that the tree is warning-free.
- **Records built by hand must stamp `hash_algo`.** `LogWriter` copies it from its hasher, but a `Record` literal defaults to
  the build's default algorithm; sign and verify then disagree in a way that looks like a forgery. Prefer `LogWriter` or a
  helper, and name the algorithm explicitly in tests.
- **Adding a message / module / transport:** follow the checklist in the module README and the definition of done in
  [08-testing-strategy](08-testing-strategy.md).
- **Upstream (libzmq/czmq/malamute/dafka):** never patch or file anything without maintainer sign-off; record
  findings in [../upstream-notes.md](../upstream-notes.md).

# Conventions

- **Language:** C++20, GCC and Clang. Warnings are errors on our targets (`-Wall -Wextra -Wpedantic -Werror`).
- **Style:** `.clang-format` (WebKit base), 4 spaces, no tabs. Run `clang-format -i` on changed files.
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
- **Dependencies:** `core`..`node` use only the standard library. Third-party code needs maintainer approval and
  must sit behind an interface (e.g. libsodium behind `Hasher`/`Signer`).
- **Comments:** explain *why* and cite design docs/TODO ids (`// TODO(FLOW-2): ...`). Every TODO id must exist in
  [TODO.md](TODO.md).
- **Adding a message / module / transport:** follow the checklist in the module README and the definition of done in
  [08-testing-strategy](08-testing-strategy.md).
- **Upstream (libzmq/czmq/malamute/dafka):** never patch or file anything without maintainer sign-off; record
  findings in [../upstream-notes.md](../upstream-notes.md).

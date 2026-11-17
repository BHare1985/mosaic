# docs — where each document goes

| Folder / file | What belongs there | Rendered into the site? |
|---|---|---|
| `design/` | Rationale: the overview, the data model, the protocol, matching, flow control, node and transport, jobs, security, testing, operations, the decisions log, and the TODO board | no |
| `site/` | The documentation generator: `Doxyfile`, the m.css config, the hand-written pages, the CMake target | that *is* the generator |
| `references/` | Fetched upstream copies (Dafka, Malamute, Zyre, the ZeroMQ Guide), git-ignored | no |
| `direction.md` | Why the project exists and what it is not | no |
| `upstream-notes.md` | Findings about the surrounding ZeroMQ ecosystem | no |
| `decentralized-broker-2026.md`, `malamute-and-dafka-system-design.md` | Background reading, prior-art notes | no |

Two rules, so the split stays honest:

- **Behaviour is specified in `openspec/specs/`** — one capability per file. This folder explains and records; it does not define
  what the system must do.
- **Only the public surface is published** — the root README, the public headers, the module READMEs and `site/pages/`. If you want
  something on the site, it goes in one of those places; if it addresses contributors, it goes in `design/`.

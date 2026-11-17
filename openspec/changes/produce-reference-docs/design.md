# Design

## Context

See proposal.md. What exists already: `documentation/CMakeLists.txt` fetches m.css by pinned commit via CPM, configures
`Doxyfile` and `conf.py`, and defines a `GenerateDocs` target; `Doxyfile` has `INPUT = README.md + include/ +
documentation/pages`, `RECURSIVE = YES`, XML output with m.css rendering it; `documentation/pages/about.dox` was rewritten for
this project. What is missing: `doxygen` is not installed anywhere, no CI job builds the target, and nothing checks the result.

## Goals / Non-Goals

**Goals:** a site that builds from one command; contract comments that cannot rot; guides that link instead of restating.

**Non-Goals:** publishing (D4 keeps output in `build/`), versioned docs, a toolchain for writing prose, or documenting internals
that are not in `include/`.

## Decisions

**D1 - doxygen is a stated prerequisite, not a hidden one.**
It goes in the README's requirements beside the compiler, gets installed by the CI job, and its absence is a clear skip with a
warning in `scripts/docs.sh` rather than a silent no-op. *Alternative:* render Markdown only - rejected: the contract comments are
the most valuable unrepeated prose in the tree, and only a C++ parser pulls them out.

**D2 - `WARN_AS_ERROR = YES` for public headers.**
An undocumented public symbol fails the build. This is the same stance the code takes with `-Werror`, applied to prose: a header
without a contract comment is a half-finished change, and the failure names the symbol.

**D3 - Module READMEs are inputs, not duplicates.**
One page per module, generated from the README that already exists next to its headers. The alternative - hand-written module
pages in `documentation/pages` - would immediately become a second copy that drifts.

**D4 - Guides link to `openspec/specs/` for behaviour.**
The site states that the specs are normative and the design set is rationale, and links. Nothing on the site restates a
requirement; a restated requirement is a requirement with two versions.

**D5 - One script, wrapped by CI.**
`scripts/docs.sh` in the same shape as `scripts/ci.sh`: it configures, builds, and reports. So the claim "the docs build" is
produced by a command a reader can run, not by a workflow file only CI can see.

## Risks / Trade-offs

- **Doxygen output is verbose and easy to game** -> the warning-as-error rule is limited to public headers and to references,
  not to every style nit.
- **A second build system step slows the gate** -> it runs after the tests and takes seconds once the dependency is cached; if it
  ever dominates, it moves to its own job rather than being dropped.
- **m.css is pinned to a commit, so a fresh environment needs network once** -> stated in the prerequisites; the offline
  requirement applies after that fetch.

## Open Questions

- Whether the spec set should get its own rendered site later. Deferrable: the Markdown reads fine and rendering it adds a
  second thing to keep in step.
- Whether `Doxyfile`'s `INPUT` should include `docs/design/` so the rationale appears in the same site. Probably yes, as links
  rather than copies - a small extension once the site builds.

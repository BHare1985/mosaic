# Proposal

## Why

The repository has a documentation pipeline and it has never produced a document. `documentation/CMakeLists.txt` fetches m.css,
configures `Doxyfile` and `conf.py`, and defines a `GenerateDocs` target - but the machine has no `doxygen`, nothing in CI builds
the target, and the last change to it only rewrote the "About" page. Every green this project reports comes from tests; the docs
have no such evidence, so they are a claim nobody has checked.

That matters more now than it did at the start: 16 capability specs, 4.4k lines of library with contract comments on every public
header, and an operator CLI plus a daemon - all of which a reader has to reconstruct from source today.

## What Changes

- The reference site builds: `doxygen` is a stated build requirement, documented and installed by CI, and the `GenerateDocs`
  target produces an m.css site from the public headers.
- The site covers what a reader needs in order: a landing page, one page per module (from the module READMEs that already exist),
  the API generated from headers, and links to the specs and design docs *rather than copies of them*.
- A `scripts/docs.sh` that a developer runs and CI wraps, in the same shape as `scripts/ci.sh`, so "the docs build" is a check
  and not a claim.
- Doxygen warnings are errors. An undocumented public symbol fails the build, which is the only way contract comments stay
  accurate rather than aspirational.
- **Not** in scope: publishing anywhere (D4 keeps everything local - the site is produced into `build/`, not deployed), a docs
  site for the specifications (they are Markdown and readable as they are), versioned documentation sets, or search tuning.

## Capabilities

### New Capabilities

- `docs/reference-site`: what the generated documentation must contain, that it builds from the public headers, and that its
  build is checked rather than trusted.

## Impact

- `documentation/` (Doxyfile, conf.py, pages), `scripts/docs.sh`, a CI job, and `doxygen` added to the documented prerequisites.
- Module READMEs become input: they are already written for this purpose and currently only read on GitHub.
- `README.md` points at how to build the docs; `docs/design/README.md` notes the site is generated, not edited.
- `docs/design/TODO.md`: a row for the documentation build closes.

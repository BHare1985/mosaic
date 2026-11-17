# Spec Delta

## Purpose

Defines the documentation this project generates from its own source - what it contains, what it is built from, and how its
correctness is checked - so a reader can find the API and the reasoning without reading 4,000 lines of headers.

## ADDED Requirements

### Requirement: The reference site builds from the public headers

A build MUST produce a browsable site from the public headers, the module READMEs and the landing page, with no network access
required at build time beyond the one-time dependency fetch. The site MUST be written under the build directory; publishing it
anywhere is out of scope.

#### Scenario: One command produces a site

- **WHEN** the documented command is run on a machine with the stated prerequisites
- **THEN** a site exists in the build directory, containing an entry per module and per public namespace

#### Scenario: The build does not depend on a network at build time

- **WHEN** the dependency has already been fetched once
- **THEN** regenerating the documentation works offline

### Requirement: Documentation warnings fail the build

An undocumented public symbol, an unresolvable reference or a malformed page MUST fail the documentation build rather than
produce a site with holes in it.

#### Scenario: An undocumented public symbol fails

- **WHEN** a public header declares a symbol with no contract comment
- **THEN** the documentation build fails and names the symbol

#### Scenario: A broken cross-reference fails

- **WHEN** a page references a target that does not exist
- **THEN** the build fails rather than shipping a dead link

### Requirement: Guides point at the specs instead of copying them

Narrative pages MUST link to the capability specifications and the design set rather than restating them, so there is one place
where a behaviour is defined and the site cannot drift from it.

#### Scenario: A module page links rather than duplicates

- **WHEN** a module page describes behaviour
- **THEN** the definition lives in a spec and the page links to it

#### Scenario: The site states which document is authoritative

- **WHEN** a reader lands on the site
- **THEN** it says that `openspec/specs/` is normative and the design set is rationale

### Requirement: The documentation build is part of the check

The documentation build MUST run in the same gate as the tests, so a change that breaks a contract comment or a page is caught
before it is merged rather than discovered by a reader.

#### Scenario: CI runs it

- **WHEN** the project's check script runs
- **THEN** it builds the documentation and reports the result alongside the tests

#### Scenario: A broken comment fails the gate

- **WHEN** a public header's contract comment is removed
- **THEN** the check fails, naming the documentation step

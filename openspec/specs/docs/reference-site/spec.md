# docs/reference-site Specification

## Purpose
Defines the documentation this project generates from its own source - what it contains, what it is built from, and how its
correctness is checked - so a reader can find the API and the reasoning without reading 4,000 lines of headers.

## Requirements

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

### Requirement: The site indexes the public surface it documents

The generated index pages (classes, namespaces and any directory tree the site offers) MUST list the entities the site
documents. A compound's absence from an index MUST NOT be caused by an undocumented parent, so a namespace whose members are
documented MUST appear rather than swallow them.

#### Scenario: Classes are listed

- **WHEN** the site is built and the classes page is opened
- **THEN** it lists the documented classes and structs, and each entry links to that entity's page

#### Scenario: A parent does not hide its members

- **WHEN** documented types live inside a namespace that has no documentation of its own
- **THEN** the namespace is documented so that both the namespace and its members are reachable, rather than the members
  disappearing from every index

### Requirement: No navigation entry is a dead end

Every entry in the site's navigation MUST open a page that lists something. A page the generator cannot fill MUST NOT be
offered in the navigation, and the reason MUST be recorded where the navigation is configured.

#### Scenario: An empty page is not in the menu

- **WHEN** a navigation entry would open a page with no entries in it
- **THEN** the entry is removed from the navigation and the reason is written next to the navigation configuration

#### Scenario: A listed entry has content

- **WHEN** a reader picks any entry from the site's navigation
- **THEN** the page they land on lists at least one item

### Requirement: The site carries the project's identity

The site MUST show the project's logo in its header, a one-line statement of what the project is beside it, and a theme colour
taken from the logo. The logo MUST be the project's original artwork and MUST NOT require a copied file or a build-time
download.

#### Scenario: The logo appears on every page

- **WHEN** any page of the site is opened
- **THEN** the header shows the project logo and the one-line brief

#### Scenario: Identity costs no asset

- **WHEN** the site is built on a machine with no logo file in the repository
- **THEN** the build succeeds and the logo still renders, because the artwork is referenced rather than copied

### Requirement: A site that builds but lists nothing fails the check

The project's check MUST verify that the generated index pages contain entries, so a successful build cannot stand for a site
with empty navigation.

#### Scenario: An empty index fails

- **WHEN** the generated classes, namespaces or directory index is empty
- **THEN** the check fails and names the page that is empty

#### Scenario: The check runs with the rest

- **WHEN** the project's check script runs on a machine that can build the documentation
- **THEN** the index pages are verified as part of it, and a machine without the documentation toolchain reports the step as
  skipped rather than passing silently

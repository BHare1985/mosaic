# Spec Delta

## ADDED Requirements

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

# Spec Delta

## Purpose

Defines what the project's README must do as its front door: show the project's identity first, say what a reader gets and who
it is for, quote cost as a measurement rather than an adjective, credit the designs it came from, and keep every claim traceable
to a specification. How the reference site is generated is `docs/reference-site`; this capability is the page that both GitHub
visitors and the site's mainpage see.

## ADDED Requirements

### Requirement: The front page opens with the project's identity

The README MUST open with the project's original logo, centred above the title, followed by a one-line statement of what the
project is, so a reader knows what they are looking at before reading any explanation. The logo MUST be the project's original
artwork, not a re-drawn or re-coloured substitute.

#### Scenario: The logo is the first thing on the page

- **WHEN** the README is rendered, on GitHub or as the generated site's mainpage
- **THEN** the original logo appears above everything else, centred, at its original aspect ratio

#### Scenario: The logo is the artwork the project has always used

- **WHEN** the README's logo is compared with the artwork from the project's earlier front page
- **THEN** it is the same image, referenced as it was originally referenced, rather than a new file added to the repository

### Requirement: The front page states what a reader gets

The README MUST state the value propositions - what adopting this library buys the reader - as a short list, and each
proposition MUST name the design property that makes it true. A proposition the design cannot support MUST NOT appear.

#### Scenario: Each promise has a mechanism behind it

- **WHEN** a reader reads the value propositions
- **THEN** each one names what it buys (nothing to run, nothing to trust, nothing to elect, replay from any holder, bounded
  cost) and the property that delivers it

#### Scenario: No benefit is sold without a mechanism

- **WHEN** a proposition has no design property behind it
- **THEN** it is either removed or moved to the optional future layers, which are linked rather than promised

### Requirement: The front page names who it is for

The README MUST address the reader as an audience: for each audience it names what that reader is looking for and what this
gives them. Applications MUST be named as problems the audience has, never as feature names.

#### Scenario: A reader finds their own situation

- **WHEN** a reader arrives from a systems team, an edge deployment, an audit function, or an agent/tool build
- **THEN** they find their situation named, with what they get and a link to the capability that provides it

#### Scenario: Nothing is offered that the system cannot do

- **WHEN** an application is mentioned that the design does not implement
- **THEN** it is marked as an optional future layer and linked to the document that says so, rather than listed as available

### Requirement: The front page shows the design as pictures

The README MUST show how the system works with diagrams, not only prose and links: at minimum one record's integrity, one
node's three roles, and a fleet replicating with no broker - and a fourth picture showing where the system sits inside a larger
deployment. Each picture MUST be followed by the mechanism it depicts, so a reader can follow the design without opening another
document.

#### Scenario: A record can be traced through the pictures

- **WHEN** a reader follows one record from a writer to a follower's store using the diagrams
- **THEN** the pictures cover every hop, and each hop is described next to the arrow it belongs to

#### Scenario: The larger deployment is pictured too

- **WHEN** a reader asks how this fits with their own writers, replicas, and applications
- **THEN** a picture shows that arrangement and names which parts they supply

### Requirement: A borrowed mechanism is attributed where it appears

Where the page uses a mechanism that came from Dafka, Malamute or the ZeroMQ Guide, it MUST name the source beside the mechanism
and link it, so the lineage is visible without a separate history section, and it MUST state which parts are the project's own.

#### Scenario: An informed reader can separate the lineage

- **WHEN** a reader who knows Dafka, Malamute or the ZeroMQ Guide reads the design section
- **THEN** they can tell which mechanism is whose, and what this project added that neither had

#### Scenario: Every attribution resolves

- **WHEN** the page cites a source
- **THEN** the citation links to that source and the link is reachable

### Requirement: Cost is quoted as a measurement or a structural fact

Any statement about performance MUST be either a number the repository's own benchmarks produce, quoted with the command that
produced it, or a structural property of the design. The README MUST say what is fast about the system rather than calling the
system fast, and MUST NOT present a measurement as a comparison against another product.

#### Scenario: A number comes with the command that produced it

- **WHEN** the README quotes a cost - matching at a subscription count, durable append latency
- **THEN** the command that reproduces it appears next to it, and the qualifier that matters (machine, durability mode) is
  stated

#### Scenario: No bare adjective

- **WHEN** the README describes speed
- **THEN** the claim names what is cheap (append, fan-out without a broker hop, per-tick work that does not grow with stored
  volume, repair paced by credit) rather than asserting "fast"

### Requirement: Prior art is credited as design, with links

The README MUST credit the designs the system came from - Dafka (and RFC 46) and Malamute - and state explicitly that the
system is not wire-compatible with either. It MUST link the prior-art note and the research the original vision referenced, so
a reader can check the claim rather than take it.

#### Scenario: A reader can tell what was borrowed and what was rejected

- **WHEN** the reader reaches the credits section
- **THEN** they see which design contributed which idea, which central component was deliberately rejected, and a link to the
  longer account

#### Scenario: Compatibility is not implied

- **WHEN** the README mentions Dafka or Malamute
- **THEN** it says in the same place that neither is wire-compatible with this project

### Requirement: Every claim on the front page traces to what owns it

Each behaviour and cost the front page promises MUST link to the specification, design document or operations guide that defines
it, and the front page MUST NOT restate normative behaviour in place of that link.

#### Scenario: A guarantee is a link, not a paragraph

- **WHEN** the page states a guarantee (convergence, verification, retention, credit)
- **THEN** the statement is short and carries a link to the document that owns it

#### Scenario: Status claims match the implementation

- **WHEN** the page reports what is implemented and what is still open
- **THEN** each open item links to the work board, and nothing described as working is listed as open

### Requirement: The front page's links are checked

Every relative link and image target in the README MUST resolve to a path that exists in the repository, and the check MUST run
in the same gate as the tests so a front page cannot rot into dead links.

#### Scenario: A broken relative link fails the check

- **WHEN** a relative link or image target in the README does not exist
- **THEN** the check fails and names the missing target

#### Scenario: The check is what the project runs

- **WHEN** the project's check script runs
- **THEN** the front page's links are verified as part of it, and the result is reported alongside the tests

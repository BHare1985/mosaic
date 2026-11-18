# api/c-abi Specification

## Purpose
Defines a stable C surface over the library, so a caller in any language with an FFI can write and read logs without adopting C++
- and so the ownership rules are stated once, explicitly, instead of being implied by headers.

## Requirements

### Requirement: The ABI is usable from a C compiler

The public header MUST compile as C, use no C++ types, no templates and no exceptions, and MUST NOT require a C++ runtime to be
included by the caller. Symbols MUST have C linkage.

#### Scenario: A C translation unit uses it

- **WHEN** a C-only test compiles the header and links the library
- **THEN** it builds and runs with a C compiler alone

#### Scenario: Errors are values

- **WHEN** any call fails
- **THEN** it reports a status code and, where useful, an error string the caller frees - never an exception or a longjmp

### Requirement: Every handle has one owner and one free

Handles (node, store, transport, iterator) MUST be created by named constructors, MUST be freed by exactly one function, and MUST
tolerate being freed once at any point without use-after-free. A handle MUST NOT be freed by the library while the caller holds it.

#### Scenario: Double free is rejected, not undefined

- **WHEN** a handle is freed twice
- **THEN** the second free is a no-op or reports an error, and nothing crashes

#### Scenario: Records cross the boundary by value

- **WHEN** a record is delivered to a C callback
- **THEN** the record is valid for the duration of the callback, and any bytes the caller keeps are explicitly copied by an ABI
  function it frees itself

### Requirement: The ABI can do what the CLI does

The surface MUST be sufficient to publish to a channel and to tail a pattern - the two operations the two-host demonstration
uses - so that a binding is a thin wrapper rather than an alternative implementation.

#### Scenario: Publish and tail through the ABI

- **WHEN** one process publishes via the ABI and another tails via the ABI
- **THEN** the records arrive in order, and the same behaviour is observable through both the ABI and the CLI

#### Scenario: Compatibility is additive

- **WHEN** a symbol's signature or a struct's field order changes after release
- **THEN** that is a breaking change and must be a new symbol or a new major version

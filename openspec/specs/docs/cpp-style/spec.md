# docs/cpp-style Specification

## Purpose
Defines the C++ style this project enforces mechanically, and the rule that a style is adopted by fixing the code and then
gating, not by gating and hoping.

## Requirements

### Requirement: Style is enforced by the build, not by review

The check script MUST run `clang-format` in dry-run mode over every tracked C++ file and `clang-tidy` with the project's check set,
and MUST fail when either reports anything. A style suggestion MUST NOT be reported as a growing backlog: a check that is
advisory for long enough is a check that is ignored.

#### Scenario: A formatted-differently file fails the check

- **GIVEN** a source file whose formatting differs from `.clang-format`
- **WHEN** the check script runs
- **THEN** it fails and names the file

#### Scenario: A style violation fails the check

- **GIVEN** a single-statement body without braces where the style requires them
- **WHEN** the check script runs
- **THEN** it fails and names the file and line

### Requirement: The style keeps behaviour neutral

Every mechanical style fix MUST leave behaviour unchanged, and the test suite MUST be the evidence: the whole suite passes before
and after a pass, with the same assertions. A fix that appears to change behaviour MUST be split out and treated as a bug fix.

#### Scenario: A style pass moves no requirements

- **GIVEN** the suite green before a style pass
- **WHEN** the style pass is applied
- **THEN** the suite is green with the same test case and assertion counts

### Requirement: Names are imported, never leaked

Source files MAY import individual standard names (`using std::string;`). Headers MUST NOT contain using-declarations or
directives, and no file MUST contain `using namespace`.

#### Scenario: A header carrying a using-declaration is refused

- **GIVEN** a public header with a using-declaration
- **WHEN** the check script runs
- **THEN** the check fails

#### Scenario: A source file imports a name

- **GIVEN** a `.cpp` file that imports `std::string` and uses it unqualified
- **WHEN** the file is built and linted
- **THEN** both pass

# Tasks

## 1. The two configs

- [x] 1.1 Replace `.clang-format` with the LLVM-based profile: 4 spaces, no tabs, 100 columns, attached braces, left-aligned
      pointers, no namespace indentation, indented case labels, `IncludeBlocks: Regroup` with `^<mosaic/` as the first category.
      Verified by: `clang-format --dry-run --Werror` accepts the config, and a sample file shows project headers before standard
      headers.
- [x] 1.2 Write `.clang-tidy`: the check families from the proposal, the naming table per construct (namespace/function/variable
      `lower_case`, types `CamelCase`, private and protected members `lower_case` with a `_` suffix, constants `CamelCase` with a
      `k` prefix, macros `UPPER_CASE`), and each exclusion with the reason it is excluded.
      Verified by: `clang-tidy --dump-config` reports the naming options, and the header filter does not walk `build/` or `_deps/`.
- [x] 1.3 Add `google-build-using-namespace` and `google-global-names-in-headers` so the Option 1 rule is enforced rather than
      described. Verified by: a scratch file with `using namespace std;` is flagged, and `using std::string;` in a `.cpp` is not.

## 2. Write the style down

- [x] 2.1 Add a "C++ style" section to `docs/design/conventions.md`: the naming table, ownership and Rule-of-Zero guidance,
      `explicit`/`override`/`[[nodiscard]]`, and the `std::` rule with the Option 1 example.
      Verified by: the table matches the two config files and names both tool versions.
- [x] 2.2 State how to apply it: `clang-format -i`, the compile-commands database command, and how to run clang-tidy.
      Verified by: the commands in the doc run as written on this tree.

## 3. The mechanical pass

- [x] 3.1 Reformat every tracked source with `clang-format -i`. Verified by: `./scripts/ci.sh` format step green, and both a
      ZeroMQ and a ZeroMQ-free configuration still build and pass.
- [x] 3.2 Import the standard names each `.cpp` actually uses (`using std::string;`) and drop the `std::` prefixes there; headers
      keep `std::` everywhere. Verified by: the tree builds fully qualified-free in `.cpp` bodies, headers contain no
      using-declarations, and `rg -c "std::" include/mosaic` still matches the header count.
- [x] 3.3 **Not adopted:** `.git-blame-ignore-revs` and blame skipping. The reformat stays a separate commit; nothing else.
      Recorded in design D6 as a deliberate choice rather than an omission.

## 4. Make it a gate

- [x] 4.1 Install `clang-tidy` and add a lint step that reports `SKIPPED - clang-tidy not found` when it is absent.
      Verified by: clang-tidy 21.1.8 installed, the step runs in 46s with `run-clang-tidy -j` (serial was minutes, which is why
      nobody would run it), and the manual and CI commands are both documented in `conventions.md`.
- [x] 4.2 Triage the first clang-tidy run: fix behaviour-preserving findings, exclude noise with a written reason, and only then
      set `WarningsAsErrors`. Verified by: the first run reported 691 findings; the five **correctness** ones were fixed by hand
      (a misplaced widening cast, a duplicated switch branch caused by an `#ifdef`, two unchecked optional dereferences, and a
      cast of an out-of-range byte into an enum - the last one now rejects a malformed algorithm tag instead of guessing), and
      the step is gated on `bugprone`/`clang-analyzer`, which run clean. **`WarningsAsErrors` is not set for style:** 521
      style/modernisation suggestions remain, tracked as STYLE-2, because a gate that is red from the day it lands is a gate
      people learn to ignore.
- [x] 4.3 Turn `CMAKE_EXPORT_COMPILE_COMMANDS` on so the lint step has a compilation database without a manual configure.
      Verified by: `build/ci-core/compile_commands.json` exists after `./scripts/ci.sh fast`.

## 5. Close out

- [x] 5.1 Add a board row for the style adoption and close it. Verified by: `./scripts/check_todo_ids.sh` exits 0.
- [x] 5.2 Definition of done: `./scripts/ci.sh` green including the new lint step, the documentation site still builds, and no
      behaviour changed (the test counts are the same before and after the formatting commit).
      Verified by: the CI run and a comparison of the suite's case and assertion counts.

# Proposal

## Why

The repository's C++ style exists only as an artefact of two things: a `.clang-format` inherited from the template, and the way
the code already happens to read. That config is **WebKit with `ColumnLimit: 0`** - no line limit at all - and there is no
`clang-tidy` anywhere, so nothing enforces modern semantics, naming or the Core Guidelines. A contributor (or an agent) has to
imitate the surrounding code and guess where the boundaries are.

The naming half is already right by luck and habit: types are `PascalCase`, functions and variables `snake_case`, private members
carry a trailing `_`, constants are `kPascalCase` (29 trailing-underscore members in `node.h` alone). Writing that down and
enforcing it costs little and stops it from decaying.

## What Changes

- `.clang-format` replaced with the widely used modern profile: LLVM base, 4 spaces, no tabs, **100 columns**, attached braces,
  left-aligned pointers, no namespace indentation, indented case labels, and include blocks regrouped with this project's own
  headers first.
- A new `.clang-tidy` enabling `bugprone`, `clang-analyzer`, `cppcoreguidelines`, `modernize`, `performance` and `readability`,
  with the naming table enforced per construct and a short list of excluded checks whose rationale is written down.
- A written style section in `docs/design/conventions.md`: the naming table, ownership and special-member rules, `[[nodiscard]]`
  and `explicit` guidance, and the `using` rule below.
- **`std::` noise, Option 1:** `using namespace ...` stays banned everywhere (including `.cpp` files), but a `.cpp` file may
  import the specific names it uses - `using std::string;` - which is what removes most of the repetition. Headers never carry
  using-declarations: a header that imports a name into `namespace mosaic` changes what every includer sees.
- One mechanical reformat of the whole tree, plus a `.git-blame-ignore-revs` entry so `git blame` can skip it.
- The format gate in `scripts/ci.sh` stays; a `clang-tidy` step is added that reports **skipped** when the tool is absent, the
  same policy the documentation step uses.
- **Not** in scope: renaming existing types or files, adopting a different brace style for lambdas, or turning `WarningsAsErrors`
  on in the first pass - findings are triaged and fixed first, then the gate tightens.

## Capabilities

None. This is tooling and documentation; behaviour does not change. The change declares `skip_specs: true`, and any clang-tidy
fix that touches code is behaviour-preserving and reviewed as such.

## Impact

- `.clang-format` (replaced), `.clang-tidy` (new), `.git-blame-ignore-revs` (new).
- Every `.h`/`.cpp` in `include/`, `source/`, `test/`, `standalone/`, `fuzz/` reformatted once; `.cpp` files additionally lose
  their `std::` prefixes where a using-declaration imports the name.
- `docs/design/conventions.md`: the style section. `scripts/ci.sh`: the lint step.
- `clang-tidy` becomes a documented optional prerequisite, like `doxygen` and `clang-format`.

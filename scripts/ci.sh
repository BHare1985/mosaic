#!/usr/bin/env bash
# The whole check, runnable locally and in CI. Local-first on purpose: every green this project reports
# should be reproducible by one command on a plain machine (decision D4 keeps anything from going upstream).
#
#   ./scripts/ci.sh            # everything
#   ./scripts/ci.sh fast       # core only: no ZeroMQ, no sanitizers
#
# Each step prints what it is doing so a failure names the configuration that broke.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

MODE="${1:-all}"
FAILED=0
step() { printf '\n=== %s ===\n' "$1"; }
fail() { printf 'FAILED: %s\n' "$1" >&2; FAILED=1; }

run_matrix() {
    # Four configurations: crypto on/off by ZeroMQ on/off. Each has caught something the others did not.
    local name="$1" cmake_args="$2" dir="build/ci-$1"
    step "$name"
    # shellcheck disable=SC2086
    if ! cmake -S test -B "$dir" $cmake_args >/dev/null; then fail "$name: configure"; return; fi
    if ! cmake --build "$dir" -j >/dev/null; then fail "$name: build"; return; fi
    if ! "$dir"/MosaicTests > "$dir/tests.log" 2>&1; then
        tail -20 "$dir/tests.log" >&2
        fail "$name: tests"
    else
        grep -E '^\[doctest\] (test cases|assertions)' "$dir/tests.log"
    fi
    # The C ABI is checked by a C program in every configuration: it is the one suite that must not need C++.
    if [ -x "$dir/mosaic_c_test" ]; then
        if ! "$dir/mosaic_c_test" > "$dir/cabi.log" 2>&1; then
            tail -10 "$dir/cabi.log" >&2
            fail "$name: C ABI"
        else
            tail -1 "$dir/cabi.log"
        fi
    fi
}

run_matrix core          "-DMOSAIC_WITH_ZMQ=OFF -DMOSAIC_BUILD_ZMQ_FROM_SOURCE=OFF"
run_matrix no-crypto     "-DMOSAIC_WITH_SODIUM=OFF -DMOSAIC_WITH_ZMQ=OFF -DMOSAIC_BUILD_ZMQ_FROM_SOURCE=OFF"

if [ "$MODE" != "fast" ]; then
    run_matrix full          "-DMOSAIC_BUILD_ZMQ_FROM_SOURCE=ON"
    run_matrix zmq-no-crypto "-DMOSAIC_WITH_SODIUM=OFF -DMOSAIC_BUILD_ZMQ_FROM_SOURCE=ON"

    step "sanitizers (ASan + UBSan)"
    SAN="build/ci-sanitize"
    if cmake -S test -B "$SAN" -DMOSAIC_WITH_ZMQ=OFF -DMOSAIC_BUILD_ZMQ_FROM_SOURCE=OFF \
            -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" >/dev/null \
        && cmake --build "$SAN" -j >/dev/null; then
        if ! "$SAN"/MosaicTests > "$SAN/tests.log" 2>&1; then
            tail -30 "$SAN/tests.log" >&2
            fail "sanitizers"
        else
            grep -E '^\[doctest\] (test cases|assertions)' "$SAN/tests.log"
        fi
    else
        fail "sanitizers: build"
    fi
fi

step "documentation"
if ! ./scripts/docs.sh > /tmp/ci-docs.log 2>&1; then
    tail -12 /tmp/ci-docs.log >&2
    fail "documentation"
else
    grep -E '^docs:' /tmp/ci-docs.log | tail -1
fi

step "site"
# A site that builds is not a site that works: the index pages are checked for content, and every navbar entry must open a
# page rather than a shell (spec: docs/reference-site).
if ! ./scripts/check_site.sh; then
    fail "site"
fi

step "front page"
# The README is the front door and the reference site's mainpage, so a moved directory or an unbalanced code fence breaks
# it silently. The check reads the page itself; prose is a review question (spec: docs/front-page).
if ! ./scripts/check_readme.sh; then
    fail "front page"
fi

step "lint"
# Style and correctness are the same gate now: .clang-tidy is the rule set, and any finding is a
# failure. clang-tidy is optional, like doxygen - absent, the step says so instead of passing
# silently. See docs/design/conventions.md for the manual commands.
if command -v clang-tidy >/dev/null 2>&1; then
    DB=$(find build -name compile_commands.json | head -1)
    if [ -z "$DB" ]; then
        fail "lint: no compilation database (run a configure first)"
    else
        DB_DIR=$(dirname "$DB")
        # `run-clang-tidy` compiles each translation unit in parallel; the serial form over the whole
        # tree takes minutes, which is long enough that nobody would run it.
        if command -v run-clang-tidy >/dev/null 2>&1; then
            if ! run-clang-tidy -p "$DB_DIR" -j "$(nproc)" -quiet 'source/.*\.cpp' \
                    > /tmp/ci-lint.log 2>&1; then
                grep -E "warning:|error:" /tmp/ci-lint.log | head -20 >&2
                fail "lint"
            else
                echo "lint: clean (clang-tidy, style and correctness)"
            fi
        else
            if ! clang-tidy -p "$DB_DIR" $(git ls-files 'source/**/*.cpp') > /tmp/ci-lint.log 2>&1; then
                grep -E "warning:" /tmp/ci-lint.log | head -20 >&2
                fail "lint (serial; install run-clang-tidy for parallelism)"
            else
                echo "lint: clean (serial; install run-clang-tidy for parallelism)"
            fi
        fi
    fi
else
    echo "lint: SKIPPED - clang-tidy not found."
    echo "      install it (apt install clang-tidy, or an LLVM release) and re-run."
fi

step "format"
if command -v clang-format >/dev/null 2>&1; then
    dirty=0
    for f in $(git ls-files '*.cpp' '*.h' | grep -E '^(include|source|test|standalone|fuzz)/'); do
        [ -f "$f" ] || continue # a file deleted in the working tree is not a formatting failure
        clang-format --dry-run --Werror "$f" >/dev/null 2>&1 || { echo "needs formatting: $f"; dirty=1; }
    done
    [ "$dirty" -eq 0 ] || fail "format"
else
    echo "clang-format not installed: skipping (install it or accept an unverified format claim)"
fi

printf '\n=== %s ===\n' "$([ "$FAILED" -eq 0 ] && echo 'CI: all checks passed' || echo 'CI: FAILURES ABOVE')"
exit "$FAILED"

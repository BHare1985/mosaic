#!/usr/bin/env bash
# Build the reference documentation. Same shape as scripts/ci.sh: a command a developer runs that CI wraps,
# so "the docs build" is something produced rather than claimed.
#
#   ./scripts/docs.sh
#
# Requires doxygen on PATH (>= 1.9). Absent, this reports the skip and says how to install it, because a
# documentation step that silently passes when it cannot run is worse than no step.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

SITE="build/all/documentation/doxygen/html/index.html"

if ! command -v doxygen >/dev/null 2>&1; then
    echo "docs: SKIPPED - doxygen not found."
    echo "      install it (apt install doxygen, or a release tarball on PATH) and re-run."
    exit 0
fi

printf '=== docs (m.css + doxygen %s) ===\n' "$(doxygen --version)"
if ! cmake -S all -B build >/dev/null; then
    echo "docs: FAILED - configure" >&2
    exit 1
fi
if ! cmake --build build --target GenerateDocs >/tmp/mosaic-docs.log 2>&1; then
    tail -20 /tmp/mosaic-docs.log >&2
    echo "docs: FAILED - generate" >&2
    exit 1
fi

if [ ! -f "$SITE" ]; then
    echo "docs: FAILED - no $SITE was produced" >&2
    exit 1
fi

pages=$(find build/all/documentation/doxygen/html -name '*.html' | wc -l | tr -d ' ')
echo "docs: $pages pages written to build/all/documentation/doxygen/html/"

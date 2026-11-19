#!/usr/bin/env bash
# The front page is the project's front door, and the one file most likely to rot: a moved directory leaves a dead link, a
# refactor deletes the logo block, an edit unbalances a code fence. This checks the three things a script can actually know
# (spec: docs/front-page):
#
#   1. the identity block is still the first thing on the page;
#   2. every RELATIVE link and image target exists (absolute URLs and bare #fragments are exempt - they cannot be verified
#      offline, and the logo is deliberately referenced rather than copied);
#   3. the fenced-code count is even.
#
# It checks existence, not accuracy: a link that resolves to the wrong document passes. Prose is a review question.
set -uo pipefail
cd "$(dirname "$0")/.."

README=README.md
LOGO='https://github.com/BHare1985/mosaic/assets/2180284/73ac71af-2309-4efd-a24a-bf8d8b021eb1'
failed=0

if [ ! -f "$README" ]; then
    echo "check_readme: no $README" >&2
    exit 1
fi

# 1. The identity block: first non-empty line, and the logo within the first eight lines.
first=$(grep -m1 -v '^[[:space:]]*$' "$README")
if [ "$first" != '<p align="center">' ]; then
    echo "check_readme: the page must open with the centred logo block, found: $first" >&2
    failed=1
fi
if ! head -8 "$README" | grep -qF "$LOGO"; then
    echo "check_readme: the original logo ($LOGO) is not in the first eight lines" >&2
    failed=1
fi

# 2. Relative link and image targets resolve. Anchors are stripped; anything with a scheme is exempt.
targets=$(
    {
        grep -oE '\]\([^)]+\)' "$README" | sed -e 's/^](//' -e 's/)$//'
        grep -oE 'src="[^"]+"' "$README" | sed -e 's/^src="//' -e 's/"$//'
    } | while IFS= read -r target; do
        case "$target" in
            http://* | https://* | mailto:* | '#'*) continue ;;
        esac
        printf '%s\n' "${target%%#*}"
    done | sort -u
)
for target in $targets; do
    [ -e "$target" ] || {
        echo "check_readme: broken relative link or image: $target" >&2
        failed=1
    }
done

# 3. Balanced fences: an unclosed or stray fence turns everything after it into code.
fences=$(grep -c '^```' "$README" || true)
if [ $((fences % 2)) -ne 0 ]; then
    echo "check_readme: unbalanced code fences ($fences, expected an even number)" >&2
    failed=1
fi

if [ "$failed" -eq 0 ]; then
    echo "check_readme: identity, $(( $(printf '%s\n' "$targets" | grep -c .) )) relative links and fences OK"
fi
exit "$failed"

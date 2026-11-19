#!/usr/bin/env bash
# The reference site once built green while its Classes and Namespaces pages listed nothing: m.css only indexes a compound
# that carries a brief, and every public type hangs off a namespace that had none, so the parent was dropped and took its
# members with it. A build that succeeds is not a site that works. This checks the generated pages (spec: docs/reference-site):
#
#   1. every list page the navbar offers (Classes, Namespaces, ...) lists at least one entry;
#   2. every narrative page the navbar offers has a body, not just a page shell.
#
# It reads the built HTML, not the sources - the sources were already right the first time.
set -uo pipefail
cd "$(dirname "$0")/.."

SITE=${MOSAIC_SITE_DIR:-build/all/site/html}
failed=0

if [ ! -f "$SITE/annotated.html" ]; then
    echo "check_site: SKIPPED - $SITE not built."
    echo "            run ./scripts/docs.sh (needs doxygen) and re-run."
    exit 0
fi

nav=$(sed -n '/^LINKS_NAVBAR1 = \[/,/^\]/p' docs/site/conf.py)
# Bare entries are list pages: (None, 'namespaces', []). Ids inside the 'pages' dropdown are narrative pages.
list_pages=$(printf '%s\n' "$nav" | grep -oE "\(None, '[a-z]+', \[\]\)" | grep -oE "'[a-z]+'" | tr -d "'")
text_pages=$(printf '%s\n' "$nav" | grep -oE "\(None, '[a-z]+'\)" | grep -oE "'[a-z]+'" | tr -d "'")

# Links inside the page's first m-doc list: an index with no entries is the failure this exists for. `href="#..."` is the
# list's own alphabet jump bar, not an entry - counting it made this check pass on a site with nothing in it.
entries_in() {
    tr '\n' ' ' < "$SITE/$1" | grep -oE '<ul class="m-doc">.*?</ul>' | head -1 | grep -oE '<a href="[^"#][^"]*\.html"' | wc -l
}

# Text inside <article>, with scripts and tags removed: a shell has a heading and nothing else.
text_bytes_in() {
    tr '\n' ' ' < "$SITE/$1" |
        sed -e 's|<script[^>]*>.*</script>||g' -e 's|.*<article>||' -e 's|</article>.*||' -e 's|<[^>]*>| |g' |
        tr -s ' ' |
        wc -c
}

for kind in $list_pages; do
    page="$kind.html"
    if [ ! -f "$SITE/$page" ]; then
        echo "check_site: the navbar offers '$kind' but $page was not generated" >&2
        failed=1
        continue
    fi
    count=$(entries_in "$page")
    if [ "$count" -lt 1 ]; then
        echo "check_site: $page lists nothing - an undocumented parent (a namespace or a directory) is swallowing its members" >&2
        failed=1
    fi
done

for name in $text_pages; do
    page="$name.html"
    if [ ! -f "$SITE/$page" ]; then
        echo "check_site: the navbar offers '$name' but $page was not generated" >&2
        failed=1
        continue
    fi
    bytes=$(text_bytes_in "$page")
    if [ "$bytes" -lt 200 ]; then
        echo "check_site: $page is an empty shell (${bytes} bytes of text) - check for a page id colliding with a generated one" >&2
        failed=1
    fi
done

if [ "$failed" -eq 0 ]; then
    echo "check_site: $(entries_in annotated.html) classes, $(entries_in namespaces.html) namespaces; every navbar entry has content"
fi
exit "$failed"

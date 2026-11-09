#!/usr/bin/env bash
# Every TODO(ID...) / task id referenced in code or docs must exist as a row/heading in docs/design/TODO.md.
set -euo pipefail
cd "$(dirname "$0")/.."
ids=$(grep -rhoE '\b(CORE|STORE|LOG|MATCH|FLOW|ENC|HELLO|ACK|HEADS|NET|SIM|ZMQ|DISC|NODE|SEC|JOBS|CLI|CI|DOC|BIND)-[0-9]+\b' \
    include source standalone test docs/design --include='*.h' --include='*.cpp' --include='*.md' \
    --exclude=TODO.md | sort -u)
missing=0
for id in $ids; do
    if ! grep -qE "^\| $id \|" docs/design/TODO.md; then
        echo "TODO.md has no row for $id"
        missing=1
    fi
done
exit $missing

#!/usr/bin/env bash
# Build and install the Python binding against a CMake build tree.
#
# One command: build the C ABI (CMake), then install the extension into the current interpreter. Pass a virtualenv's
# python to keep it isolated: `scripts/build_python.sh /path/to/venv/bin/python`.
set -euo pipefail

cd "$(dirname "$0")/.."
PYTHON="${1:-python3}"
BUILD_DIR="${MOSAIC_BUILD_DIR:-build/all}"

if [ ! -d "$BUILD_DIR" ]; then
  echo "build_python: configuring $BUILD_DIR"
  cmake -S all -B "$BUILD_DIR" > /dev/null
fi
cmake --build "$BUILD_DIR" --target MosaicC -j > /dev/null
echo "build_python: C ABI built in $BUILD_DIR"

# The extension builds against the tree above, so isolation is off: the "build dependency" is the build we just made.
if ! "$PYTHON" -c "import setuptools" 2>/dev/null; then
  "$PYTHON" -m pip install --quiet setuptools
fi
# Build a wheel, then install it. `pip install .` builds into a temporary tree that has produced a wheel whose
# extension was linked without the archives - an undefined-symbol error at import, not at build. A wheel built here
# and inspected is the same artefact that gets installed.
ROOT="$(pwd)"
rm -rf bindings/python/build bindings/python/dist bindings/python/*.egg-info
( cd bindings/python && MOSAIC_BUILD_DIR="$ROOT/$BUILD_DIR" "$PYTHON" setup.py -q bdist_wheel > /dev/null )
"$PYTHON" -m pip install --quiet --force-reinstall bindings/python/dist/*.whl
"$PYTHON" -c "import mosaic; print('build_python: mosaic', mosaic.version())"

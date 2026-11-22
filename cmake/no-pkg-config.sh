#!/bin/sh
# Stand-in pkg-config for builds where the real one is absent.
#
# libzmq's generated Findsodium.cmake does `find_package(PkgConfig REQUIRED)` before looking for
# libsodium, and CMake's FindPkgConfig rejects an executable that fails `--version`. This answers that
# one probe and reports "no modules" for everything else, so the SODIUM_* paths we pass explicitly are
# used instead of a pkg-config lookup. It never claims a module exists.
case "$1" in
    --version) echo "0.0-mosaic-stub" ; exit 0 ;;
    *) exit 1 ;;
esac

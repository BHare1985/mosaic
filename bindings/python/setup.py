"""Build the extension against the CMake-built C ABI.

setuptools cannot discover C++ libraries in a CMake tree, so the paths come from MOSAIC_BUILD_DIR - the directory
`scripts/build_python.sh` used. Everything is explicit here rather than guessed.
"""

import os
import pathlib

from setuptools import Extension, setup

build_dir = pathlib.Path(os.environ.get("MOSAIC_BUILD_DIR", "build/all"))
source_dir = pathlib.Path(__file__).resolve().parent.parent.parent

libraries = []
library_dirs = []
for candidate in (build_dir, build_dir / "all"):
    if not candidate.is_dir():
        continue
    library_dirs.append(str(candidate))
    libraries.extend(["MosaicC", "MosaicZmq", "Mosaic", "czmq-static", "sodium"])
    break

# Static archives, in dependency order - the linker reads a static archive once, so anything that depends on a
# later archive must come first. Paths are globbed inside the build tree because CPM's dependency layout is not
# ours to depend on.
extra_objects = []
for pattern in (
    "**/libMosaicC.a",
    "**/libMosaicZmq.a",
    "**/libMosaic.a",
    "**/libmosaic_sqlite3.a",
    "**/libzmq.a",
    "**/libsodium.a",
):
    matches = sorted(build_dir.glob(pattern))
    if matches:
        extra_objects.append(str(matches[0]))

setup(
    ext_modules=[
        Extension(
            "mosaic._mosaic",
            sources=["module.c"],
            include_dirs=[
                str(source_dir / "include"),
                str(build_dir / "generated" / "include"),
                str(build_dir / "all" / "generated" / "include"),
            ],
            extra_objects=extra_objects,
            libraries=libraries if not extra_objects else [],
            library_dirs=library_dirs,
            # Static archives need their runtime's libraries by hand.
            extra_link_args=["-pthread", "-lstdc++", "-lm"],
        )
    ]
)

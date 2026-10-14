#!/usr/bin/env bash
# Builds the Cap'n Proto tools (capnp, capnpc-c++, capnpc-capnp) as portable binaries and
# packages them with the standard schemas into a relocatable tree:
#
#   capnproto-tools-newtype-<platform>/
#     bin/      capnp, capnpc, capnpc-c++, capnpc-capnp
#     include/  capnp/*.capnp (the standard schemas, found relative to bin/capnp)
#     LICENSE
#
# which is smoke-tested from a different location and archived as .tar.gz (or .zip on Windows)
# in $WORK_DIR.
#
# Only the tools are packaged: the libraries need to match the consumer's toolchain and libc, so
# consumers build those from the source tarball.
#
# Usage: build-tools.sh <platform> [extra cmake configure args...]

set -euo pipefail

platform=$1
shift

root=$(cd "$(dirname "$0")/../.." && pwd)
work=${WORK_DIR:-$root/build-newtype}
build=$work/build
stage=$work/stage
name=capnproto-tools-newtype-$platform
pkg=$work/$name

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) windows=1; exe=.exe ;;
  *) windows=0; exe= ;;
esac

jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo "${NUMBER_OF_PROCESSORS:-4}")

rm -rf "$stage" "$pkg" "$pkg.tar.gz" "$pkg.zip"

cmake -S "$root/c++" -B "$build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DWITH_OPENSSL=OFF \
  -DWITH_ZLIB=OFF \
  -DCMAKE_INSTALL_PREFIX="$stage" \
  "$@"
cmake --build "$build" --config Release --parallel "$jobs"
cmake --install "$build" --config Release

mkdir -p "$pkg/bin" "$pkg/include"
for tool in capnp capnpc-c++ capnpc-capnp; do
  cp "$stage/bin/$tool$exe" "$pkg/bin/"
done
if [ "$windows" = 1 ]; then
  cp "$pkg/bin/capnp.exe" "$pkg/bin/capnpc.exe"
else
  ln -s capnp "$pkg/bin/capnpc"
fi

(cd "$stage/include" && find . -name '*.capnp' | sort) | while read -r schema; do
  mkdir -p "$pkg/include/$(dirname "$schema")"
  cp "$stage/include/$schema" "$pkg/include/$schema"
done
cp "$root/LICENSE" "$pkg/"

# Strip, and check that nothing beyond the platform's base system libraries is linked dynamically.
case "$(uname -s)" in
  Linux)
    strip "$pkg"/bin/capnp "$pkg"/bin/capnpc-*
    for f in "$pkg"/bin/capnp "$pkg"/bin/capnpc-*; do
      if readelf -d "$f" | grep -q NEEDED; then
        echo "error: $f is dynamically linked:" >&2
        readelf -d "$f" | grep NEEDED >&2
        exit 1
      fi
    done
    ;;
  Darwin)
    strip "$pkg"/bin/capnp "$pkg"/bin/capnpc-*
    for f in "$pkg"/bin/capnp "$pkg"/bin/capnpc-*; do
      if otool -L "$f" | grep -E '^\s' | grep -v -E '^\s+(/usr/lib/|/System/)'; then
        echo "error: $f links a non-system library (above)" >&2
        exit 1
      fi
    done
    ;;
esac

"$root/.github/newtype/smoke-test.sh" "$pkg"

if [ "$windows" = 1 ]; then
  (cd "$work" && 7z a -tzip "$name.zip" "$name" >/dev/null)
  echo "Created $pkg.zip"
else
  tar -C "$work" -czf "$pkg.tar.gz" "$name"
  echo "Created $pkg.tar.gz"
fi

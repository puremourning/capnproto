#!/bin/sh
# Builds fully static Linux tools against musl. Runs inside an Alpine container, e.g.:
#
#   docker run --rm -v "$PWD":/src -w /src alpine:3.23 .github/newtype/build-linux-static.sh
#
# The resulting binaries have no dynamic dependencies, so they run on any Linux distribution of
# the same architecture (including RHEL/UBI) and in FROM scratch images.

set -eu

apk add --no-cache bash build-base cmake ninja linux-headers >/dev/null

exec bash "$(dirname "$0")/build-tools.sh" "linux-$(uname -m)" \
  -G Ninja \
  -DCMAKE_EXE_LINKER_FLAGS=-static

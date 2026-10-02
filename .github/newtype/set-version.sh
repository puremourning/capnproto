#!/usr/bin/env bash
# Stamps a newtype build version (e.g. "2.0-dev-newtype.0123456789ab") into the CMake and
# autotools build files, and prints it. The version appears in `capnp --version`, the source
# tarball name and the pkg-config/CMake package metadata.
#
# Usage: set-version.sh

set -euo pipefail
cd "$(dirname "$0")/../.."

sha=$(git rev-parse --short=12 HEAD)
base=$(sed -n -e 's/^set(VERSION \(.*\))$/\1/p' c++/CMakeLists.txt)
base=${base%%-newtype.*}  # in case the files were already stamped
version="${base}-newtype.${sha}"

sed -i.bak -e "s/^set(VERSION .*)$/set(VERSION ${version})/" c++/CMakeLists.txt
sed -i.bak -e "s/^AC_INIT(\[Capn Proto\],\[[^]]*\]/AC_INIT([Capn Proto],[${version}]/" c++/configure.ac
rm -f c++/CMakeLists.txt.bak c++/configure.ac.bak

grep -q "^set(VERSION ${version})$" c++/CMakeLists.txt
grep -q "^AC_INIT(\[Capn Proto\],\[${version}\]" c++/configure.ac

echo "$version"

#!/usr/bin/env bash
# Checks that a packaged tools tree works after being moved: capnp must find its own standard
# schemas and plugins without relying on the build prefix or $PATH.
#
# Usage: smoke-test.sh <tools-tree>

set -euo pipefail

pkg=$1

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) windows=1 ;;
  *) windows=0 ;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cp -R "$pkg" "$tmp/moved"
capnp=$tmp/moved/bin/capnp

# capnp reports paths with symlinks resolved (e.g. /var -> /private/var on macOS) and in native
# form on Windows.
expected=$(cd "$tmp/moved/include" && pwd -P)
if [ "$windows" = 1 ]; then
  expected=$(cygpath -w "$expected")
fi

check_include_dir() {
  local actual
  actual=$("$1" config --include-dir | tr -d '\r')
  if [ "$windows" = 1 ]; then
    # Drive letter case may differ.
    actual=${actual,,}
    local want=${expected,,}
  else
    local want=$expected
  fi
  if [ "$actual" != "$want" ]; then
    echo "error: $1 config --include-dir: expected '$want', got '$actual'" >&2
    exit 1
  fi
}

"$capnp" --version
"$capnp" config
check_include_dir "$capnp"

if [ "$windows" = 0 ]; then
  ln -s "$capnp" "$tmp/capnp-link"
  check_include_dir "$tmp/capnp-link"
fi

mkdir "$tmp/work"
cd "$tmp/work"
cat > test.capnp <<'EOF'
@0xbf5147cbbecf40c1;
using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("smoke");
struct Test { x @0 :UInt32; }
EOF

# The plugins must be found next to capnp, not via $PATH.
env PATH=/nonexistent "$capnp" compile -oc++ -ocapnp test.capnp
test -s test.capnp.h
test -s test.capnp.c++
grep -q 'namespace smoke' test.capnp.h

echo "Smoke test passed"

#! /bin/sh
# Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
# Licensed under the MIT License:
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# Tests the `capnp` tool's various commands, other than `compile`.

set -eu

fail() {
  echo "FAILED: $@" >&2
  exit 1
}

if test -f ./capnp; then
  CAPNP=${CAPNP:-./capnp}
elif test -f ./capnp.exe; then
  CAPNP=${CAPNP:-./capnp.exe}
else
  CAPNP=${CAPNP:-capnp}
fi

# Don't let the caller's environment add import paths to the tests below.
unset CAPNP_INCLUDE

SCHEMA=`dirname "$0"`/../test.capnp
JSON_SCHEMA=`dirname "$0"`/../compat/json-test.capnp
TESTDATA=`dirname "$0"`/../testdata
SRCDIR=`dirname "$0"`/../..

SUFFIX=${TESTDATA#*/src/}
PREFIX=${TESTDATA%${SUFFIX}}

if [ "$PREFIX" = "" ]; then
  PREFIX=.
fi

# ========================================================================================
# convert

$CAPNP convert text:binary $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/binary - || fail encode
$CAPNP convert text:flat $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/flat - || fail encode flat
$CAPNP convert text:packed $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/packed - || fail encode packed
$CAPNP convert text:flat-packed $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/packedflat - || fail encode packedflat
$CAPNP convert text:binary $SCHEMA TestAllTypes < $TESTDATA/pretty.txt | cmp $TESTDATA/binary - || fail parse pretty

$CAPNP convert binary:text $SCHEMA TestAllTypes < $TESTDATA/binary | cmp $TESTDATA/pretty.txt - || fail decode
$CAPNP convert flat:text $SCHEMA TestAllTypes < $TESTDATA/flat | cmp $TESTDATA/pretty.txt - || fail decode flat
$CAPNP convert packed:text $SCHEMA TestAllTypes < $TESTDATA/packed | cmp $TESTDATA/pretty.txt - || fail decode packed
$CAPNP convert flat-packed:text $SCHEMA TestAllTypes < $TESTDATA/packedflat | cmp $TESTDATA/pretty.txt - || fail decode packedflat
$CAPNP convert binary:text --short $SCHEMA TestAllTypes < $TESTDATA/binary | cmp $TESTDATA/short.txt - || fail decode short

$CAPNP convert binary:text $SCHEMA TestAllTypes < $TESTDATA/segmented | cmp $TESTDATA/pretty.txt - || fail decode segmented
$CAPNP convert packed:text $SCHEMA TestAllTypes < $TESTDATA/segmented-packed | cmp $TESTDATA/pretty.txt - || fail decode segmented-packed

$CAPNP convert binary:packed < $TESTDATA/binary | cmp $TESTDATA/packed - || fail binary to packed
$CAPNP convert packed:binary < $TESTDATA/packed | cmp $TESTDATA/binary - || fail packed to binary

$CAPNP convert binary:json $SCHEMA TestAllTypes < $TESTDATA/binary | cmp $TESTDATA/pretty.json - || fail binary to json
$CAPNP convert binary:json --short $SCHEMA TestAllTypes < $TESTDATA/binary | cmp $TESTDATA/short.json - || fail binary to short json

$CAPNP convert json:binary $SCHEMA TestAllTypes < $TESTDATA/pretty.json | cmp $TESTDATA/binary - || fail json to binary
$CAPNP convert json:binary $SCHEMA TestAllTypes < $TESTDATA/short.json | cmp $TESTDATA/binary - || fail short json to binary

$CAPNP convert json:binary $JSON_SCHEMA TestJsonAnnotations -I"$SRCDIR" < $TESTDATA/annotated.json | cmp $TESTDATA/annotated-json.binary - || fail annotated json to binary
$CAPNP convert binary:json $JSON_SCHEMA TestJsonAnnotations -I"$SRCDIR" < $TESTDATA/annotated-json.binary | cmp $TESTDATA/annotated.json - || fail annotated binary to json

[ "$(echo '(foo = (text = "abc"))' | $CAPNP convert text:text "$SRCDIR/capnp/test.capnp" BrandedAlias)" = '(foo = (text = "abc"), uv = void)' ]  || fail branded alias
[ "$(echo '(foo = (text = "abc"))' | $CAPNP convert text:text "$SRCDIR/capnp/test.capnp" BrandedAlias.Inner)" = '(foo = (text = "abc"))' ]  || fail branded alias
[ "$(echo '(foo = (text = "abc"))' | $CAPNP convert text:text "$SRCDIR/capnp/test.capnp" 'TestGenerics(BoxedText, Text)')" = '(foo = (text = "abc"), uv = void)' ]  || fail branded alias
[ "$(echo '(baz = (text = "abc"))' | $CAPNP convert text:text "$SRCDIR/capnp/test.capnp" 'TestGenerics(TestAllTypes, List(Int32)).Inner2(BoxedText)')" = '(baz = (text = "abc"))' ]  || fail branded alias

# ========================================================================================
# DEPRECATED encode/decode

$CAPNP encode $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/binary - || fail encode
$CAPNP encode --flat $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/flat - || fail encode flat
$CAPNP encode --packed $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/packed - || fail encode packed
$CAPNP encode --packed --flat $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/packedflat - || fail encode packedflat
$CAPNP encode $SCHEMA TestAllTypes < $TESTDATA/pretty.txt | cmp $TESTDATA/binary - || fail parse pretty

$CAPNP decode $SCHEMA TestAllTypes < $TESTDATA/binary | cmp $TESTDATA/pretty.txt - || fail decode
$CAPNP decode --flat $SCHEMA TestAllTypes < $TESTDATA/flat | cmp $TESTDATA/pretty.txt - || fail decode flat
$CAPNP decode --packed $SCHEMA TestAllTypes < $TESTDATA/packed | cmp $TESTDATA/pretty.txt - || fail decode packed
$CAPNP decode --packed --flat $SCHEMA TestAllTypes < $TESTDATA/packedflat | cmp $TESTDATA/pretty.txt - || fail decode packedflat
$CAPNP decode --short $SCHEMA TestAllTypes < $TESTDATA/binary | cmp $TESTDATA/short.txt - || fail decode short

$CAPNP decode $SCHEMA TestAllTypes < $TESTDATA/segmented | cmp $TESTDATA/pretty.txt - || fail decode segmented
$CAPNP decode --packed $SCHEMA TestAllTypes < $TESTDATA/segmented-packed | cmp $TESTDATA/pretty.txt - || fail decode segmented-packed

# ========================================================================================
# eval

test_eval() {
  test "x`$CAPNP eval $SCHEMA $1 | tr -d '\r'`" = "x$2" || fail eval "$1 == $2"
}

test_eval TestDefaults.uInt32Field 3456789012
test_eval TestDefaults.structField.textField '"baz"'
test_eval TestDefaults.int8List "[111, -111]"
test_eval 'TestDefaults.structList[1].textField' '"structlist 2"'
test_eval globalPrintableStruct '(someText = "foo")'
test_eval TestConstants.enumConst corge
test_eval 'TestListDefaults.lists.int32ListList[2][0]' 12341234

test "x`$CAPNP eval $SCHEMA -ojson globalPrintableStruct | tr -d '\r'`" = "x{\"someText\": \"foo\"}" || fail eval json "globalPrintableStruct == {someText = \"foo\"}"

$CAPNP eval $TESTDATA/no-file-id.capnp.nobuild foo >/dev/null || fail eval "file without file ID can be parsed"
test "x`$CAPNP eval $TESTDATA/no-file-id.capnp.nobuild foo | tr -d '\r'`" = 'x"bar"' || fail eval "file without file ID parsed correctly"

$CAPNP compile --no-standard-import --src-prefix="$PREFIX" -ofoo $TESTDATA/errors.capnp.nobuild 2>&1 | sed -e "s,^.*errors[.]capnp[.]nobuild:,file:,g" | tr -d '\r' |
    diff -u $TESTDATA/errors.txt - || fail error output

$CAPNP compile --no-standard-import --src-prefix="$PREFIX" -ofoo $TESTDATA/errors2.capnp.nobuild 2>&1 | sed -e "s,^.*errors2[.]capnp[.]nobuild:,file:,g" | tr -d '\r' |
    diff -u $TESTDATA/errors2.txt - || fail error2 output

# An incomplete `@[...]` mapping (fewer ordinals than the inline newtype has fields) is allowed,
# but must warn about the unmapped trailing field(s) and still compile successfully (exit 0).
INCOMPLETE_WARN=$($CAPNP compile --no-standard-import --src-prefix="$PREFIX" -o- $TESTDATA/incomplete-mapping.capnp.nobuild 2>&1 >/dev/null) || fail "incomplete mapping should compile"
echo "$INCOMPLETE_WARN" | grep -q "warning: .*unmapped" || fail "incomplete mapping should warn about unmapped fields"

# Over-mapping (more ordinals than the inline newtype has fields) is a clean error.
$CAPNP compile --no-standard-import --src-prefix="$PREFIX" -o- $TESTDATA/newtype-over-mapping.capnp.nobuild 2>&1 | grep -q "maps 4 ordinals, but the type has only 3" || fail "over-mapping should error"

# Under-mapping a union below two members is a clean error, NOT an internal validation assert.
$CAPNP compile --no-standard-import --src-prefix="$PREFIX" -o- $TESTDATA/newtype-union-undermap.capnp.nobuild 2>&1 | grep -q "union needs at least two" || fail "union under-mapping should error cleanly"

# Under-mapping a union so that a trailing arm is wholly unmapped drops that arm and still
# compiles (with the usual warning), NOT an internal validation assert.
DROP_ARM_WARN=$($CAPNP compile --no-standard-import --src-prefix="$PREFIX" -o- $TESTDATA/newtype-union-drop-arm.capnp.nobuild 2>&1 >/dev/null) || fail "union arm drop should compile"
echo "$DROP_ARM_WARN" | grep -q "warning: .*unmapped" || fail "union arm drop should warn about unmapped fields"

# CAPNP_INCLUDE directories are searched, in order, before -I directories, even with
# --no-standard-import. Empty and nonexistent entries are ignored.
INCLUDE_DIR=`mktemp -d`
mkdir -p "$INCLUDE_DIR/a/lib" "$INCLUDE_DIR/b/lib"
printf '@0xa1b2c3d4e5f60001;\nconst which :Text = "a";\n' > "$INCLUDE_DIR/a/lib/x.capnp"
printf '@0xa1b2c3d4e5f60001;\nconst which :Text = "b";\n' > "$INCLUDE_DIR/b/lib/x.capnp"
printf '@0xa1b2c3d4e5f60002;\nconst v :Text = import "/lib/x.capnp".which;\n' > "$INCLUDE_DIR/m.capnp"
case "$CAPNP" in *.exe) SEP=";" ;; *) SEP=":" ;; esac
test "x`CAPNP_INCLUDE="$INCLUDE_DIR/a" $CAPNP eval --no-standard-import -I"$INCLUDE_DIR/b" "$INCLUDE_DIR/m.capnp" v | tr -d '\r'`" = 'x"a"' ||
    fail "CAPNP_INCLUDE should be searched before -I"
test "x`CAPNP_INCLUDE="$SEP$INCLUDE_DIR/missing$SEP$INCLUDE_DIR/b$SEP$INCLUDE_DIR/a$SEP" $CAPNP eval --no-standard-import "$INCLUDE_DIR/m.capnp" v | tr -d '\r'`" = 'x"b"' ||
    fail "CAPNP_INCLUDE should be searched in order, ignoring empty and missing entries"
if test "$SEP" = ":"; then  # Windows prints native paths, which won't match the shell's
  test "x`CAPNP_INCLUDE="$INCLUDE_DIR/missing$SEP$INCLUDE_DIR/b" $CAPNP config --import-paths | head -1`" = "x$INCLUDE_DIR/b" ||
      fail "config --import-paths should list existing CAPNP_INCLUDE directories first"
fi
rm -rf "$INCLUDE_DIR"

# capnpc-capnp output for schemas using newtypes must recompile, both natively and in v1
# compatibility mode (which expands newtypes away, so a v1 compiler can read it). This covers
# aliases of group/union newtypes (`type Bar = Foo`) and named unions carrying union-only
# annotations (printed as `name :union $ann {...}`).
if test -f ./capnpc-capnp; then
  CAPNPC_CAPNP=${CAPNPC_CAPNP:-./capnpc-capnp}
elif test -f ./capnpc-capnp.exe; then
  CAPNPC_CAPNP=${CAPNPC_CAPNP:-./capnpc-capnp.exe}
else
  CAPNPC_CAPNP=${CAPNPC_CAPNP:-capnpc-capnp}
fi
REGEN_DIR=`mktemp -d`
for compat in "" 1; do
  for f in capnp/c++.capnp capnp/compat/json.capnp capnp/test-newtype-import.capnp \
           capnp/test-newtype.capnp capnp/compat/json-test.capnp; do
    mkdir -p "$REGEN_DIR/m$compat/`dirname $f`"
    CAPNPC_CAPNP_COMPAT_VERSION=$compat $CAPNP compile --src-prefix="$SRCDIR" -I"$SRCDIR" \
        -o"$CAPNPC_CAPNP" "$SRCDIR/$f" 2>/dev/null | sed 1d > "$REGEN_DIR/m$compat/$f" ||
        fail "capnpc-capnp $f (compat='$compat')"
  done
  for f in capnp/test-newtype.capnp capnp/compat/json-test.capnp; do
    $CAPNP compile --no-standard-import -I"$REGEN_DIR/m$compat" -o- "$REGEN_DIR/m$compat/$f" \
        > /dev/null || fail "capnpc-capnp output for $f does not recompile (compat='$compat')"
  done
done
! grep -q '^type \|@\[' "$REGEN_DIR/m1/capnp/test-newtype.capnp" ||
    fail "capnpc-capnp v1 compat output still contains newtype syntax"
rm -rf "$REGEN_DIR"

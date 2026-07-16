# Copyright (c) 2024 the Cap'n Proto authors and contributors
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

# `type` newtypes defined here and imported by test-newtype.capnp, exercising cross-file use of
# scalar aliases, group wrappers, and union wrappers. A newtype's `type` node and (for groups) its
# template live in this file; importers must still pull them into their generated code.

@0xe1f0a2b3c4d5e6f7;

using Cxx = import "c++.capnp";
$Cxx.namespace("capnproto_test::capnp::newtype");

type ImportedId = Data;              # scalar pointer newtype used cross-file
type ImportedAge = UInt16;           # scalar value newtype used cross-file

type ImportedVec = group {           # group newtype used cross-file -> wrapper in this file
  x @0 :Float32;
  y @1 :Float32;
  z @2 :Float32;
}

type ImportedChoice = union {        # union newtype used cross-file
  none @0 :Void;
  code @1 :Int32;
  label @2 :Text;
}

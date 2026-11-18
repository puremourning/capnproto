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

# Version 1 of an evolving schema, paired with test-newtype-v2.capnp. Both are compiled into one
# test binary (in different C++ namespaces) so newtype-test.c++ can hand a use-site table generated
# from one version to a carrier (wrapper) generated from the other.

@0xd28cc9b147368611;

using Cxx = import "c++.capnp";
$Cxx.namespace("capnproto_test::capnp::newtype::v1");

type Vec3 = group {
  x @0 :Float32;
  y @1 :Float32;
  z @2 :Float32;
}

# The nested-evolution reproducer (see test-newtype-v2.capnp).
type Rect = group {
  tl @[0-2] :Vec3;
  br @[3-5] :Vec3;
}

struct S {
  r @[0-5] :Rect;
  tag @6 :UInt32;
}

# Further evolutions: a nested newtype gaining a member, and a single-level newtype gaining a field.
type Box = group {
  tl @[0-2] :Vec3;
  br @[3-5] :Vec3;
}

struct Evolving {
  r @[0-5] :Box;
  tag @6 :UInt32;
  p @[7-9] :Vec3;
}

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

# Version 2 of test-newtype-v1.capnp, a legal evolution of it: Vec3 gains `w` (appended), and
# each newtype built from it maps `w` (or not) at new ordinals, as commented below.

@0xf602beb1a620ccce;

using Cxx = import "c++.capnp";
$Cxx.namespace("capnproto_test::capnp::newtype::v2");

type Vec3 = group {
  x @0 :Float32;
  y @1 :Float32;
  z @2 :Float32;
  w @3 :Float32;
}

# Rect maps the new `w` at new ordinals 6 and 7; S maps those at 7 and 8. So at S's use site
# tl = S ordinals 0,1,2,7 and br = 3,4,5,8: the nested leaves are not contiguous.
type Rect = group {
  tl @[0-2, 6] :Vec3;
  br @[3-5, 7] :Vec3;
}

struct S {
  r @[0-5, 7, 8] :Rect;
  tag @6 :UInt32;
}

# Box leaves `w` unmapped in its existing members (an incomplete mapping, so the compiler warns)
# and gains a new member `mid`; Evolving maps Box's new ordinals 6-9 at 10-13, and p.w at 14.
type Box = group {
  tl @[0-2] :Vec3;
  br @[3-5] :Vec3;
  mid @[6-9] :Vec3;
}

struct Evolving {
  r @[0-5, 10-13] :Box;
  tag @6 :UInt32;
  p @[7-9, 14] :Vec3;
}

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

# Schema exercising `type` newtypes and inline group stamping, used by newtype-test.c++ to check
# the generated wrappers. NOTE: `partial` maps only two of Vec3's three fields on purpose -- the
# compiler is expected to emit an "unmapped" warning for it at generation time.

@0xd9a76358b3648f94;

using Cxx = import "c++.capnp";
$Cxx.namespace("capnproto_test::capnp::newtype");

using Import = import "test-newtype-import.capnp";

type Uuid = Data;                    # scalar newtype -> `using`
type Age = UInt16;                   # scalar newtype -> `using`

type Vec3 = group {                  # group newtype -> offset-parametrized wrapper
  x @0 :Float32;
  y @1 :Float32;
  z @2 :Float32;
}

type Price = group {
  value @0 :Int64;
  scale @1 :UInt16;
}

type Priced = group {                # a field with an explicit default
  amount @0 :Int64;
  scale @1 :UInt16 = 100;
}

type Named = group {                 # Text/Data pointer fields (mixed with a data field)
  label @0 :Text;
  count @1 :Int32;
}

struct Coord { lat @0 :Float64; lng @1 :Float64; }

type Boxed = group {                 # struct + list + text + data fields
  at @0 :Coord;
  tags @1 :List(Int32);
  note @2 :Text;
  id @3 :Int32;
}

type OrderPrices = group {           # newtype built from other newtypes -> nested wrapper
  limit @[0-1] :Price;
  stop @[2-3] :Price;
}

type OrderType = union {             # union newtype -> wrapper with which()/isX()/getX()
  limit @[0-1] :Price;               # group arm (nested newtype)
  market @2 :Void;                   # void arm
  cancel @3 :Int32;                  # data arm
}

struct Shapes {
  id @0 :Uuid;
  age @1 :Age;
  topLeft @[2-4] :Vec3;              # complete mapping
  bottomRight @[5-7] :Vec3;
  partial @[8-9] :Vec3;              # incomplete: z is unmapped -> reads default
  prices @[10, 11, 12, 13] :OrderPrices;
  priced @[14, 15] :Priced;          # explicit-default field, complete mapping
  pricedPartial @[16] :Priced;       # incomplete: scale unmapped -> reads its explicit default
  named @[17, 18] :Named;            # Text pointer field + data field
  boxed @[19, 20, 21, 22] :Boxed;    # struct + list + text + data fields
  order @[23, 24, 25, 26] :OrderType;  # union newtype
}

struct CrossFile {
  # Newtypes imported from test-newtype-import.capnp: their `type` node and template live in the
  # other file, so this exercises the compiler pulling cross-file newtype nodes into the request.
  id @0 :Import.ImportedId;             # scalar pointer newtype -> ImportedId::Reader
  age @1 :Import.ImportedAge;           # scalar value newtype -> ImportedAge
  corner @[2, 3, 4] :Import.ImportedVec;   # group newtype -> ImportedVec::Reader<...>
  choice @[5, 6, 7] :Import.ImportedChoice;  # union newtype
}

struct PlaceParams {
  # A method's parameters are an ordinary struct, so group/union newtypes reach a method through a
  # named parameter struct like this one. (The `@[...]` ordinal-mapping syntax is only valid on
  # struct fields, not inside an inline `(...)` parameter list.)
  spot @[0-2] :Vec3;                    # group newtype in a request slot
  tag @3 :Uuid;                         # scalar pointer newtype in a request slot
}

struct PlaceResults {
  echo @[0-2] :Vec3;                    # group newtype in a response slot
  kind @[3, 4, 5, 6] :OrderType;        # union newtype in a response slot
}

interface Registry {
  # Newtypes in interface method parameter/result slots. Scalar newtypes may appear directly in an
  # inline parameter list; group/union newtypes come in via the named structs above.
  lookup @0 (id :Uuid, age :Age) -> (foundId :Uuid, foundAge :Age);
  place @1 PlaceParams -> PlaceResults;
}

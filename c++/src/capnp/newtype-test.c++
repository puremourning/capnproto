// Copyright (c) 2024 the Cap'n Proto authors and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <capnp/test-newtype.capnp.h>
#include "message.h"
#include <kj/compat/gtest.h>

namespace capnproto_test {
namespace capnp {
namespace newtype {
namespace {

TEST(Newtype, ScalarUsingAliases) {
  // Uuid == ::capnp::Data and Age == uint16_t via generated `using` aliases; these lines only
  // compile if the aliases exist and name the underlying types.
  ::capnp::MallocMessageBuilder message;
  auto shapes = message.initRoot<Shapes>();
  shapes.setAge(42);
  Age age = shapes.getAge();
  EXPECT_EQ(42, age);
  Uuid::Reader id = shapes.getId();
  EXPECT_EQ(0u, id.size());
}

TEST(Newtype, GroupWrapperRoundTrip) {
  ::capnp::MallocMessageBuilder message;
  auto shapes = message.initRoot<Shapes>();
  auto tl = shapes.getTopLeft();          // Vec3::Builder<...>
  tl.setX(1.5f); tl.setY(2.5f); tl.setZ(3.5f);
  auto br = shapes.getBottomRight();
  br.setX(4.5f); br.setY(5.5f); br.setZ(6.5f);

  auto r = shapes.asReader();
  EXPECT_EQ(1.5f, r.getTopLeft().getX());
  EXPECT_EQ(2.5f, r.getTopLeft().getY());
  EXPECT_EQ(3.5f, r.getTopLeft().getZ());
  // The two Vec3 use sites occupy distinct words -- no aliasing between topLeft and bottomRight.
  EXPECT_EQ(4.5f, r.getBottomRight().getX());
  EXPECT_EQ(6.5f, r.getBottomRight().getZ());
}

TEST(Newtype, IncompleteMappingReadsDefault) {
  // `partial @[8-9] :Vec3` maps only x and y; z is unmapped and must read its default (and, at
  // compile time, `setZ` on this instance is a static_assert -- so it isn't called here).
  ::capnp::MallocMessageBuilder message;
  auto shapes = message.initRoot<Shapes>();
  auto p = shapes.getPartial();
  p.setX(7.5f); p.setY(8.5f);

  auto r = shapes.asReader();
  EXPECT_EQ(7.5f, r.getPartial().getX());
  EXPECT_EQ(8.5f, r.getPartial().getY());
  EXPECT_EQ(0.0f, r.getPartial().getZ());   // unmapped -> default
}

TEST(Newtype, NestedWrapperRoundTrip) {
  // OrderPrices is a newtype built from two Price newtypes; getLimit()/getStop() return Price
  // wrappers slicing OrderPrices's offset pack.
  ::capnp::MallocMessageBuilder message;
  auto shapes = message.initRoot<Shapes>();
  auto prices = shapes.getPrices();       // OrderPrices::Builder<...>
  prices.getLimit().setValue(100);        // getLimit() -> Price::Builder<...>
  prices.getLimit().setScale(2);
  prices.getStop().setValue(500);
  prices.getStop().setScale(5);

  auto r = shapes.asReader();
  auto rp = r.getPrices();
  EXPECT_EQ(100, rp.getLimit().getValue());
  EXPECT_EQ(2, rp.getLimit().getScale());
  EXPECT_EQ(500, rp.getStop().getValue());   // no aliasing with limit
  EXPECT_EQ(5, rp.getStop().getScale());
}

TEST(Newtype, AnyReaderErasure) {
  // asAny() erases the templated wrapper to one concrete AnyReader that composes across use sites.
  ::capnp::MallocMessageBuilder message;
  auto shapes = message.initRoot<Shapes>();
  shapes.getTopLeft().setX(1.5f);
  shapes.getBottomRight().setZ(9.5f);
  shapes.getPrices().getLimit().setValue(100);

  auto r = shapes.asReader();
  Vec3::AnyReader a = r.getTopLeft().asAny();
  Vec3::AnyReader b = r.getBottomRight().asAny();
  EXPECT_EQ(1.5f, a.getX());
  EXPECT_EQ(9.5f, b.getZ());

  OrderPrices::AnyReader pricesAny = r.getPrices().asAny();   // nested erasure
  EXPECT_EQ(100, pricesAny.getLimit().getValue());
}

}  // namespace
}  // namespace newtype
}  // namespace capnp
}  // namespace capnproto_test

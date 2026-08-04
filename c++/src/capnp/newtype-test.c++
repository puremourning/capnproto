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

#include "capnp/dynamic.h"
#include "capnp/test-newtype.capnp.h"
#include "message.h"
#include <kj/compat/gtest.h>

#if !CAPNP_LITE
#include <capnp/capability.h>
#include <kj/async.h>
#endif  // !CAPNP_LITE

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

// Ids of test-newtype.capnp's `voidAnno` and `structAnno`. Annotation nodes generate no C++
// declaration, so their ids are spelled out here; they are derived from the file id plus the
// annotation name, and can be re-read with `capnp compile -ocapnp capnp/test-newtype.capnp`.
static constexpr uint64_t VOID_ANNO_ID = 0xd3a27aab171949d7ull;
static constexpr uint64_t STRUCT_ANNO_ID = 0xba3806704b92bc7cull;

TEST(Newtype, NewTypeAnnotationsArePropagatedToTheirStampings) {
  // `type Uuid = Data $structAnno(width=10, name="Hello") $voidAnno;` -- a field declared as
  // `id @0 :Uuid` inherits the newtype's annotations, and a struct-valued annotation must arrive
  // with its *contents*, not as an empty struct.
  auto field = ::capnp::Schema::from<Shapes>().getFieldByName("id");

  bool sawVoidAnno = false;
  kj::Maybe<::capnp::schema::Value::Reader> structAnnoValue;
  for (auto anno: field.getProto().getAnnotations()) {
    if (anno.getId() == VOID_ANNO_ID) sawVoidAnno = true;
    if (anno.getId() == STRUCT_ANNO_ID) structAnnoValue = anno.getValue();
  }

  // Both annotations propagate...
  EXPECT_TRUE(sawVoidAnno);
  auto value = KJ_ASSERT_NONNULL(structAnnoValue, "structAnno did not propagate to Shapes.id");
  ASSERT_TRUE(value.isStruct());

  // ...and the struct-valued one carries its contents, not an empty struct.  (A struct value is
  // only interpreted once every node has a bootstrap schema, so a naive copy out of the newtype's
  // bootstrap schema would pick up the placeholder that stands in for it until then.)
  auto annoStruct = value.getStruct().as<StructAnno>();
  EXPECT_TRUE(annoStruct.hasName());
  EXPECT_EQ(10, annoStruct.getWidth());
  EXPECT_TRUE(annoStruct.getName() == "Hello");
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

TEST(Newtype, ExplicitDefaults) {
  // `Priced.scale` has an explicit default of 100. An unset-but-mapped scale reads it (via the
  // load mask), an unmapped scale reads it too (via unmask), and setting round-trips.
  ::capnp::MallocMessageBuilder message;
  auto shapes = message.initRoot<Shapes>();
  shapes.getPriced().setAmount(7);            // scale left unset
  auto r = shapes.asReader();
  EXPECT_EQ(7, r.getPriced().getAmount());
  EXPECT_EQ(100, r.getPriced().getScale());          // mapped-but-unset -> default via mask
  EXPECT_EQ(100, r.getPricedPartial().getScale());   // unmapped -> default via unmask

  shapes.getPriced().setScale(9);
  EXPECT_EQ(9, shapes.asReader().getPriced().getScale());
}

TEST(Newtype, PointerFields) {
  // A group newtype with Text/Data pointer fields gets wrapper accessors (get/set/init/has).
  ::capnp::MallocMessageBuilder message;
  auto shapes = message.initRoot<Shapes>();
  auto named = shapes.getNamed();
  named.setLabel("widget");
  named.setCount(5);
  auto r = shapes.asReader().getNamed();
  EXPECT_TRUE(r.getLabel() == "widget");
  EXPECT_TRUE(r.hasLabel());
  EXPECT_EQ(5, r.getCount());
}

TEST(Newtype, StructListPointerFields) {
  // A group newtype with struct, list, text and data fields -- all wrapped (via out-of-lined
  // accessor definitions), including init/get/set/has and asAny() erasure.
  ::capnp::MallocMessageBuilder message;
  auto shapes = message.initRoot<Shapes>();
  auto boxed = shapes.getBoxed();
  auto at = boxed.initAt();                     // struct field
  at.setLat(51.5);
  at.setLng(-0.1);
  auto tags = boxed.initTags(2);                // list field
  tags.set(0, 10); tags.set(1, 20);
  boxed.setNote("hello");                       // text field
  boxed.setId(99);                              // data field

  auto r = shapes.asReader().getBoxed();
  EXPECT_EQ(51.5, r.getAt().getLat());
  EXPECT_EQ(-0.1, r.getAt().getLng());
  EXPECT_EQ(2u, r.getTags().size());
  EXPECT_EQ(20, r.getTags()[1]);
  EXPECT_TRUE(r.getNote() == "hello");
  EXPECT_EQ(99, r.getId());
  EXPECT_TRUE(r.hasAt() && r.hasTags() && r.hasNote());

  auto any = r.asAny();                          // erased form works for pointer-field wrappers too
  EXPECT_EQ(99, any.getId());
  EXPECT_TRUE(any.getNote() == "hello");
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

TEST(Newtype, UnionWrapper) {
  // A union newtype gets an offset-parametrized wrapper with which()/isX()/getX(); arms include a
  // nested group newtype (limit -> Price), a void arm (market) and a data arm (cancel).
  ::capnp::MallocMessageBuilder message;
  auto shapes = message.initRoot<Shapes>();
  auto order = shapes.getOrder();               // OrderType::Builder<...>

  auto limit = order.initLimit();               // selects LIMIT arm, returns Price::Builder<...>
  limit.setValue(100);
  limit.setScale(2);
  {
    auto r = shapes.asReader().getOrder();
    EXPECT_EQ(OrderType::LIMIT, r.which());
    EXPECT_TRUE(r.isLimit());
    EXPECT_EQ(100, r.getLimit().getValue());    // getLimit() -> Price wrapper
    EXPECT_EQ(2, r.getLimit().getScale());
  }

  order.setCancel(7);                           // switch to the CANCEL data arm
  {
    auto r = shapes.asReader().getOrder();
    EXPECT_EQ(OrderType::CANCEL, r.which());
    EXPECT_TRUE(r.isCancel());
    EXPECT_FALSE(r.isLimit());
    EXPECT_EQ(7, r.getCancel());
  }

  order.setMarket(::capnp::VOID);               // switch to the MARKET void arm
  EXPECT_EQ(OrderType::MARKET, shapes.asReader().getOrder().which());
}

TEST(Newtype, CrossFileNewtypes) {
  // The newtypes here are defined in test-newtype-import.capnp. This only compiles/links if the
  // compiler pulled their `type` nodes (and, for groups, their templates) into this file's request.
  ::capnp::MallocMessageBuilder message;
  auto cf = message.initRoot<CrossFile>();

  // Scalar newtypes keep their imported names in signatures.
  cf.setAge(77);
  ImportedAge age = cf.getAge();
  EXPECT_EQ(77, age);
  ImportedId::Reader id = cf.asReader().getId();
  EXPECT_EQ(0u, id.size());

  // Group newtype wrapper works cross-file.
  auto corner = cf.getCorner();                 // ImportedVec::Builder<2, 3, 4>
  corner.setX(1.25f); corner.setY(2.5f); corner.setZ(3.75f);
  auto r = cf.asReader();
  EXPECT_EQ(1.25f, r.getCorner().getX());
  EXPECT_EQ(2.5f, r.getCorner().getY());
  EXPECT_EQ(3.75f, r.getCorner().getZ());
  ImportedVec::AnyReader anyCorner = r.getCorner().asAny();   // erasure works cross-file too
  EXPECT_EQ(3.75f, anyCorner.getZ());

  // Union newtype wrapper works cross-file.
  cf.getChoice().setCode(-5);
  {
    auto c = cf.asReader().getChoice();
    EXPECT_EQ(ImportedChoice::CODE, c.which());
    EXPECT_TRUE(c.isCode());
    EXPECT_EQ(-5, c.getCode());
  }
  cf.getChoice().setNone(::capnp::VOID);
  EXPECT_EQ(ImportedChoice::NONE, cf.asReader().getChoice().which());
}

#if !CAPNP_LITE

class RegistryImpl final: public Registry::Server {
  // Reads newtype-typed parameters and writes newtype-typed results. The parameter/result accessors
  // naming the newtypes (Uuid::Reader, Age, Vec3::Builder<...>, OrderType::Builder<...>) only
  // compile if the newtype codegen reaches a method's implicit parameter/result structs.
public:
  kj::Promise<void> lookup(LookupContext context) override {
    auto params = context.getParams();
    Uuid::Reader id = params.getId();          // scalar pointer newtype parameter
    Age age = params.getAge();                 // scalar value newtype parameter
    auto results = context.getResults();
    results.setFoundId(id);                    // echo the Uuid straight back
    results.setFoundAge(age + 1);              // Age is uint16_t: arithmetic uses the underlying type
    return kj::READY_NOW;
  }

  kj::Promise<void> place(PlaceContext context) override {
    auto params = context.getParams();
    auto spot = params.getSpot();              // Vec3::Reader<...> group newtype parameter
    auto results = context.getResults();
    auto echo = results.initEcho();            // Vec3::Builder<...> group newtype result
    echo.setX(spot.getX() * 2);
    echo.setY(spot.getY() * 2);
    echo.setZ(spot.getZ() * 2);
    results.initKind().setCancel(99);          // union newtype result
    return kj::READY_NOW;
  }
};

TEST(Newtype, CapabilityParamsAndResults) {
  // Newtypes in interface method parameter/result slots, exercised through a real capability call:
  // the client marshals newtype parameters into the request, the server reads them and writes
  // newtype results, and the client reads the results back.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Registry::Client client(kj::heap<RegistryImpl>());

  // Scalar newtypes (Uuid = Data, Age = UInt16) in an inline parameter/result list.
  static const kj::byte idBytes[] = { 0x01, 0x02, 0x03, 0x04 };
  auto lreq = client.lookupRequest();
  lreq.setId(Uuid::Reader(idBytes, sizeof(idBytes)));
  lreq.setAge(41);
  auto lresp = lreq.send().wait(waitScope);
  EXPECT_EQ(4u, lresp.getFoundId().size());   // Uuid round-tripped through the call
  EXPECT_EQ(0x03, lresp.getFoundId()[2]);
  Age foundAge = lresp.getFoundAge();
  EXPECT_EQ(42, foundAge);

  // Group + union newtypes via named parameter/result structs.
  auto preq = client.placeRequest();
  auto spot = preq.initSpot();                // Vec3::Builder<...>
  spot.setX(1.0f); spot.setY(2.0f); spot.setZ(3.0f);
  preq.setTag(Uuid::Reader(idBytes, sizeof(idBytes)));
  auto presp = preq.send().wait(waitScope);
  auto echo = presp.getEcho();                // Vec3::Reader<...>
  EXPECT_EQ(2.0f, echo.getX());
  EXPECT_EQ(4.0f, echo.getY());
  EXPECT_EQ(6.0f, echo.getZ());
  auto kind = presp.getKind();                // OrderType::Reader<...>
  EXPECT_EQ(OrderType::CANCEL, kind.which());
  EXPECT_TRUE(kind.isCancel());
  EXPECT_EQ(99, kind.getCancel());
}

#endif  // !CAPNP_LITE

}  // namespace
}  // namespace newtype
}  // namespace capnp
}  // namespace capnproto_test

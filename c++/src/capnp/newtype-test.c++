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
#include "capnp/test-newtype-v1.capnp.h"
#include "capnp/test-newtype-v2.capnp.h"
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

kj::Maybe<kj::StringPtr> groupAnno(::capnp::StructSchema::Field field) {
  // The value of `$groupAnno` on `field`, asserting it appears at most once (an alias's annotation
  // must replace, not duplicate, the one it overrides).
  kj::Maybe<kj::StringPtr> result;
  for (auto anno: field.getProto().getAnnotations()) {
    auto value = anno.getValue();
    if (value.isText()) {
      KJ_EXPECT(result == kj::none, "duplicate annotation", field.getProto().getName());
      result = value.getText();
    }
  }
  return result;
}

TEST(Newtype, AliasOfGroupOrUnionNewtype) {
  ::capnp::MallocMessageBuilder message;
  auto a = message.initRoot<Aliases>();

  // Aliases of group/union newtypes get the aliased newtype's wrapper; these only compile if
  // `Point::Builder<...>` etc. name Vec3's wrapper.
  auto corner = a.getCorner();
  corner.setX(1); corner.setY(2); corner.setZ(3);
  auto late = a.getLate();
  late.setZ(4);
  a.getChained().setY(5);
  a.getOverridden().setX(6);
  a.getImported().setZ(7);
  a.getOrder().setCancel(8);
  auto segment = a.getSegment();
  segment.getFrom().setX(9);
  segment.getTo().setZ(10);

  auto r = a.asReader();
  Point::AnyReader anyCorner = r.getCorner().asAny();
  Vec3::AnyReader sameType = anyCorner;  // Point is Vec3
  EXPECT_EQ(1, sameType.getX());
  EXPECT_EQ(2, r.getCorner().getY());
  EXPECT_EQ(3, r.getCorner().getZ());
  EXPECT_EQ(4, r.getLate().getZ());
  EXPECT_EQ(5, r.getChained().getY());
  EXPECT_EQ(6, r.getOverridden().getX());
  EXPECT_EQ(7, r.getImported().getZ());
  EXPECT_EQ(OrderKind::CANCEL, r.getOrder().which());
  EXPECT_EQ(8, r.getOrder().getCancel());
  EXPECT_EQ(9, r.getSegment().getFrom().getX());
  EXPECT_EQ(10, r.getSegment().getTo().getZ());
  // Distinct use sites don't overlap.
  EXPECT_EQ(0, r.getChained().getX());
  EXPECT_EQ(0, r.getSegment().getTo().getX());

  // Each use site records the newtype it named (the alias), not the newtype it aliases.
  auto schema = ::capnp::Schema::from<Aliases>();
  auto typeIdOf = [&](kj::StringPtr name) {
    return schema.getFieldByName(name).getProto().getTypeId();
  };
  EXPECT_NE(0u, typeIdOf("corner"));
  EXPECT_NE(typeIdOf("corner"), typeIdOf("late"));
  EXPECT_NE(typeIdOf("corner"), typeIdOf("chained"));
  EXPECT_EQ(typeIdOf("chained"), typeIdOf("overridden"));

  // Annotations merge along the chain: nearest wins.
  EXPECT_EQ(groupAnno(schema.getFieldByName("corner")), kj::StringPtr("point"));
  EXPECT_EQ(groupAnno(schema.getFieldByName("chained")), kj::StringPtr("chained"));
  EXPECT_EQ(groupAnno(schema.getFieldByName("overridden")), kj::StringPtr("use-site"));
  EXPECT_TRUE(groupAnno(schema.getFieldByName("late")) == kj::none);
}

// ---------------------------------------------------------------------------------------
// Schema evolution: use-site tables vs carriers generated from different schema versions.
//
// test-newtype-v1.capnp and test-newtype-v2.capnp are two versions of one schema, generated into
// the namespaces v1 and v2. Each use site's layout table (::capnp::_::NewtypeTable) is per level
// and indexed by template field position, so a v1 carrier can read through a v2 table (extra
// entries ignored) and a v2 carrier through a v1 table (missing entries read as unmapped).

using ::capnp::_::NewtypeTable;
using ::capnp::_::NEWTYPE_UNMAPPED;

template <typename T> struct TableOf;
template <const NewtypeTable* t> struct TableOf<v1::Rect::Reader<t>> { static constexpr auto value = t; };
template <const NewtypeTable* t> struct TableOf<v2::Rect::Reader<t>> { static constexpr auto value = t; };
template <const NewtypeTable* t> struct TableOf<v1::Box::Reader<t>> { static constexpr auto value = t; };
template <const NewtypeTable* t> struct TableOf<v2::Box::Reader<t>> { static constexpr auto value = t; };

TEST(Newtype, EvolutionV1CarrierOverV2Table) {
  ::capnp::MallocMessageBuilder message;
  auto e = message.initRoot<v2::Evolving>();
  auto tl = e.getR().getTl(); tl.setX(1); tl.setY(2); tl.setZ(3);
  auto br = e.getR().getBr(); br.setX(4); br.setY(5); br.setZ(6);
  auto mid = e.getR().getMid(); mid.setX(7); mid.setY(8); mid.setZ(9); mid.setW(10);
  e.setTag(77);
  auto p = e.getP(); p.setX(11); p.setY(12); p.setZ(13); p.setW(14);

  auto er = e.asReader();
  auto sr = ::capnp::_::PointerHelpers<v2::Evolving>::getInternalReader(er);
  const NewtypeTable* rTable = er.getR().asAny().getTable();
  const NewtypeTable* pTable = er.getP().asAny().getTable();
  EXPECT_EQ(3u, rTable->size);               // v2 Box has three members; v1 knows two

  // Erased v1 carriers over the v2 tables: the fields v1 knows read correctly; mid and w are
  // simply invisible to v1.
  v1::Box::AnyReader r1(sr, rTable);
  EXPECT_EQ(1, r1.getTl().getX());
  EXPECT_EQ(2, r1.getTl().getY());
  EXPECT_EQ(3, r1.getTl().getZ());
  EXPECT_EQ(4, r1.getBr().getX());
  EXPECT_EQ(5, r1.getBr().getY());
  EXPECT_EQ(6, r1.getBr().getZ());
  v1::Vec3::AnyReader p1(sr, pTable);
  EXPECT_EQ(11, p1.getX());
  EXPECT_EQ(13, p1.getZ());

  // The zero-cost v1 carrier, instantiated on the v2 use site's table.
  constexpr const NewtypeTable* R = TableOf<decltype(er.getR())>::value;
  v1::Box::Reader<R> rt(sr);
  EXPECT_EQ(3, rt.getTl().getZ());
  EXPECT_EQ(4, rt.getBr().getX());
  EXPECT_EQ(6, rt.getBr().getZ());

  // Writing through v1 carriers lands where v2 reads it.
  auto sb = ::capnp::_::PointerHelpers<v2::Evolving>::getInternalBuilder(
      message.getRoot<v2::Evolving>());
  v1::Box::AnyBuilder b1(sb, rTable);
  b1.getBr().setY(42);
  v1::Box::Builder<R>(sb).getTl().setX(43);
  EXPECT_EQ(42, e.asReader().getR().getBr().getY());
  EXPECT_EQ(43, e.asReader().getR().getTl().getX());
  EXPECT_EQ(9, e.asReader().getR().getMid().getZ());   // untouched
  EXPECT_EQ(77u, e.asReader().getTag());
}

TEST(Newtype, EvolutionV2CarrierOverV1Table) {
  ::capnp::MallocMessageBuilder message;
  auto e = message.initRoot<v1::Evolving>();
  auto tl = e.getR().getTl(); tl.setX(1); tl.setY(2); tl.setZ(3);
  auto br = e.getR().getBr(); br.setX(4); br.setY(5); br.setZ(6);
  e.setTag(77);
  auto p = e.getP(); p.setX(11); p.setY(12); p.setZ(13);

  auto er = e.asReader();
  auto sr = ::capnp::_::PointerHelpers<v1::Evolving>::getInternalReader(er);
  const NewtypeTable* rTable = er.getR().asAny().getTable();
  const NewtypeTable* pTable = er.getP().asAny().getTable();
  EXPECT_EQ(2u, rTable->size);
  EXPECT_EQ(3u, pTable->size);

  // Erased v2 carriers over the (shorter) v1 tables: known fields read correctly; fields past the
  // end of a table -- Vec3.w, Box.mid -- read as their defaults instead of running off the end.
  v2::Box::AnyReader r2(sr, rTable);
  EXPECT_EQ(1, r2.getTl().getX());
  EXPECT_EQ(3, r2.getTl().getZ());
  EXPECT_EQ(0, r2.getTl().getW());
  EXPECT_EQ(4, r2.getBr().getX());
  EXPECT_EQ(6, r2.getBr().getZ());
  EXPECT_EQ(0, r2.getBr().getW());
  EXPECT_EQ(0, r2.getMid().getX());
  EXPECT_EQ(0, r2.getMid().getW());
  v2::Vec3::AnyReader p2(sr, pTable);
  EXPECT_EQ(12, p2.getY());
  EXPECT_EQ(0, p2.getW());

  // The zero-cost v2 carrier on the v1 table folds the missing fields to their defaults.
  constexpr const NewtypeTable* R = TableOf<decltype(er.getR())>::value;
  v2::Box::Reader<R> rt(sr);
  EXPECT_EQ(2, rt.getTl().getY());
  EXPECT_EQ(0, rt.getTl().getW());
  EXPECT_EQ(5, rt.getBr().getY());
  EXPECT_EQ(0, rt.getMid().getY());
  static_assert(R->child(2) == &::capnp::_::EMPTY_NEWTYPE_TABLE);
  static_assert(R->child(0)->offset(3) == NEWTYPE_UNMAPPED);

  // Setting a field the table lacks is an error; mapped fields still write.
  auto sb = ::capnp::_::PointerHelpers<v1::Evolving>::getInternalBuilder(
      message.getRoot<v1::Evolving>());
  v2::Box::AnyBuilder b2(sb, rTable);
  KJ_EXPECT_THROW_MESSAGE("not mapped", b2.getTl().setW(1));
  KJ_EXPECT_THROW_MESSAGE("not mapped", b2.getMid().setX(1));
  v2::Vec3::AnyBuilder pb2(sb, pTable);
  KJ_EXPECT_THROW_MESSAGE("not mapped", pb2.setW(1));
  b2.getBr().setZ(60);
  pb2.setX(110);
  EXPECT_EQ(60, e.asReader().getR().getBr().getZ());
  EXPECT_EQ(110, e.asReader().getP().getX());
  EXPECT_EQ(77u, e.asReader().getTag());
}

TEST(Newtype, EvolutionNestedRect) {
  // The nested reproducer: v2's Vec3 gains `w`, which Rect maps at its new ordinals 6 and 7 and S
  // at 7 and 8 -- so at S.r, tl is S ordinals 0,1,2,7 and br is 3,4,5,8. A flat tree-ordered table
  // ([0,1,2,7,3,4,5,8]) sliced by v1's Rect (br = [3..6]) would read br from ordinals 7,3,4.
  {
    // v1 carriers over the v2 use site.
    ::capnp::MallocMessageBuilder message;
    auto s = message.initRoot<v2::S>();
    auto tl = s.getR().getTl(); tl.setX(1); tl.setY(2); tl.setZ(3); tl.setW(4);
    auto br = s.getR().getBr(); br.setX(5); br.setY(6); br.setZ(7); br.setW(8);
    s.setTag(77);

    auto sr = ::capnp::_::PointerHelpers<v2::S>::getInternalReader(s.asReader());
    auto sb = ::capnp::_::PointerHelpers<v2::S>::getInternalBuilder(message.getRoot<v2::S>());
    EXPECT_EQ(4, sr.getDataField<float>(::capnp::bounded<7>() * ::capnp::ELEMENTS));  // tl.w
    EXPECT_EQ(8, sr.getDataField<float>(::capnp::bounded<8>() * ::capnp::ELEMENTS));  // br.w
    const NewtypeTable* table = s.asReader().getR().asAny().getTable();
    // The generated per-level table: Rect's members by template position, each with its own table.
    ASSERT_EQ(2u, table->size);
    EXPECT_EQ(4u, table->child(0)->size);
    EXPECT_EQ(7u, table->child(0)->offset(3));   // tl.w
    EXPECT_EQ(3u, table->child(1)->offset(0));   // br.x
    EXPECT_EQ(8u, table->child(1)->offset(3));   // br.w

    v1::Rect::AnyReader r1(sr, table);
    EXPECT_EQ(1, r1.getTl().getX());
    EXPECT_EQ(2, r1.getTl().getY());
    EXPECT_EQ(3, r1.getTl().getZ());
    EXPECT_EQ(5, r1.getBr().getX());
    EXPECT_EQ(6, r1.getBr().getY());
    EXPECT_EQ(7, r1.getBr().getZ());
    constexpr const NewtypeTable* R = TableOf<decltype(s.asReader().getR())>::value;
    v1::Rect::Reader<R> rt(sr);
    EXPECT_EQ(5, rt.getBr().getX());
    EXPECT_EQ(7, rt.getBr().getZ());

    v1::Rect::AnyBuilder b1(sb, table);
    b1.getBr().setX(50);
    EXPECT_EQ(50, s.asReader().getR().getBr().getX());
    EXPECT_EQ(8, s.asReader().getR().getBr().getW());   // untouched
    EXPECT_EQ(4, s.asReader().getR().getTl().getW());
    EXPECT_EQ(77u, s.asReader().getTag());
  }
  {
    // v2 carriers over the v1 use site: w is absent from v1's tables.
    ::capnp::MallocMessageBuilder message;
    auto s = message.initRoot<v1::S>();
    auto tl = s.getR().getTl(); tl.setX(1); tl.setY(2); tl.setZ(3);
    auto br = s.getR().getBr(); br.setX(5); br.setY(6); br.setZ(7);
    s.setTag(77);

    auto sr = ::capnp::_::PointerHelpers<v1::S>::getInternalReader(s.asReader());
    auto sb = ::capnp::_::PointerHelpers<v1::S>::getInternalBuilder(message.getRoot<v1::S>());
    const NewtypeTable* table = s.asReader().getR().asAny().getTable();

    v2::Rect::AnyReader r2(sr, table);
    EXPECT_EQ(1, r2.getTl().getX());
    EXPECT_EQ(3, r2.getTl().getZ());
    EXPECT_EQ(0, r2.getTl().getW());
    EXPECT_EQ(5, r2.getBr().getX());
    EXPECT_EQ(7, r2.getBr().getZ());
    EXPECT_EQ(0, r2.getBr().getW());
    constexpr const NewtypeTable* R = TableOf<decltype(s.asReader().getR())>::value;
    v2::Rect::Reader<R> rt(sr);
    EXPECT_EQ(6, rt.getBr().getY());
    EXPECT_EQ(0, rt.getBr().getW());

    v2::Rect::AnyBuilder b2(sb, table);
    KJ_EXPECT_THROW_MESSAGE("not mapped", b2.getTl().setW(1));
    KJ_EXPECT_THROW_MESSAGE("not mapped", b2.getBr().setW(1));
    b2.getBr().setY(60);
    EXPECT_EQ(60, s.asReader().getR().getBr().getY());
    EXPECT_EQ(77u, s.asReader().getTag());
  }
}

// The same nested evolution as EvolutionNestedRect above, with the per-level tables built by hand,
// so the carriers are checked independently of the compiler's stamping.
constexpr NewtypeTable::Entry HAND_TL[] = {{0, nullptr}, {1, nullptr}, {2, nullptr}, {7, nullptr}};
constexpr NewtypeTable::Entry HAND_BR[] = {{3, nullptr}, {4, nullptr}, {5, nullptr}, {8, nullptr}};
constexpr NewtypeTable HAND_TL_TABLE = {HAND_TL, 4, NEWTYPE_UNMAPPED};
constexpr NewtypeTable HAND_BR_TABLE = {HAND_BR, 4, NEWTYPE_UNMAPPED};
constexpr NewtypeTable::Entry HAND_RECT[] = {
  {NEWTYPE_UNMAPPED, &HAND_TL_TABLE}, {NEWTYPE_UNMAPPED, &HAND_BR_TABLE}};
constexpr NewtypeTable HAND_RECT_TABLE = {HAND_RECT, 2, NEWTYPE_UNMAPPED};

TEST(Newtype, EvolutionNestedHandBuiltTables) {
  ::capnp::MallocMessageBuilder message;
  message.initRoot<v2::S>();   // any struct with >= 9 Float32 data slots
  auto sb = ::capnp::_::PointerHelpers<v2::S>::getInternalBuilder(message.getRoot<v2::S>());
  v2::Rect::AnyBuilder b2(sb, &HAND_RECT_TABLE);
  b2.getTl().setX(1); b2.getTl().setY(2); b2.getTl().setZ(3); b2.getTl().setW(4);
  b2.getBr().setX(5); b2.getBr().setY(6); b2.getBr().setZ(7); b2.getBr().setW(8);

  auto sr = sb.asReader();
  EXPECT_EQ(4, sr.getDataField<float>(::capnp::bounded<7>() * ::capnp::ELEMENTS));  // tl.w
  EXPECT_EQ(5, sr.getDataField<float>(::capnp::bounded<3>() * ::capnp::ELEMENTS));  // br.x

  v1::Rect::AnyReader r1(sr, &HAND_RECT_TABLE);
  EXPECT_EQ(1, r1.getTl().getX());
  EXPECT_EQ(3, r1.getTl().getZ());
  EXPECT_EQ(5, r1.getBr().getX());
  EXPECT_EQ(6, r1.getBr().getY());
  EXPECT_EQ(7, r1.getBr().getZ());
  v1::Rect::Reader<&HAND_RECT_TABLE> rt1(sr);
  EXPECT_EQ(5, rt1.getBr().getX());
  EXPECT_EQ(7, rt1.getBr().getZ());
  v2::Rect::Reader<&HAND_RECT_TABLE> rt2(sr);
  EXPECT_EQ(4, rt2.getTl().getW());
  EXPECT_EQ(8, rt2.getBr().getW());
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

// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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

#define CAPNP_TESTING_CAPNP 1

#include "schema-parser.h"
#include <kj/compat/gtest.h>
#include "test-util.h"
#include <kj/debug.h>
#include <map>

namespace capnp {
namespace {

#if _WIN32
#define ABS(x) "C:\\" x
#else
#define ABS(x) "/" x
#endif

class FakeFileReader final: public kj::Filesystem {
public:
  void add(kj::StringPtr name, kj::StringPtr content) {
    root->openFile(cwd.evalNative(name), kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT)
        ->writeAll(content);
  }

  const kj::Directory& getRoot() const override { return *root; }
  const kj::Directory& getCurrent() const override { return *current; }
  kj::PathPtr getCurrentPath() const override { return cwd; }

private:
  kj::Own<const kj::Directory> root = kj::newInMemoryDirectory(kj::nullClock());
  kj::Path cwd = kj::Path({}).evalNative(ABS("path/to/current/dir"));
  kj::Own<const kj::Directory> current = root->openSubdir(cwd,
      kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT);
};

static uint64_t getFieldTypeFileId(StructSchema::Field field) {
  return field.getContainingStruct()
      .getDependency(field.getProto().getSlot().getType().getStruct().getTypeId())
      .getProto().getScopeId();
}

TEST(SchemaParser, Basic) {
  FakeFileReader reader;
  SchemaParser parser;
  parser.setDiskFilesystem(reader);

  reader.add("src/foo/bar.capnp",
      "@0x8123456789abcdef;\n"
      "struct Bar {\n"
      "  baz @0: import \"baz.capnp\".Baz;\n"
      "  corge @1: import \"../qux/corge.capnp\".Corge;\n"
      "  grault @2: import \"/grault.capnp\".Grault;\n"
      "  garply @3: import \"/garply.capnp\".Garply;\n"
      "}\n");
  reader.add("src/foo/baz.capnp",
      "@0x823456789abcdef1;\n"
      "struct Baz {}\n");
  reader.add("src/qux/corge.capnp",
      "@0x83456789abcdef12;\n"
      "struct Corge {}\n");
  reader.add(ABS("usr/include/grault.capnp"),
      "@0x8456789abcdef123;\n"
      "struct Grault {}\n");
  reader.add(ABS("opt/include/grault.capnp"),
      "@0x8000000000000001;\n"
      "struct WrongGrault {}\n");
  reader.add(ABS("usr/local/include/garply.capnp"),
      "@0x856789abcdef1234;\n"
      "struct Garply {}\n");

  kj::StringPtr importPath[] = {
    ABS("usr/include"), ABS("usr/local/include"), ABS("opt/include")
  };

  ParsedSchema barSchema = parser.parseDiskFile(
      "foo2/bar2.capnp", "src/foo/bar.capnp", importPath);

  auto barProto = barSchema.getProto();
  EXPECT_EQ(0x8123456789abcdefull, barProto.getId());
  EXPECT_EQ("foo2/bar2.capnp", barProto.getDisplayName());

  auto barStruct = barSchema.getNested("Bar");
  auto barFields = barStruct.asStruct().getFields();
  ASSERT_EQ(4u, barFields.size());
  EXPECT_EQ("baz", barFields[0].getProto().getName());
  EXPECT_EQ(0x823456789abcdef1ull, getFieldTypeFileId(barFields[0]));
  EXPECT_EQ("corge", barFields[1].getProto().getName());
  EXPECT_EQ(0x83456789abcdef12ull, getFieldTypeFileId(barFields[1]));
  EXPECT_EQ("grault", barFields[2].getProto().getName());
  EXPECT_EQ(0x8456789abcdef123ull, getFieldTypeFileId(barFields[2]));
  EXPECT_EQ("garply", barFields[3].getProto().getName());
  EXPECT_EQ(0x856789abcdef1234ull, getFieldTypeFileId(barFields[3]));

  auto barStructs = barSchema.getAllNested();
  ASSERT_EQ(1, barStructs.size());
  EXPECT_EQ("Bar", barStructs[0].getUnqualifiedName());
  barFields = barStructs[0].asStruct().getFields();
  ASSERT_EQ(4u, barFields.size());
  EXPECT_EQ("baz", barFields[0].getProto().getName());
  EXPECT_EQ(0x823456789abcdef1ull, getFieldTypeFileId(barFields[0]));

  auto bazSchema = parser.parseDiskFile(
      "not/used/because/already/loaded",
      "src/foo/baz.capnp", importPath);
  EXPECT_EQ(0x823456789abcdef1ull, bazSchema.getProto().getId());
  EXPECT_EQ("foo2/baz.capnp", bazSchema.getProto().getDisplayName());
  auto bazStruct = bazSchema.getNested("Baz").asStruct();
  EXPECT_EQ(bazStruct, barStruct.getDependency(bazStruct.getProto().getId()));

  auto corgeSchema = parser.parseDiskFile(
      "not/used/because/already/loaded",
      "src/qux/corge.capnp", importPath);
  EXPECT_EQ(0x83456789abcdef12ull, corgeSchema.getProto().getId());
  EXPECT_EQ("qux/corge.capnp", corgeSchema.getProto().getDisplayName());
  auto corgeStruct = corgeSchema.getNested("Corge").asStruct();
  EXPECT_EQ(corgeStruct, barStruct.getDependency(corgeStruct.getProto().getId()));

  auto graultSchema = parser.parseDiskFile(
      "not/used/because/already/loaded",
      ABS("usr/include/grault.capnp"), importPath);
  EXPECT_EQ(0x8456789abcdef123ull, graultSchema.getProto().getId());
  EXPECT_EQ("grault.capnp", graultSchema.getProto().getDisplayName());
  auto graultStruct = graultSchema.getNested("Grault").asStruct();
  EXPECT_EQ(graultStruct, barStruct.getDependency(graultStruct.getProto().getId()));

  // Try importing the other grault.capnp directly.  It'll get the display name we specify since
  // it wasn't imported before.
  auto wrongGraultSchema = parser.parseDiskFile(
      "weird/display/name.capnp",
      ABS("opt/include/grault.capnp"), importPath);
  EXPECT_EQ(0x8000000000000001ull, wrongGraultSchema.getProto().getId());
  EXPECT_EQ("weird/display/name.capnp", wrongGraultSchema.getProto().getDisplayName());
}

TEST(SchemaParser, TypeDeclarationResolvesTransparently) {
  // A `type X = <expr>` declaration behaves like a named `using`: every use of the newtype
  // resolves to the underlying type in the schema (transparently, even when chained through
  // another `type`).
  FakeFileReader reader;
  SchemaParser parser;
  parser.setDiskFilesystem(reader);

  reader.add("newtype.capnp",
      "@0x8123456789abce01;\n"
      "type Uuid = Data;\n"
      "type ProviderId = Uuid;\n"
      "struct S {\n"
      "  providerId @0 :ProviderId;\n"
      "  raw @1 :Uuid;\n"
      "  name @2 :Text;\n"
      "}\n");

  ParsedSchema fileSchema = parser.parseDiskFile(
      "newtype.capnp", "newtype.capnp", nullptr);

  auto fields = fileSchema.getNested("S").asStruct().getFields();
  auto providerIdType = fields[0].getProto().getSlot().getType();
  auto rawType = fields[1].getProto().getSlot().getType();
  auto nameType = fields[2].getProto().getSlot().getType();
  EXPECT_EQ(schema::Type::DATA, providerIdType.which());  // ProviderId -> Uuid -> Data
  EXPECT_EQ(schema::Type::DATA, rawType.which());          // Uuid -> Data
  EXPECT_EQ(schema::Type::TEXT, nameType.which());         // plain Text

  // The `type` declarations survive into the schema as TYPE nodes.
  auto uuidNode = fileSchema.getNested("Uuid").getProto();
  auto providerIdNode = fileSchema.getNested("ProviderId").getProto();
  EXPECT_EQ(schema::Node::TYPE, uuidNode.which());
  EXPECT_EQ(schema::Node::TYPE, providerIdNode.which());

  // Each use records a `typeId` back-reference to the newtype it was written as (the outermost
  // name at the use site), while the wire type stays the underlying type.
  EXPECT_EQ(providerIdNode.getId(), providerIdType.getTypeId());
  EXPECT_EQ(uuidNode.getId(), rawType.getTypeId());
  EXPECT_EQ(0u, nameType.getTypeId());

  // A TYPE node records its underlying type, chaining the back-reference through intermediate
  // newtypes: ProviderId -> Uuid -> Data.
  EXPECT_EQ(schema::Type::DATA, uuidNode.getType().which());
  EXPECT_EQ(0u, uuidNode.getType().getTypeId());
  EXPECT_EQ(schema::Type::DATA, providerIdNode.getType().which());
  EXPECT_EQ(uuidNode.getId(), providerIdNode.getType().getTypeId());
}

TEST(SchemaParser, TypeNewtypeAnnotationMerge) {
  // A field written as a `type` newtype inherits that newtype's (field-scoped) annotations,
  // merged through the newtype chain, with use-site annotations overriding by annotation ID.
  FakeFileReader reader;
  SchemaParser parser;
  parser.setDiskFilesystem(reader);

  reader.add("ann.capnp",
      "@0x8123456789abce02;\n"
      "annotation hex(field) :Void;\n"
      "annotation len(field) :UInt32;\n"
      "annotation pii(field) :Void;\n"
      "type Uuid = Data $hex $len(16);\n"
      "type ProviderId = Uuid $pii;\n"
      "struct S {\n"
      "  id @0 :Uuid;                  # -> hex, len(16)\n"
      "  owner @1 :ProviderId;         # -> pii, hex, len(16) (inherited through chain)\n"
      "  override @2 :Uuid $len(32);   # -> len(32) [use-site wins], hex\n"
      "  plain @3 :Data;               # -> (none)\n"
      "}\n");

  ParsedSchema fileSchema = parser.parseDiskFile("ann.capnp", "ann.capnp", nullptr);

  uint64_t hexId = fileSchema.getNested("hex").getProto().getId();
  uint64_t lenId = fileSchema.getNested("len").getProto().getId();
  uint64_t piiId = fileSchema.getNested("pii").getProto().getId();

  auto fields = fileSchema.getNested("S").asStruct().getFields();

  auto hasAnn = [](StructSchema::Field f, uint64_t id) {
    for (auto a: f.getProto().getAnnotations()) {
      if (a.getId() == id) return true;
    }
    return false;
  };
  auto lenValue = [lenId](StructSchema::Field f) -> kj::Maybe<uint32_t> {
    for (auto a: f.getProto().getAnnotations()) {
      if (a.getId() == lenId) return a.getValue().getUint32();
    }
    return nullptr;
  };

  // id :Uuid -> hex, len(16)
  EXPECT_TRUE(hasAnn(fields[0], hexId));
  EXPECT_EQ(16u, KJ_ASSERT_NONNULL(lenValue(fields[0])));
  EXPECT_FALSE(hasAnn(fields[0], piiId));

  // owner :ProviderId -> pii (from ProviderId) + hex, len(16) (inherited from Uuid).
  EXPECT_TRUE(hasAnn(fields[1], piiId));
  EXPECT_TRUE(hasAnn(fields[1], hexId));
  EXPECT_EQ(16u, KJ_ASSERT_NONNULL(lenValue(fields[1])));

  // override :Uuid $len(32) -> use-site len(32) overrides the newtype's len(16); hex inherited.
  EXPECT_EQ(32u, KJ_ASSERT_NONNULL(lenValue(fields[2])));
  EXPECT_TRUE(hasAnn(fields[2], hexId));

  // plain :Data -> no annotations at all.
  EXPECT_EQ(0u, fields[3].getProto().getAnnotations().size());
}

TEST(SchemaParser, InlineGroupNewtypeStamp) {
  // A field written `@[...] :Vec3` where `type Vec3 = group {...}` stamps the newtype's fields
  // inline into the parent struct's data space (no pointer), remapping ordinals, and records
  // Field.typeId = the newtype on each stamped group field.
  FakeFileReader reader;
  SchemaParser parser;
  parser.setDiskFilesystem(reader);

  reader.add("stamp.capnp",
      "@0x8123456789abce03;\n"
      "type Vec3 = group { x @0 :Float32; y @1 :Float32; z @2 :Float32; }\n"
      "struct Rectangle {\n"
      "  topLeft @[0-2] :Vec3;\n"
      "  bottomRight @[3-5] :Vec3;\n"
      "}\n");

  ParsedSchema fileSchema = parser.parseDiskFile("stamp.capnp", "stamp.capnp", nullptr);

  uint64_t vec3Id = fileSchema.getNested("Vec3").getProto().getId();
  auto rect = fileSchema.getNested("Rectangle").asStruct();

  // Six floats packed inline: 3 data words, no pointers.
  EXPECT_EQ(3u, rect.getProto().getStruct().getDataWordCount());
  EXPECT_EQ(0u, rect.getProto().getStruct().getPointerCount());

  auto fields = rect.getFields();
  ASSERT_EQ(2u, fields.size());
  auto tl = fields[0].getProto();
  auto br = fields[1].getProto();

  // Both are group fields whose Field.typeId back-references the Vec3 newtype.
  EXPECT_TRUE(tl.isGroup());
  EXPECT_TRUE(br.isGroup());
  EXPECT_EQ(vec3Id, tl.getTypeId());
  EXPECT_EQ(vec3Id, br.getTypeId());

  // The per-instance group nodes stamp x/y/z at consecutive offsets in the parent.
  auto tlFields = rect.getDependency(tl.getGroup().getTypeId()).asStruct().getFields();
  auto brFields = rect.getDependency(br.getGroup().getTypeId()).asStruct().getFields();
  ASSERT_EQ(3u, tlFields.size());
  ASSERT_EQ(3u, brFields.size());
  for (uint i = 0; i < 3; i++) {
    EXPECT_EQ(i, tlFields[i].getProto().getSlot().getOffset());
    EXPECT_EQ(3u + i, brFields[i].getProto().getSlot().getOffset());
    EXPECT_EQ(schema::Type::FLOAT32, tlFields[i].getProto().getSlot().getType().which());
  }
}

TEST(SchemaParser, InlineUnionNewtypeStamp) {
  // A union newtype stamps inline like a group, but with a discriminant: the members overlap
  // and get discriminant values; the discriminant consumes an offset but no ordinal.
  FakeFileReader reader;
  SchemaParser parser;
  parser.setDiskFilesystem(reader);

  reader.add("ustamp.capnp",
      "@0x8123456789abce04;\n"
      "type Instruction = union { market @0 :Void; limit @1 :Float32; stop @2 :Float32; }\n"
      "struct Order {\n"
      "  id @0 :Int32;\n"
      "  instr @[1, 2, 3] :Instruction;\n"
      "}\n");

  ParsedSchema fileSchema = parser.parseDiskFile("ustamp.capnp", "ustamp.capnp", nullptr);
  uint64_t instrId = fileSchema.getNested("Instruction").getProto().getId();
  auto order = fileSchema.getNested("Order").asStruct();

  auto instr = order.getFieldByName("instr").getProto();
  EXPECT_TRUE(instr.isGroup());
  EXPECT_EQ(instrId, instr.getTypeId());  // Field.typeId back-reference to the newtype

  auto instrGroup = order.getDependency(instr.getGroup().getTypeId()).asStruct();
  EXPECT_EQ(3u, instrGroup.getProto().getStruct().getDiscriminantCount());

  auto gf = instrGroup.getFields();
  ASSERT_EQ(3u, gf.size());
  EXPECT_EQ(0u, gf[0].getProto().getDiscriminantValue());  // market
  EXPECT_EQ(1u, gf[1].getProto().getDiscriminantValue());  // limit
  EXPECT_EQ(2u, gf[2].getProto().getDiscriminantValue());  // stop
  // limit and stop are mutually exclusive -> they overlap at the same offset.
  EXPECT_EQ(gf[1].getProto().getSlot().getOffset(), gf[2].getProto().getSlot().getOffset());
}

TEST(SchemaParser, InlineNewtypePropagatesFieldProperties) {
  // A stamped leaf must carry the template field's default value, hadExplicitDefault flag and
  // annotations -- not only its type.
  FakeFileReader reader;
  SchemaParser parser;
  parser.setDiskFilesystem(reader);

  reader.add("prop.capnp",
      "@0x8123456789abce05;\n"
      "annotation label @0x9123456789abce06 (field) :Text;\n"
      "type X = group { foo @0 :UInt32 = 100 $label(\"f\"); bar @1 :Int32; }\n"
      "struct S { a @[0, 1] :X; }\n");

  ParsedSchema fileSchema = parser.parseDiskFile("prop.capnp", "prop.capnp", nullptr);
  auto s = fileSchema.getNested("S").asStruct();
  auto group = s.getFieldByName("a").getType().asStruct();  // this instance's group node
  auto foo = group.getFieldByName("foo").getProto();

  ASSERT_TRUE(foo.isSlot());
  EXPECT_TRUE(foo.getSlot().getHadExplicitDefault());
  EXPECT_EQ(100u, foo.getSlot().getDefaultValue().getUint32());
  ASSERT_EQ(1u, foo.getAnnotations().size());
  EXPECT_EQ("f", foo.getAnnotations()[0].getValue().getText());
}

TEST(SchemaParser, Constants) {
  // This is actually a test of the full dynamic API stack for constants, because the schemas for
  // constants are not actually accessible from the generated code API, so the only way to ever
  // get a ConstSchema is by parsing it.

  FakeFileReader reader;
  SchemaParser parser;
  parser.setDiskFilesystem(reader);

  reader.add("const.capnp",
      "@0x8123456789abcdef;\n"
      "const uint32Const :UInt32 = 1234;\n"
      "const listConst :List(Float32) = [1.25, 2.5, 3e4];\n"
      "const structConst :Foo = (bar = 123, baz = \"qux\");\n"
      "struct Foo {\n"
      "  bar @0 :Int16;\n"
      "  baz @1 :Text;\n"
      "}\n"
      "const genericConst :TestGeneric(Text) = (value = \"text\");\n"
      "struct TestGeneric(T) {\n"
      "  value @0 :T;\n"
      "}\n");

  ParsedSchema fileSchema = parser.parseDiskFile(
      "const.capnp", "const.capnp", nullptr);

  EXPECT_EQ(1234, fileSchema.getNested("uint32Const").asConst().as<uint32_t>());

  auto list = fileSchema.getNested("listConst").asConst().as<DynamicList>();
  ASSERT_EQ(3u, list.size());
  EXPECT_EQ(1.25, list[0].as<float>());
  EXPECT_EQ(2.5, list[1].as<float>());
  EXPECT_EQ(3e4f, list[2].as<float>());

  auto structConst = fileSchema.getNested("structConst").asConst().as<DynamicStruct>();
  EXPECT_EQ(123, structConst.get("bar").as<int16_t>());
  EXPECT_EQ("qux", structConst.get("baz").as<Text>());

  auto genericConst = fileSchema.getNested("genericConst").asConst().as<DynamicStruct>();
  EXPECT_EQ("text", genericConst.get("value").as<Text>());
}

void expectSourceInfo(schema::Node::SourceInfo::Reader sourceInfo,
                   uint64_t expectedId, kj::StringPtr expectedComment,
                   std::initializer_list<const kj::StringPtr> expectedMembers) {
  KJ_EXPECT(sourceInfo.getId() == expectedId, sourceInfo, expectedId);
  KJ_EXPECT(sourceInfo.getDocComment() == expectedComment, sourceInfo, expectedComment);

  auto members = sourceInfo.getMembers();
  KJ_ASSERT(members.size() == expectedMembers.size());
  for (auto i: kj::indices(expectedMembers)) {
    KJ_EXPECT(members[i].getDocComment() == expectedMembers.begin()[i],
              members[i], expectedMembers.begin()[i]);
  }
}

TEST(SchemaParser, SourceInfo) {
  FakeFileReader reader;
  SchemaParser parser;
  parser.setDiskFilesystem(reader);

  reader.add("foo.capnp",
      "@0x84a2c6051e1061ed;\n"
      "# file doc comment\n"
      "\n"
      "struct Foo @0xc6527d0a670dc4c3 {\n"
      "  # struct doc comment\n"
      "  # second line\n"
      "\n"
      "  bar @0 :UInt32;\n"
      "  # field doc comment\n"
      "  baz :group {\n"
      "    # group doc comment\n"
      "    qux @1 :Text;\n"
      "    # group field doc comment\n"
      "  }\n"
      "}\n"
      "\n"
      "enum Corge @0xae08878f1a016f14 {\n"
      "  # enum doc comment\n"
      "  grault @0;\n"
      "  # enumerant doc comment\n"
      "  garply @1;\n"
      "}\n"
      "\n"
      "interface Waldo @0xc0f1b0aff62b761e {\n"
      "  # interface doc comment\n"
      "  fred @0 (plugh :Int32) -> (xyzzy :Text);\n"
      "  # method doc comment\n"
      "}\n"
      "\n"
      "struct Thud @0xcca9972702b730b4 {}\n"
      "# post-comment\n");

  ParsedSchema file = parser.parseDiskFile(
      "foo.capnp", "foo.capnp", nullptr);
  ParsedSchema foo = file.getNested("Foo");

  expectSourceInfo(file.getSourceInfo(), 0x84a2c6051e1061edull, "file doc comment\n", {});

  expectSourceInfo(foo.getSourceInfo(), 0xc6527d0a670dc4c3ull, "struct doc comment\nsecond line\n",
      { "field doc comment\n", "group doc comment\n" });

  auto group = foo.asStruct().getFieldByName("baz").getType().asStruct();
  expectSourceInfo(KJ_ASSERT_NONNULL(parser.getSourceInfo(group)),
      group.getProto().getId(), "group doc comment\n", { "group field doc comment\n" });

  ParsedSchema corge = file.getNested("Corge");
  expectSourceInfo(corge.getSourceInfo(), 0xae08878f1a016f14, "enum doc comment\n",
      { "enumerant doc comment\n", "" });

  ParsedSchema waldo = file.getNested("Waldo");
  expectSourceInfo(waldo.getSourceInfo(), 0xc0f1b0aff62b761e, "interface doc comment\n",
      { "method doc comment\n" });

  ParsedSchema thud = file.getNested("Thud");
  expectSourceInfo(thud.getSourceInfo(), 0xcca9972702b730b4, "post-comment\n", {});
}

TEST(SchemaParser, SetFileIdsRequired) {
  FakeFileReader reader;
  reader.add("no-file-id.capnp",
      "const foo :Int32 = 123;\n");

  {
    SchemaParser parser;
    parser.setDiskFilesystem(reader);

    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("File does not declare an ID.",
        parser.parseDiskFile("no-file-id.capnp", "no-file-id.capnp", nullptr));
  }
  {
    SchemaParser parser;
    parser.setDiskFilesystem(reader);
    parser.setFileIdsRequired(false);

    auto fileSchema = parser.parseDiskFile("no-file-id.capnp", "no-file-id.capnp", nullptr);
    KJ_EXPECT(fileSchema.getNested("foo").asConst().as<int32_t>() == 123);
  }
}

}  // namespace
}  // namespace capnp

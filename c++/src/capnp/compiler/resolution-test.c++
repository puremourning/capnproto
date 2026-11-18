// Copyright (c) 2026 the Cap'n Proto authors and contributors
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

// Tests the identifier resolutions the compiler reports (which `capnp compile` emits as
// `CodeGeneratorRequest.RequestedFile.fileSourceInfo.identifiers`, for tools such as language
// servers). In particular, a name that resolves through a `type` newtype must report the newtype's
// own node -- not the type it wraps -- so that navigation lands on, and find-references finds
// uses of, the newtype.

#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include <capnp/compiler/lexer.capnp.h>
#include <capnp/message.h>
#include <kj/map.h>
#include <kj/test.h>

namespace capnp {
namespace compiler {
namespace {

class TestModule final: public Module {
public:
  TestModule(kj::StringPtr name, kj::StringPtr source)
      : name(name), source(source) {}

  void addImport(kj::StringPtr path, TestModule& module) { imports.insert(path, &module); }

  kj::StringPtr getSourceName() override { return name; }

  Orphan<ParsedFile> loadContent(Orphanage orphanage) override {
    MallocMessageBuilder lexedBuilder;
    auto statements = lexedBuilder.initRoot<LexedStatements>();
    lex(source.asArray(), statements, *this);
    auto parsed = orphanage.newOrphan<ParsedFile>();
    parseFile(statements.getStatements(), parsed.get(), *this, true);
    return parsed;
  }

  kj::Maybe<Module&> importRelative(kj::StringPtr importPath) override {
    KJ_IF_SOME(module, imports.find(importPath)) {
      return *module;
    }
    return kj::none;
  }

  kj::Maybe<kj::Array<const byte>> embedRelative(kj::StringPtr embedPath) override {
    return kj::none;
  }

  void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) override {
    KJ_FAIL_ASSERT("unexpected compile error", name, startByte, endByte, message);
  }

  bool hadErrors() override { return false; }

  void reportResolution(Resolution resolution) override { resolutions.add(resolution); }

  kj::ArrayPtr<const Resolution> getResolutions() override { return resolutions; }

  uint64_t typeIdAt(kj::StringPtr context, kj::StringPtr identifier) {
    // The type ID reported for the name `identifier`, written inside the (unique) snippet
    // `context`. A dotted name (`Lib.Uuid`) is reported once for the whole expression, which ends
    // where its last component does, so match on the end byte.
    auto contextStart = KJ_ASSERT_NONNULL(find(source, context), "context not in source", context);
    auto offset = KJ_ASSERT_NONNULL(find(context, identifier), "identifier not in context");
    uint32_t end = contextStart + offset + identifier.size();
    kj::Maybe<uint64_t> result;
    for (auto& resolution: resolutions) {
      if (resolution.endByte != end) continue;
      KJ_SWITCH_ONEOF(resolution.target) {
        KJ_CASE_ONEOF(type, Resolution::Type) {
          result = type.typeId;
        }
        KJ_CASE_ONEOF(member, Resolution::Member) {}
      }
    }
    return KJ_ASSERT_NONNULL(result, "no type resolution", context, identifier);
  }

private:
  kj::StringPtr name;
  kj::StringPtr source;
  kj::HashMap<kj::StringPtr, TestModule*> imports;
  kj::Vector<Resolution> resolutions;

  static kj::Maybe<size_t> find(kj::StringPtr haystack, kj::StringPtr needle) {
    for (size_t i = 0; i + needle.size() <= haystack.size(); i++) {
      if (haystack.slice(i, i + needle.size()) == needle) return i;
    }
    return kj::none;
  }
};

constexpr uint64_t MAIN_ID = 0xd4c5b3a2f1e0d9c8ull;
constexpr uint64_t LIB_ID = 0xd4c5b3a2f1e0d9c9ull;

KJ_TEST("identifiers that name a newtype resolve to the newtype, not its target") {
  TestModule lib("lib.capnp",
      "@0xd4c5b3a2f1e0d9c9;\n"
      "type Uuid = Data;\n");
  TestModule main("main.capnp",
      "@0xd4c5b3a2f1e0d9c8;\n"
      "using Lib = import \"lib.capnp\";\n"
      "type Age = UInt16;\n"
      "type Years = Age;\n"
      "type Point = group { x @0 :Int32; y @1 :Int32; }\n"
      "type Spot = Point;\n"
      "struct Outer { type Inner = UInt32; }\n"
      "struct Plain {}\n"
      "struct S {\n"
      "  age @0 :Age;\n"
      "  years @1 :Years;\n"
      "  absolute @2 : .Age;\n"  // `:.` would lex as one operator.

      "  inner @3 :Outer.Inner;\n"
      "  imported @4 :Lib.Uuid;\n"
      "  point @[5,6] :Point;\n"
      "  spot @[7,8] :Spot;\n"
      "  plain @9 :Plain;\n"
      "  raw @10 :UInt16;\n"
      "}\n");
  main.addImport("lib.capnp", lib);

  Compiler compiler;
  KJ_ASSERT(compiler.add(main).getId() == MAIN_ID);
  compiler.eagerlyCompile(MAIN_ID, Compiler::ALL_RELATED_NODES);

  // `Compiler::lookup()` already returns a newtype's own node; the reported resolutions must agree.
  auto id = [&](uint64_t parent, kj::StringPtr name) {
    return KJ_ASSERT_NONNULL(compiler.lookup(parent, name), name);
  };
  uint64_t age = id(MAIN_ID, "Age");
  uint64_t builtinUInt16 = main.typeIdAt("type Age = UInt16", "UInt16");
  KJ_ASSERT(age != builtinUInt16);

  // A newtype over a builtin, by relative and absolute name.
  KJ_EXPECT(main.typeIdAt("age @0 :Age", "Age") == age);
  KJ_EXPECT(main.typeIdAt("absolute @2 : .Age", "Age") == age);

  // A newtype of a newtype is its own node, and its target names the inner newtype.
  KJ_EXPECT(main.typeIdAt("years @1 :Years", "Years") == id(MAIN_ID, "Years"));
  KJ_EXPECT(main.typeIdAt("type Years = Age", "Age") == age);

  // Member access: a newtype nested in a struct, and one reached through an import.
  KJ_EXPECT(main.typeIdAt("inner @3 :Outer.Inner", "Outer.Inner") ==
            id(id(MAIN_ID, "Outer"), "Inner"));
  KJ_EXPECT(main.typeIdAt("imported @4 :Lib.Uuid", "Lib.Uuid") == id(LIB_ID, "Uuid"));

  // Group newtypes, and a newtype aliasing one.
  KJ_EXPECT(main.typeIdAt("point @[5,6] :Point", "Point") == id(MAIN_ID, "Point"));
  KJ_EXPECT(main.typeIdAt("spot @[7,8] :Spot", "Spot") == id(MAIN_ID, "Spot"));
  KJ_EXPECT(main.typeIdAt("type Spot = Point", "Point") == id(MAIN_ID, "Point"));

  // Names that don't involve a newtype are unaffected.
  KJ_EXPECT(main.typeIdAt("plain @9 :Plain", "Plain") == id(MAIN_ID, "Plain"));
  KJ_EXPECT(main.typeIdAt("raw @10 :UInt16", "UInt16") == builtinUInt16);
}

}  // namespace
}  // namespace compiler
}  // namespace capnp

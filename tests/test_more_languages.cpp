#include "test_util.h"

using namespace mfdp;

#define OPTIONAL_RUNTIME(body)                                                      \
  try {                                                                             \
    body                                                                            \
  } catch (const mfdp::Error& e_) {                                              \
    if (e_.code() == mfdp::ErrorCode::AdapterUnavailable) {                      \
      std::cout << "       skipped: " << e_.what() << "\n";                         \
      return;                                                                       \
    }                                                                               \
    throw;                                                                          \
  }

TEST(python_star_import_resolves_cross_file) {
  WriteOptions o = baseOptions({fixture("multi/star_user.py"), fixture("multi/util.py")});
  o.kwrds = "use";
  Artifact a = buildArtifacts(o)[0];
  bool helper = false;
  bool limit = false;
  for (const auto& d : a.files[0].deps) {
    if (d.name == "helper" && d.status == DepStatus::CrossFile && d.targetFile == 1) helper = true;
    if (d.name == "LIMIT" && d.status == DepStatus::CrossFile && d.targetFile == 1) limit = true;
  }
  CHECK(helper);
  CHECK(limit);
}

TEST(nested_member_paths_resolve_to_the_deepest_symbol) {
  WriteOptions o;
  o.memory.push_back({"nested.py",
                      "class Outer:\n    class Inner:\n        @staticmethod\n        def go():\n            return 1\n\n"
                      "def use():\n    return Outer.Inner.go()\n"});
  o.kwrds = "use";
  Artifact a = buildArtifacts(o)[0];
  const FileRecord& f = a.files[0];
  bool deepest = false;
  for (const auto& d : f.deps) {
    if (d.name == "Outer.Inner.go" && d.targetSymbol >= 0) {
      deepest = f.symbols[d.targetSymbol].qualified == "Outer.Inner.go" && d.kind == DepKind::Call;
    }
  }
  CHECK(deepest);
}

TEST(all_target_languages_are_marked_implemented) {
  for (const char* id : {"python", "typescript", "javascript", "c", "cpp", "go", "java", "csharp", "rust"}) {
    const LanguageEntry* e = findLanguageById(id);
    CHECK(e != nullptr && e->implemented);
  }
  const LanguageEntry* ruby = findLanguageById("ruby");
  CHECK(ruby != nullptr && !ruby->implemented);
}

TEST(single_c_file) {
  OPTIONAL_RUNTIME({
    Artifact a = buildArtifacts(baseOptions({fixture("sample.c")}))[0];
    const FileRecord& f = a.files[0];
    CHECK_EQ(f.language, std::string("c"));
    const Symbol* add = findSymbol(f, "add");
    CHECK(add && add->kind == SymbolKind::Function);
    CHECK_EQ(add->params.size(), 2u);
    CHECK(add->returnType == "int");
    const Symbol* scale = findSymbol(f, "scale");
    CHECK(scale != nullptr);
    CHECK(contains(scale->doc, "Scale a point"));
    CHECK(findSymbol(f, "MAX_ITEMS") != nullptr);
    CHECK(findSymbol(f, "Color") && findSymbol(f, "Color")->kind == SymbolKind::Enum);
    CHECK(findSymbol(f, "counter") != nullptr);
    CHECK_EQ(f.totalImports, 2u);
    bool addDep = false;
    for (const auto& d : f.deps) {
      if (d.name == "add" && d.status == DepStatus::SameFile) addDep = true;
    }
    CHECK(addDep);
  })
}

TEST(single_cpp_file) {
  OPTIONAL_RUNTIME({
    Artifact a = buildArtifacts(baseOptions({fixture("sample.cpp")}))[0];
    const FileRecord& f = a.files[0];
    CHECK_EQ(f.language, std::string("cpp"));
    CHECK(findSymbol(f, "shapes") && findSymbol(f, "shapes")->kind == SymbolKind::Namespace);
    const Symbol* circle = findSymbol(f, "shapes::Circle");
    CHECK(circle && circle->kind == SymbolKind::Class);
    CHECK_EQ(circle->bases.size(), 1u);
    const Symbol* area = findSymbol(f, "shapes::Circle::area");
    CHECK(area && area->kind == SymbolKind::Method);
    CHECK(area->parentPlus1 > 0);
    CHECK(findSymbol(f, "shapes::Circle::Circle") != nullptr);
    CHECK(findSymbol(f, "shapes::total") != nullptr);
  })
}

TEST(single_go_file) {
  OPTIONAL_RUNTIME({
    Artifact a = buildArtifacts(baseOptions({fixture("sample.go")}))[0];
    const FileRecord& f = a.files[0];
    CHECK_EQ(f.language, std::string("go"));
    const Symbol* describe = findSymbol(f, "Describe");
    CHECK(describe && describe->kind == SymbolKind::Function && describe->exported == Tri::Yes);
    CHECK_EQ(describe->params.size(), 1u);
    CHECK(describe->returnType == "string");
    const Symbol* rect = findSymbol(f, "Rect");
    CHECK(rect && rect->kind == SymbolKind::Class);
    const Symbol* area = findSymbol(f, "Rect.Area");
    CHECK(area && area->kind == SymbolKind::Method && area->parentPlus1 > 0);
    CHECK(findSymbol(f, "Shape") && findSymbol(f, "Shape")->kind == SymbolKind::Interface);
    CHECK(findSymbol(f, "Version") && findSymbol(f, "Version")->kind == SymbolKind::Constant);
    bool fmtExternal = false;
    for (const auto& d : f.deps) {
      if (d.name == "fmt.Sprintf" && d.status == DepStatus::External && d.module == "fmt") fmtExternal = true;
    }
    CHECK(fmtExternal);
  })
}

TEST(c_include_resolves_header_symbols) {
  OPTIONAL_RUNTIME({
    WriteOptions o = baseOptions({fixture("multi_c/main.c"), fixture("multi_c/util.h")});
    o.kwrds = "run";
    Artifact a = buildArtifacts(o)[0];
    bool found = false;
    for (const auto& d : a.files[0].deps) {
      if (d.name == "helper" && d.status == DepStatus::CrossFile && d.targetFile == 1) found = true;
    }
    CHECK(found);
  })
}

TEST(go_same_package_files_resolve) {
  OPTIONAL_RUNTIME({
    WriteOptions o = baseOptions({fixture("multi_go/a.go"), fixture("multi_go/b.go")});
    o.kwrds = "Run";
    Artifact a = buildArtifacts(o)[0];
    bool found = false;
    for (const auto& d : a.files[0].deps) {
      if (d.name == "helper" && d.status == DepStatus::CrossFile && d.targetFile == 1) found = true;
    }
    CHECK(found);
  })
}

TEST(c_go_java_rust_csharp_syntax_errors_are_structured) {
  for (const char* name : {"bad.c", "bad.go", "bad.java", "bad.rs", "bad.cs"}) {
    try {
      buildArtifacts(baseOptions({fixture(name)}));
      CHECK(false);
    } catch (const mfdp::Error& e) {
      if (e.code() == mfdp::ErrorCode::AdapterUnavailable) {
        std::cout << "       skipped: " << e.what() << "\n";
        continue;
      }
      CHECK(e.code() == mfdp::ErrorCode::ParseFailed);
      CHECK(e.line() >= 1u);
    }
  }
}

TEST(single_java_file) {
  OPTIONAL_RUNTIME({
    Artifact a = buildArtifacts(baseOptions({fixture("sample.java")}))[0];
    const FileRecord& f = a.files[0];
    CHECK_EQ(f.language, std::string("java"));
    const Symbol* cls = findSymbol(f, "Inventory");
    CHECK(cls && cls->kind == SymbolKind::Class && cls->exported == Tri::Yes);
    CHECK_EQ(cls->bases.size(), 2u);
    const Symbol* add = findSymbol(f, "Inventory.add");
    CHECK(add && add->kind == SymbolKind::Method && add->parentPlus1 > 0);
    CHECK_EQ(add->params.size(), 2u);
    CHECK_EQ(add->params[1].annotation, std::string("int..."));
    CHECK(contains(add->doc, "Add an item."));
    CHECK(findSymbol(f, "Inventory.LIMIT") && findSymbol(f, "Inventory.LIMIT")->kind == SymbolKind::Constant);
    CHECK(findSymbol(f, "Inventory.Level") && findSymbol(f, "Inventory.Level")->kind == SymbolKind::Enum);
    CHECK(findSymbol(f, "Inventory.Inventory") && findSymbol(f, "Inventory.Inventory")->kind == SymbolKind::Constructor);
    CHECK_EQ(f.totalImports, 2u);
    bool validate = false;
    for (const auto& d : f.deps) {
      if (d.name == "this.validate" && d.status == DepStatus::SameFile && d.targetSymbol >= 0 &&
          f.symbols[d.targetSymbol].qualified == "Inventory.validate") {
        validate = true;
      }
    }
    CHECK(validate);
  })
}

TEST(java_cross_file_import_resolves) {
  OPTIONAL_RUNTIME({
    WriteOptions o = baseOptions({fixture("multi_java/pkg/a/Service.java"), fixture("multi_java/pkg/b/Repo.java")});
    o.kwrds = "load";
    Artifact a = buildArtifacts(o)[0];
    bool found = false;
    for (const auto& d : a.files[0].deps) {
      if (d.status == DepStatus::CrossFile && d.targetFile == 1 && d.targetSymbol >= 0 &&
          a.files[1].symbols[d.targetSymbol].qualified == "Repo.find") {
        found = true;
      }
    }
    CHECK(found);
  })
}

TEST(single_rust_file) {
  OPTIONAL_RUNTIME({
    Artifact a = buildArtifacts(baseOptions({fixture("sample.rs")}))[0];
    const FileRecord& f = a.files[0];
    CHECK_EQ(f.language, std::string("rust"));
    const Symbol* inv = findSymbol(f, "Inventory");
    CHECK(inv && inv->kind == SymbolKind::Class && inv->exported == Tri::Yes);
    CHECK(inv->bases.size() == 1 && inv->bases[0] == "Describe");
    const Symbol* add = findSymbol(f, "Inventory::add");
    CHECK(add && add->kind == SymbolKind::Method && add->parentPlus1 > 0);
    CHECK(contains(add->doc, "Add stock."));
    CHECK(findSymbol(f, "Describe") && findSymbol(f, "Describe")->kind == SymbolKind::Interface);
    CHECK(findSymbol(f, "Inventory::describe") != nullptr);
    CHECK(findSymbol(f, "MAX_ITEMS") && findSymbol(f, "MAX_ITEMS")->kind == SymbolKind::Constant);
    CHECK(findSymbol(f, "total") && findSymbol(f, "total")->kind == SymbolKind::Function);
    CHECK(findSymbol(f, "Inventory::items") != nullptr);
    bool validate = false;
    for (const auto& d : f.deps) {
      if (d.name == "this.validate" && d.status == DepStatus::SameFile) validate = true;
    }
    CHECK(validate);
  })
}

TEST(single_csharp_file) {
  OPTIONAL_RUNTIME({
    Artifact a = buildArtifacts(baseOptions({fixture("sample.cs")}))[0];
    const FileRecord& f = a.files[0];
    CHECK_EQ(f.language, std::string("csharp"));
    CHECK(findSymbol(f, "Demo") && findSymbol(f, "Demo")->kind == SymbolKind::Namespace);
    CHECK(findSymbol(f, "Demo.IShape") && findSymbol(f, "Demo.IShape")->kind == SymbolKind::Interface);
    const Symbol* circle = findSymbol(f, "Demo.Circle");
    CHECK(circle && circle->kind == SymbolKind::Class && circle->bases.size() == 1);
    const Symbol* area = findSymbol(f, "Demo.Circle.Area");
    CHECK(area && area->kind == SymbolKind::Method && area->parentPlus1 > 0);
    CHECK(contains(area->doc, "Area of the circle."));
    CHECK(findSymbol(f, "Demo.Circle.Pi") && findSymbol(f, "Demo.Circle.Pi")->kind == SymbolKind::Constant);
    CHECK(findSymbol(f, "Demo.Circle.Circle") && findSymbol(f, "Demo.Circle.Circle")->kind == SymbolKind::Constructor);
    CHECK(findSymbol(f, "Demo.Circle.Total") != nullptr);
    CHECK_EQ(f.totalImports, 2u);
  })
}

